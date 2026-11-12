# Handoff: refatoração SOLID/Hexagonal + merges recorrentes do upstream

Leia este arquivo inteiro antes de mexer na branch. Depois, para o detalhe de cada fase, veja
`tasks/todo.md` (checklist com resultados), `tasks/plan.md` (planos por fase) e
`tasks/plan_merge_main.md` (planos de merge). As regras do `AGENTS.md` da raiz continuam valendo.

## 1. O que é o projeto

- Servidor Canary (MMORPG, C++20). Branch de trabalho: `nicollassantos/solid`.
- **Modelo de ameaça: o cliente é hostil.** OTServers são muito atacados por concorrentes. A
  base é aberta: quem conhece um bug, ou o corrigiu só no próprio servidor, usa esse bug como
  ataque contra os outros. Exemplo real: um bug ao usar cama, explorado com milhares de
  jogadores online, levou dias de debug. Por isso:
  - toda entrada vinda do cliente (pacotes, ids, posições, contagens, páginas, textos) é
    validada na fronteira;
  - falha vira rejeição segura, nunca crash, corrupção ou loop;
  - recursos que o cliente controla (caches, filas, timers) têm limite;
  - toda rejeição ou anomalia deixa rastro para depurar (player, IP, pacote, argumentos, com log
    limitado por taxa) e uma métrica.
- **Monitorar cada aspecto:** OpenTelemetry → Prometheus (`FEATURE_METRICS`, presets
  `*-release-metrics`, exporter em `metricsPrometheusAddress` (padrão `0.0.0.0:9464`), stack em
  `metrics/docker-compose.yml` + `metrics/prometheus/prometheus.yml`). Caminho novo ou
  endurecido ganha contador ou latência (`g_metrics().addCounter`, `metrics::method_latency`).
  O preset de testes compila os stubs no-op, então valide também com um preset `-metrics`
  quando mexer em métricas.
  **O Prometheus só recebe dados com duas chaves ligadas:**
  1. Build com preset `*-release-metrics` (`FEATURE_METRICS=ON`). Nos outros, inclusive
     `linux-release` e o de testes, as métricas são funções vazias.
  2. `metricsEnablePrometheus = true` no `config.lua`. No `config.lua.dist` vem `false`, e o
     `config.lua` local também estava `false` em 2026-11-12.

  O `metrics/prometheus/prometheus.yml` lê `host.docker.internal:9464`, isto é, o servidor
  rodando no host, fora do docker. Confira as duas chaves no servidor de produção; sem elas,
  nenhuma métrica nova chega ao Prometheus.
- **Objetivo do fork: ser uma versão do Canary extremamente sólida, altamente testável, fácil
  de depurar, de manter à mão e de monitorar.** Use isso para priorizar:
  - portas e fakes onde permitem testar comportamento que hoje não tem teste;
  - testes de integração (MySQL) para todo SQL;
  - correções de estabilidade: lifetime de callbacks, null-deref, perda de itens ou ouro;
  - smoke boot a cada fase.

- Manutenção manual: um humano precisa achar o código e fazer o merge sem depender das
  ferramentas.
  - Prefira arquivos e funções pequenos, com nomes que digam o que fazem.
  - Uma porta só quando ela paga com teste; nada de camadas extras.
  - Mantenha a assinatura do upstream quando possível (só muda o receptor).
  - Registre cada movimento de código no mapa da seção 5b.
  - Comentários explicam o porquê, não o quê.

  Refatoração que só renomeia, sem ganho de teste, estabilidade ou clareza, não vale o custo de
  conflito nos merges.
- `origin` = fork `nicollassantos/canary`. `origin/main` espelha o `main` do projeto original
  (opentibiabr/canary).
- A branch faz duas coisas ao mesmo tempo:
  1. Refatoração contínua para SOLID/Hexagonal: quebra de `Game`, `Player` e `ProtocolGame` em
     services, components e arquivos menores; portas e adapters para IO e configuração; DI com
     boost::di; testes.
  2. **Merges recorrentes do `main` upstream.** Isso vai continuar sempre. Todo merge precisa
     levar as mudanças upstream para o lugar onde o código vive hoje (seção 5).

## 2. Regras de trabalho combinadas com o usuário

- Responder ao usuário em português. Commits, PRs e comentários de código em inglês.
- **Nunca dar push**, em especial para `origin/main`. Commits só locais, um por task, com
  Conventional Commits, terminando em `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- `git add` só com caminhos explícitos. Mudanças não commitadas que não são suas: pergunte ao
  usuário se vão para commit próprio ou se devem ser descartadas. Nunca faça stash, reset ou
  commit por conta própria. Exemplo: o aviso de preâmbulo "world name" em `connection.cpp`
  ficou fora de commit da Phase 17 até a 20; o usuário mandou manter e ele entrou em
  `b6fe41f24`.
- Fluxo por fase: o usuário pede "abra a Phase N e pode iniciar". Aí registrar a fase em
  `tasks/plan.md` e `tasks/todo.md` (commit `docs(tasks): ...`), executar task a task (build +
  testes + commit por task), fechar com o CHECKPOINT no `todo.md` e um relatório curto.
- Banco: nunca apontar o servidor para o banco real do usuário. Smoke tests só em banco
  descartável (seção 7), apagado ao fim.
- `config.lua` é local e está no gitignore. O usuário usa `worldType = "open-pvp"` com o datapack
  `data-otservbr-global`. Não versionar. Já houve um incidente em que `config.lua.dist` foi
  sobrescrito por um processo externo com o conteúdo local; o usuário reverteu. Confira o diff
  antes de commitar.
- PR/comentários em PR só com pedido explícito.

## 3. Arquitetura e padrões em uso

### Camadas

- **Services** (`Game` delega; recebem `Game&` e `IConfigManager&` no construtor; não usam mais
  `g_game()`/`g_configManager()`): `src/game/{movement,interaction,trade,loot,outfit,chat,forge,
  market,highscore,cyclopedia,sound,bank}/…_service.cpp`, `src/items/item_service.cpp`,
  `src/creatures/*/…_service.cpp` (CombatService, CreatureManagementService).
- **Player components** (`src/creatures/players/components/player_*_component.cpp`): pedaços de
  `player.cpp` movidos para lá. `Player` mantém delegates. Leem config por
  `IConfigManager::getInstance()` (zero `g_configManager()`). Na 20.3, parte das chamadas `g_game()` passou para `CreatureFeedback`/`WorldRegistry`.
- **ProtocolGame** dividido em `protocolgame.cpp` (parse/login), `protocolgame_send.cpp` (núcleo),
  `_send_cyclopedia.cpp`, `_send_economy.cpp` (market/store/forge/prey/imbuement),
  `_send_social.cpp` (chat/VIP/party) e `protocolgame_helpers.hpp`.
- Limites acordados: `game.cpp` < 4k linhas, `player.cpp` < 7k, `protocolgame.cpp` < 7k. Se um
  merge passar disso, mover funções novas para services ou components.

### Portas (driven ports) e adapters

| Porta | Adapter de produção | Fake de teste (`tests/fixture/`) |
|---|---|---|
| `IConfigManager` (`config/config_port.hpp`) | `ConfigManager` (binding para **a instância carregada**) | `config/in_memory_config_manager.hpp` |
| `IDatabase` (`database/database_port.hpp`) | `Database` | — (testes de integração usam MySQL) |
| `IPlayerRepository` | `PlayerRepositoryDB(IDatabase&)`: dono do SQL de nome/guid/saldo desde a 21.1; load/save e `gameWorldAuthentication` ainda delegam para `IOLoginData` | `creatures/in_memory_player_repository.hpp` |
| `IVipRepository` | `VipRepositoryDB(IDatabase&)`, dono do SQL desde a 20.2 | `creatures/in_memory_vip_repository.hpp` |
| `IMarketRepository` | `MarketRepositoryDB(IDatabase&, IConfigManager&)`, dono do SQL desde a 20.1 | `io/in_memory_market_repository.hpp` |
| `IGuildRepository` | `GuildRepositoryDB(IDatabase&)`, dono do SQL; `IOGuild` removido | `io/in_memory_guild_repository.hpp` |
| `IPlayerStorageRepository` | `PlayerStorageRepositoryDB` | — |
| `ExpertPvpWorld` | a própria classe (concreta, métodos virtuais sobre `g_game()`) | fake por herança |
| `CreatureFeedback` (`g_creatureFeedback()`) | a própria classe (concreta, sobre `g_game()`) | `creatures/recording_creature_feedback.hpp` |
| `WorldRegistry` (`g_worldRegistry()`) | a própria classe (concreta, sobre `g_game()`) | `creatures/fake_world_registry.hpp` |

- Container: `src/lib/di/container.hpp`. `DI::port<T>()` devolve o fake se o container de teste
  tiver binding de `T`; senão, o binding de produção. Todas as `getInstance()` de portas usam
  isso. `isBound<T>()` foi adicionado ao injector do projeto (`src/lib/di/injector.hpp`).
- **Porta concreta virtual** (como `ExpertPvpWorld`): use quando os testes que não fazem binding
  precisam continuar funcionando. O injector de teste constrói sozinho um tipo concreto sem
  binding; um tipo abstrato sem binding dispara assert (e com NDEBUG vira nullptr e crash).
- Adapter novo: injeção pelo construtor (`IDatabase&`, `IConfigManager&`), binding
  `.in(di::singleton)`, teste em `tests/unit/lib/di/container_singletons_test.cpp` provando
  instância única e teste de integração com MySQL em `tests/integration/io/`.
- Portas resolvidas **na hora da chamada** (`world()`, `X::getInstance()`), nunca guardadas em
  membro por player. Guardar já causou segfault na desmontagem de testes.

### Armadilhas do boost::di

- Construtor privado gera o erro enganoso `has_to_many_constructor_parameters`. Foi o caso do
  `Scripts`; a correção foi deixar o construtor público.
- `cfg()` do injector é protegido. Para perguntar se há binding, use `DI::isBound`/`isBound<T>()`.
- `Modules`/`metrics::Metrics` resolvem por binding implícito. Binding explícito esbarra em
  ciclo de include (os headers incluem `container.hpp`).
- Testes: guarde `DI::getTestContainer()`, instale o seu com `DI::setTestContainer(...)` e, na
  desmontagem, **restaure o anterior antes de destruir o injector**. Destruir o injector ainda
  instalado já causou segfault. Funcionam dois formatos: injector por teste
  (`std::unique_ptr`, como em `player_death_component_test.cpp`) ou `inline static` por suíte
  (`SetUpTestSuite`/`TearDownTestSuite`).

### Regras de C++ que já quebraram o build

- Unity build esconde include faltando. Sempre rodar `tools/merge/syntax_check.sh` (compila cada
  `.cpp` sozinho, sem unity) nos arquivos tocados.
- Helpers em namespace anônimo com o mesmo nome em dois `.cpp` colidem no unity build
  (aconteceu com `componentConfig()`). Use nomes únicos ou chamada direta.
- PCH: include que vem do PCH precisa de guarda `#ifndef USE_PRECOMPILED_HEADERS`.
- Arquivo novo ou removido: atualizar o `CMakeLists.txt` da pasta, `vcproj/canary.vcxproj`
  (LF, ASCII) e o CMake de teste quando houver.
- Callbacks diferidos (dispatcher, `g_databaseTasks()`) não capturam `this`, referências nem
  iteradores. Capture valores e resolva as portas dentro do callback (ver
  `game/market/market_expiry.cpp`).
- `MarketOfferList` é `std::list` (sem `operator[]`).

## 4. Validação (build e testes)

```sh
export VCPKG_ROOT=/home/nicollas/vcpkg            # o preset depende disso
cmake --preset linux-release-enabled-tests        # só se o build tree não existir
cmake --build build/linux-release-enabled-tests -j"$(nproc)"
tools/merge/syntax_check.sh build/linux-release-enabled-tests <arquivos .cpp tocados>
build/linux-release-enabled-tests/tests/unit/canary_ut --gtest_brief=1
build/linux-release-enabled-tests/tests/integration/canary_it --gtest_brief=1
```

- Leia antes `docs/building/local-validation.md` (regra do `AGENTS.md` raiz).
- Build demora. Rode em background e espere a notificação; não faça polling com `sleep`. Um
  waiter do tipo `[ $(grep -c … || echo 0) -ge 2 ]` quebra (gera "0\n0") e nunca termina.
- Os presets `*-release-enabled-tests` têm `DEBUG_LOG=ON` (paridade com o CI upstream
  `linux-debug`; `ConnectionWriteDiagnosticsTest` depende disso). O `./canary` gerado por esse
  preset tem DEBUG_LOG. Para produção, use `linux-release`.
- Integração: MySQL no container docker `mysql` (imagem `mysql:8`). Credenciais em
  `tests/test.env` (`TESTS_ENV_DEFAULT`) ou `TEST_ENV_FILE`. Cada teste roda dentro de
  `BEGIN`/`ROLLBACK` (`databaseTest`). Caminhos async (`g_databaseTasks()`) ficam fora dessa
  transação; não teste por integração.
- vcpkg: se `vcpkg.json` mudar o baseline, faça checkout de `$VCPKG_ROOT` nesse commit e rode
  `bootstrap-vcpkg.sh`. Se o `CMakeCache.txt` apontar `find_*` para um `vcpkg_installed` antigo,
  recrie `build/<preset>`. `VCPKG_INSTALLED_DIR` precisa ficar dentro do build tree
  (SharedBuildCache.cmake).

## 5. Merge recorrente do upstream (`origin/main`)

Ferramentas e passo a passo: `tools/merge/README.md`. Resumo e lições:

1. Árvore limpa, baseline verde e branch de backup local. Se houver diff não commitado que não
   é seu, **pergunte ao usuário** antes (seção 2). Sem commit, um reset ou checkout o perde, e o
   git recusa o merge se o upstream também tiver mudado o arquivo.
2. `git fetch origin && git merge --no-commit origin/main`. É merge real com dois pais. O último
   merge foi `deba5a464`, com base upstream `666a60f6f`; o próximo considera só commits novos
   depois desse ponto.
3. Arquivos com código movido: `git checkout --ours` e merge por função com
   `split_merge.py` (protocolgame) e `moved_merge.py` (game.cpp → services, player.cpp →
   components). Revisar todas as categorias de saída do tool:
   - `merged_conflict*`: resolver os marcadores e qualificar membros (`game_.`, `m_player.`).
   - `merged_clean_moved`: revisar mesmo assim, porque podem entrar chamadas não qualificadas.
   - `deleted_upstream_port_by_hand` e `excluded_insert`: mudança de assinatura; atualizar
     declaração, delegate e implementação juntos.
   - `new_namespace_member_place_by_hand`: mover o bloco de namespace anônimo inteiro para o
     arquivo certo.
   - `*_has_duplicates`: há cópia morta. Na Phase 17, a mudança de `sendBlockEffect` caiu numa
     cópia morta em `game.cpp`, e o código vivo era o `CombatService`.
4. `audit_moved.py` para `game.cpp` e `player.cpp` precisa dar zero misses. Arquivos que o
   upstream ainda tem e a branch apagou (`iomarket`, `ioguild`, blocos VIP e nome/guid/saldo de `iologindata`)
   seguem a seção "Upstream files deleted on the branch" de `tools/merge/README.md`. Linhas
   com receptor trocado nos components seguem a tabela "Receiver substitutions" do mesmo
   arquivo.
5. Aplicar a arquitetura ao código novo do upstream: função nova em `Game` vai para service,
   em `Player` vai para component, e IO estático novo vai para porta.
6. Build, `syntax_check.sh`, ut + it e smoke boot (seção 7). Registrar a fase no `todo.md` e
   atualizar o mapa da seção 5b.
7. Mudanças upstream conhecidas que afetam a branch: `"pvp"` virou alias de retro-pvp (o
   upstream removeu `toggleServerIsRetroPVP`); a branch adicionou `"open-pvp"`
   (`game/world_type_config`). Multiprotocolo e Expert PvP vieram do upstream.

## 5b. Mapa upstream → branch (o `main` sempre chega no formato do upstream)

O `origin/main` sempre traz o código como ele está no projeto original: `game.cpp`, `player.cpp`
e `protocolgame.cpp` inteiros, `iomarket`/`ioguild` existindo, `g_game()`/`g_configManager()`
em toda parte. Toda mudança upstream num desses lugares precisa chegar ao caminho onde o
código vive na branch. **Ao mover código em qualquer fase, acrescente a linha aqui e em
`tools/merge/README.md` no mesmo commit.**

| Upstream (como vem no `main`) | Onde está na branch | O que fazer no merge |
|---|---|---|
| `src/game/game.cpp` | `game.cpp` (delegates e o que sobrou) e os services: `src/game/{chat,cyclopedia,forge,highscore,interaction,loot,market,movement,outfit,sound,trade}/*_service.cpp`, `src/items/item_service.cpp`, `src/creatures/combat/combat_service.cpp`, `src/creatures/management/creature_management_service.cpp`, `src/game/scheduling/monster_post_think_queue.cpp` | `moved_merge.py` (comando no README). Nas linhas tocadas: `g_game().` → `game_.`, `g_configManager()` → `config_` |
| helpers de namespace do `game.cpp` (teleport guard, dinheiro, market, `playerCanUseItemOnHouseTile`) | `teleport_guard.cpp`, `item_service.cpp`, `market_service.cpp`, `game_helpers.hpp` | manual: não casam por nome (tabela "Namespace helpers" do README) |
| `src/creatures/players/player.cpp` | `player.cpp` (delegates) e `src/creatures/players/components/player_*_component.cpp`, `player_race_icon_overlays.cpp` | `moved_merge.py`. Nas linhas tocadas: `m_player.` nos membros; `g_configManager()` → `IConfigManager::getInstance()`; `g_game().X` → `g_creatureFeedback().X`/`g_worldRegistry().X` conforme a tabela "Receiver substitutions" |
| `src/server/network/protocol/protocolgame.cpp` | `protocolgame.cpp` (parse/login), `protocolgame_send.cpp`, `protocolgame_send_cyclopedia.cpp`, `protocolgame_send_economy.cpp`, `protocolgame_send_social.cpp`, `protocolgame_helpers.hpp`; despacho de comandos em `src/server/network/commands/game_command_dispatcher.cpp` | `split_merge.py` com os **6** arquivos (comando no README) |
| `src/io/iomarket.{cpp,hpp}` | `src/io/market_repository_db.cpp`; expiração/reembolso em `src/game/market/market_expiry.cpp` | apagado na branch: `moved_merge.py` + port manual de `processExpiredOffers`/`getTierFromDatabaseTable`; `git rm` de novo |
| `src/io/ioguild.{cpp,hpp}` | `src/io/guild_repository_db.cpp` | apagado: `moved_merge.py`; `git rm` de novo |
| `src/io/iologindata.cpp`: VIP; `getNameByGuid`/`getGuidByName`/`getGuidByNameEx`/`formatPlayerName`/`increaseBankBalance` | `src/creatures/players/vip_repository_db.cpp`, `src/creatures/players/player_repository_db.cpp` | `moved_merge.py` com os 3 arquivos; load/save continuam em `iologindata.cpp` |
| `game.cpp` `Game::updatePlayersOnline` | `CyclopediaService::updatePlayersOnline` (monta a lista de GUIDs) e `PlayerRepositoryDB::updateOnlinePlayers` (SQL) | manual: corpo dividido (README, "split between a service and an adapter") |
| `game.cpp` highscores: `generateHighscoreQuery`, `generateVocationConditionHighscore`, `cacheQueryHighscore`, `generateHighscoreOrGetCachedQueryFor*`, `queryCache` | `src/io/highscore_repository_db.cpp` (`HighscoreRepositoryDB`, mesmos nomes) | `moved_merge.py` casa por nome (adapter no comando do `game.cpp`); `QueryHighscoreCacheEntry`/`queryCache` e os delegates privados saíram do `game.hpp` |
| `game.cpp` `Game::playerHighscores` e `Game::processHighscoreResults` | `HighscoreService` (pedido, task assíncrona, cache de resultado, envio) e `HighscoreRepositoryDB` (query, parse) | manual: corpo dividido (README) |
| `src/canary_server.cpp`: parsing de `worldType` | `src/game/world_type_config.cpp` (`world_type_config::parse`) | se o upstream mudar nomes de world type, atualizar o parse e `world_type_config_test.cpp`; manter `"open-pvp"` |
| `src/creatures/players/components/pvp/expert_pvp.cpp` | mesmo arquivo, com `g_game()`/config → `world()` (`ExpertPvpWorld`) | merge normal; manter `world().` nas linhas tocadas |

**Correções e trocas da branch dentro de arquivos do upstream** (o merge **não** pode
desfazer; se o upstream mexer nessas linhas, reaplique):

| Arquivo upstream | O que a branch mudou | Por quê |
|---|---|---|
| `src/io/iologindata.cpp` `gameWorldAuthentication` | personagem marcado para deleção é recusado (busca sem diferenciar maiúsculas na lista da conta); IP no log | o `players[name] != 0` original nunca recusava, porque a lista da conta já exclui deletados (teste `CharacterMarkedForDeletionIsRefused`) |
| `src/account/account_repository.cpp` | `inject<AccountRepository>()` → `DI::port<AccountRepository>()` | testes com container próprio caíam (nullptr) em tudo que passa por `Account` |
| `src/creatures/players/components/player_vip.cpp` | `IOLoginData::*VIP*` → `g_vipRepository()`; guarda de `account` nulo em `edit` | porta VIP (19.3); null-deref |
| `src/creatures/players/components/pvp/expert_pvp.cpp` | `g_game()`/config → `world()` | porta `ExpertPvpWorld` (18.4) |
| `src/server/network/connection/connection.cpp` | preâmbulo "world name" que não bate com `serverName` vira aviso + fechamento | diagnóstico de login (`b6fe41f24`) |
| `game.cpp` `cacheQueryHighscore` (hoje em `highscore_repository_db.cpp`) e `processHighscoreResults` (hoje em `highscore_service.cpp`) | `highscore_cache::prune` antes de inserir (expiradas saem; no teto, o cache é limpo) + métrica `highscore_cache_evictions` | as chaves vêm do cliente; no upstream os caches crescem sem fim |
| `game.cpp` `playerHighscores` (hoje em `highscore_service.cpp`) | página 0 em `HIGHSCORE_GETENTRIES` é recusada antes da query + métrica `highscore_requests_rejected` | `(page - 1)` dava underflow e rodava o ranking inteiro no MySQL |

Chamadas: o upstream chama `IOLoginData::`/`IOMarket::`/`IOGuild::` fora de `src/io`. Na
branch, toda chamada passa por `g_playerRepository()`/`g_vipRepository()`/
`g_marketRepository()`/`g_guildRepository()` (tabela "Call sites" do README). Depois de cada
merge, o `git grep` do README precisa voltar vazio.

Cobertura: depois de mover código ou de um merge, rode `tools/merge/coverage_check.py` para
`game.cpp`, `player.cpp` e `protocolgame.cpp`; o resultado precisa ser 0 funções sem cópia local
(fora os helpers de namespace listados). Na Phase 22 o resultado foi 416/416, 830/830 e 398/398.

Código novo do upstream:
- Função nova em `Game` vai para o service do assunto.
- Função nova em `Player` vai para o component.
- IO estático novo vai para porta/adapter.
- Se `game.cpp` passar de 4k linhas ou `player.cpp` de 7k, mova antes de commitar o merge.
- Escreva a linha correspondente nesta tabela.

## 6. Estado atual (Phase 20 concluída)

Linha de base da Phase 20: `g_game()` 1143 e `g_configManager()` 458 em `src/*.cpp`. Nos
components há 183 `g_game()`: 93 em arquivos só da branch e 90 em arquivos que também existem
upstream.

- **20.1 Market**: concluída no commit `79aabb80c`. Resultado: ut 874/874, it 64/64;
  `g_game()` 1140 e `g_configManager()` 452 (linhas contadas com `git grep`).
  - `MarketRepositoryDB(IDatabase&, IConfigManager&)` agora é dono do SQL e das estatísticas
    (mapas + mutex). `src/io/iomarket.{cpp,hpp}` foram removidos (CMake, vcxproj e includes
    atualizados).
  - Porta: `checkExpiredOffers()` virou
    `loadExpiredOffers(time_t, std::function<void(std::vector<ExpiredMarketOffer>)>)`.
  - Reembolso de ofertas expiradas (lógica de jogo) foi para
    `src/game/market/market_expiry.{hpp,cpp}`: `market_expiry::checkExpiredOffers()`, chamado
    em `canary_server.cpp`, e `processExpiredOffers(Game&, IMarketRepository&,
    IPlayerRepository&, offers)`. O corpo foi movido sem reordenar "mover para histórico →
    reembolsar".
  - Testes: `tests/unit/game/market_expiry_test.cpp` (4),
    `tests/integration/io/market_repository_db_it.cpp` (8) e um teste de container para
    `MarketRepositoryDB`. A fixture em memória ganhou `loadExpiredOffers`, `hasOffer`,
    `historySize` e `lastHistoryState`.
  - Diff conferido: o corpo do reembolso só trocou a fonte dos dados (struct no lugar de
    `DBResult`) e passou a receber `game`/`players` injetados.
- **20.2 VIP**: concluída no commit `da75f9b33`. Os 10 métodos VIP saíram de
  `IOLoginData` e foram para `VipRepositoryDB(IDatabase&)`, idênticos, com `std::ignore =` nos
  `executeQuery` soltos (`IDatabase::executeQuery` é `[[nodiscard]]`). Testes em
  `tests/integration/io/vip_repository_db_it.cpp`. O trigger `oncreate_accounts` cria 3 grupos
  padrão com AUTO_INCREMENT global, então os testes escolhem um id de grupo livre. Resultado:
  ut 874/874, it 68/68.
- **Antes da 20.3**: o teste `DatabasePortIsTheConnectedGlobalDatabase` (commit `178c4bd55`)
  prova que o `IDatabase` injetado é o `Database` conectado. Smoke boot com
  data-otservbr-global: online em 7s, zero `[error]`, exit 0. Smoke com oferta semeada (uma
  oferta de compra expirada, 2 × 500) confirmou o caminho real: oferta removida, 1 linha
  `EXPIRED` no histórico, saldo do dono 1000.
  - Na Phase 21, o reembolso de venda ganhou unit tests (dono online) e smoke semeado (dono
    offline, inbox salva). A linha de histórico já foi comprovada pelo smoke; o insert é async,
    então os testes de integração não cobrem o sucesso de `moveOfferToHistory`.
- **20.3 Components do branch com portas por papel**: concluída. 93 → 57 `g_game()` nos
  arquivos só da branch; o total em `src` caiu de 1140 para 1104 linhas, sem contar os 16 dos
  adapters. Resultado: ut 885/885, it 68/68.
  - `CreatureFeedback` (`components/creature_feedback.{hpp,cpp}`, `g_creatureFeedback()`):
    `changeSpeed`, `changePlayerSpeed`, `addCreatureHealth`, `addPlayerMana`,
    `internalCreatureChangeOutfit`, `reloadCreature`, `addMagicEffect(pos, effect)`,
    `sendSingleSoundEffect`. Foram 20 chamadas.
  - `WorldRegistry` (`components/world_registry.{hpp,cpp}`, `g_worldRegistry()`):
    `getPlayerByID`, `getCreatureByID`, `getPlayers`, `addPlayer`, `removePlayer`,
    `removeCreature`, `checkPlayersRecord`, `getWorldType`. Foram 16 chamadas.
  - As duas são concretas e virtuais, com assinaturas iguais às de `Game` (só o receptor muda;
    tabela em `tools/merge/README.md`), binding `.in(di::singleton)` e resolução por chamada.
    Fakes: `tests/fixture/creatures/recording_creature_feedback.hpp` e
    `fake_world_registry.hpp`. Testes: `creature_feedback_test.cpp` (5) e
    `world_registry_test.cpp` (5).
  - **Ficam em `g_game()` de propósito**:
    - mutação de itens (forge, imbuement, inventory, stash): um fake que não move itens deixa a
      lógica sem teste;
    - chamadas para outros casos de uso (`internalCloseTrade`, follow/attack, `internalTeleport`,
      `getBedBySleeper`);
    - catálogos (`mounts`, `getItemsClassifications`, `outfitSupportsMount`).

  Arquivos que também existem upstream (`player_attached_effects`, `wheel/player_wheel`,
  cyclopedia, title, badge, achievement, vip, storage, weapon_proficiency) continuam com
  `g_game()`, para reduzir conflito de merge.
  - Nova porta só quando um fake permitir um teste que hoje não é possível; caso contrário é
    só renomear e custa conflito.
- Fora de escopo por decisão: load/save de player em `IOLoginData` (~2000 linhas em
  `src/io/functions/`, que o upstream edita com frequência).

## 6b. Phase 21 (concluída)

- **21.1**: `PlayerRepositoryDB(IDatabase&)` agora é dono de `getNameByGuid`, `getGuidByName`,
  `getGuidByNameEx`, `formatPlayerName` e `increaseBankBalance` (movidos idênticos). Ficam em
  `IOLoginData` o load/save de player, `gameWorldAuthentication` (lógica de autenticação, sem
  teste) e `getAccountType` (código morto, mas o arquivo é do upstream). Defeito conhecido:
  `getGuidByNameEx` lê `g_game().groups`. Testes em
  `tests/integration/io/player_repository_db_it.cpp`.
- **21.2**: `tests/unit/game/market_expiry_test.cpp` cobre o reembolso de venda (empilhável
  100 + 50, não empilhável 1 por unidade, dono desconhecido). Achados:
  - `Player()` não cria inbox; use `Player(nullptr)`, como a produção.
  - O container insere no início da lista.
  - `Item::items` vem do items.xml real nos testes, então ids baixos têm grupo próprio (101
    é "earth", splash). Normalize `group`/`type` e restaure.
  - `Item::setTier` lê o `g_configManager()` global, sem config em unit test.
- **21.3**: smoke semeado passou com dois donos e com um único dono (receita na seção 7).
  `SaveManager::savePlayer` salva player offline de forma síncrona, então a venda e a compra do
  mesmo dono não se sobrescrevem.
- Resultado: ut 888/888, it 72/72.

## 7. Smoke test do servidor (banco descartável)

1. Copiar `config.lua` para o scratchpad trocando `mysqlDatabase` para `"canary_smoke_<fase>"` e
   as portas para 17171/17172 (para não colidir com outro servidor).
2. Senha em arquivo `600`; `MYSQL_PWD` via env; nunca ecoar a senha.
3. `docker exec -e MYSQL_PWD mysql mysql -uroot -e "CREATE DATABASE canary_smoke_<fase>;"` e
   importar `schema.sql`.
4. Rodar `./canary` com essa config até aparecer `server online`, depois SIGINT. Esperar
   encerramento limpo (exit 0) e checar `error|warn` no log. O único erro conhecido e
   pré-existente é `loadLuaMapUnique` uid 14092 item 2473 (data-otservbr-global).
5. `DROP DATABASE` e apagar a config e a senha copiadas.

**Smoke semeado do market** (único teste do reembolso de expiradas num banco real): depois de
importar o `schema.sql` e antes de subir, insira as linhas abaixo; depois do SIGINT e antes do
`DROP`, confira os valores. `created = 1` faz as ofertas já nascerem expiradas, e o reembolso
roda no startup.

```sql
INSERT INTO `accounts` (`id`, `name`, `email`, `password`, `type`, `premdays`, `lastday`, `creation`) VALUES (990003, 'smoke_trader', 'smoke_trader@test.invalid', '', 1, 0, 0, 0);
INSERT INTO `players` (`name`, `account_id`, `conditions`) VALUES ('Smoke Trader', 990003, '');
-- sell offer: 150 small emeralds (stackable) -> must come back to the offline owner's inbox
INSERT INTO `market_offers` (`player_id`, `sale`, `itemtype`, `amount`, `created`, `anonymous`, `price`, `tier`) VALUES ((SELECT `id` FROM `players` WHERE `name` = 'Smoke Trader'), 1, 3032, 150, 1, 0, 20, 0);
-- buy offer: 2 x 500 -> must come back as bank balance
INSERT INTO `market_offers` (`player_id`, `sale`, `itemtype`, `amount`, `created`, `anonymous`, `price`, `tier`) VALUES ((SELECT `id` FROM `players` WHERE `name` = 'Smoke Trader'), 0, 3031, 2, 1, 0, 500, 0);
```

Valores esperados:
- `market_offers` vazio;
- 2 linhas em `market_history` com `state = 2` (EXPIRED);
- `players.balance` do dono igual a 1000;
- `SUM(count)` em `player_inboxitems` com `itemtype = 3032` igual a 150 (duas linhas: 100 + 50).

O tier fica sem cobertura: as ofertas usam tier 0, e o unit test não consegue afirmar o tier.

## 8. Histórico resumido

| Fase | Resultado |
|---|---|
| 0–13 | Extração de services e components, portas de config/DB/repos, testes de components |
| 14, 16, 17 | Merges do upstream. A 17 foi o primeiro merge real com 2 pais (107 commits) e criou `tools/merge/` |
| 15 | Cobertura de services |
| 18 | Instância única de config; `open-pvp`; `DEBUG_LOG` nos presets de teste; `ExpertPvpWorld`; split do `protocolgame_send`; `Scripts` no DI |
| 19 | Services sem singletons; components com config pela porta; IO estático → portas (`IVipRepository`, `DI::port`); `GuildRepositoryDB` dono do SQL |
| 20 | Market e VIP com SQL nos adapters; portas `CreatureFeedback`/`WorldRegistry` nos components (seção 6) |
| 21 | `PlayerRepositoryDB` dono do SQL de nome/guid/saldo; reembolso de venda coberto (seção 6b) |

Contagem de testes: fim da Phase 19, `canary_ut` 867/867 e `canary_it` 56/56; fim da Phase 20,
885/885 e 68/68; fim da Phase 21, 888/888 e 72/72.
