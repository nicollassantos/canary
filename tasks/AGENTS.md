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

  Consultas úteis:
  - latência por pacote: `method_latency_microseconds_bucket{method=~"packet_.*"}` (o `_count` é o
    número de pacotes por opcode);
  - pacotes que travaram o dispatcher por 50 ms ou mais: `slow_packets_total{opcode}`;
  - conexões recusadas pelo teto de conexões não logadas por IP: `connections_refused_total{reason="per_ip_pending_limit"}` (se subir com jogadores reais, aumente `maxPendingConnectionsPerIp`);
  - edições de lista de casa recusadas: `house_list_updates_rejected{reason="too_long"|"cooldown"}`;
  - pedidos de outfit ignorados pelo intervalo: `outfit_requests_throttled{request="window"|"change"}`;
  - clientes desconectados por fila de saída cheia: `connection_output_queue_overflow_total`
    (acompanhe depois de ligar em produção; se derrubar gente com lag, suba
    `maxOutputQueueMessages`);
  - highscores: `highscore_requests_total`, `highscore_requests_rejected{reason}`,
    `highscore_served_from_cache`, `highscore_cache_evictions{cache}`.
  - save do logout e banco: `player_logout_saves{result="committed"|"failed"}`,
    `player_save_barrier_timeouts` (save pendente há mais de 1 s: relog e escrita Lua em tabela de
    player são recusados até ele commitar), `lua_db_writes_refused` (o script recebe `false`; a maioria
    não confere, então a escrita some, mas fica no log com o SQL), `player_credits_deferred_to_save` /
    `player_credits_deferred_to_save_lost`, `database_transaction_retry_warnings`,
    `database_transactions_lost`. Fora de uma queda do banco, `player_save_barrier_timeouts` e
    `lua_db_writes_refused` devem ficar em zero; se subirem, olhe `database_task_connections_replaced{pool}`
    e aumente `databaseSaveConnections` (as conexões do save são separadas das de highscore e mercado
    desde a 25.24).

  No log, `[PacketWatch]` traz opcode, player, IP e duração (no máximo um aviso a cada 5s, com
  a contagem dos suprimidos).
  Teste de ponta a ponta (Phase 23.0): build `linux-release-metrics`, smoke com
  `metricsEnablePrometheus = true` e `curl :9464/metrics`. Resultado: 7316 linhas, com os
  histogramas `method/lock/lua/query/task_latency`.
  **Segurança:** o padrão `metricsPrometheusAddress = "0.0.0.0:9464"` expõe `/metrics` a
  qualquer endereço (latência por pacote, começo do SQL). Um concorrente veria ao vivo o efeito
  de um ataque. Em produção use `127.0.0.1:9464` ou firewall liberando só o Prometheus.
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
- **Regra de jogo não se inventa** (usuário, 2026-11-19: "Se não sabe, não invente. Pesquise como é no Tibia
  antes!"). Antes de decidir como um sistema se comporta, pesquise o Tibia oficial: manual do tibia.com
  (`/gameguides/?subtopic=manual&section=...`), TibiaWiki BR (`www.tibiawiki.com.br`; o fandom recusa) e,
  como indício, a interface do OTClient (`/home/nicollas/lundaria/otclient`). Cite a fonte no commit e no
  `todo.md`; sem fonte, diga isso, mantenha o upstream ou pergunte.
- Antes de mudar um sistema achado numa auditoria, procure no `tasks/todo.md` uma decisão do usuário
  sobre ele (`git grep -n -i '<sistema>' tasks/`, ex.: "team finder fica como está" na 23.5g). Se houver,
  não mude: leve a evidência nova ao usuário. O team finder foi mudado e revertido por isso (`0c0ef4f07`).
- Regras e notas do fork ficam **só neste arquivo** (`tasks/AGENTS.md`). Não criar outros
  `AGENTS.md`; o da raiz é do upstream (mexer nele gera conflito de merge).

## 2b. Entrada do cliente: regras contra defeitos recorrentes

Todo valor lido de um pacote é hostil: concorrentes rodam clients modificados e bots contra a
base. Cada regra vem de um defeito explorável por pacote forjado (tabela de correções do §5b).

**C++:**
- Índice, slot ou id do pacote: checar contra o tamanho da tabela **e** contra o conjunto
  permitido (vocação, desbloqueio, bestiário, dono) antes de qualquer acesso. Nunca indexar
  `std::array`/`std::vector` direto com valor do pacote. Testar com valor fora da tabela e com
  valor dentro dela mas não permitido.
- Recusa retorna: depois de mandar o erro ao cliente, `return`. Mandar o erro e seguir executa a
  ação recusada mesmo assim.
- Checagens baratas antes do trabalho caro: nada de `getPlayerByName(..., true)`,
  `getPlayerByGUID(..., true)` ou `loadPlayerBy*` (personagem inteiro carregado no dispatcher)
  antes de dono, estado, tamanho e intervalo. Testar com `InMemoryPlayerRepository::playerLoads`.
- Nome do pacote acima de `NETWORKMESSAGE_PLAYERNAME_MAXLENGTH`: recusar antes de query ou log.
- Texto que vira caminho ou conteúdo de arquivo: só caracteres de nome
  (`PlayerInteractionService::isReportableName`), nunca `/`, `\`, `.` ou bytes de controle;
  limitar tamanho e taxa.
- Pedido que chega ao banco, a arquivo ou a broadcast: `RequestCooldown` por player
  (`src/utils/request_cooldown.hpp`) e métrica `*_rejected{reason}`, para o ataque aparecer no
  Prometheus.
- Antes da autenticação (primeira mensagem, login), nada pode tocar o estado de outro jogador:
  derrubar sessão, revelar versão de client, carregar personagem. Tudo que depende do personagem
  pedido vem depois do `gameWorldAuthentication`. Prova com sockets reais:
  `tools/smoke/login_takeover_check.py`.
- Log de recusa: uma linha por pacote, nunca uma por elemento de uma lista do pacote (some a
  contagem e logue uma vez). O `log_budget` corta o excesso dentro de um pacote, mas o que é
  cortado some do log.
- Uma lista do pacote (contagem + itens): recusar contagem maior que os bytes restantes e acima do
  teto do recurso antes de reservar ou percorrer.
- Laço sobre contagem do pacote: contador mais largo que a contagem e `<` (`for (uint32_t i = 0; i < count; ++i)`),
  nunca `uintN_t` com `<=` contra um valor do mesmo tamanho: com a contagem no máximo do tipo o contador volta a 0
  e o laço não termina (um pacote de 5 bytes no `0xE2` congelava o dispatcher). Conferir com
  `grep -rn -E 'for \((uint8_t|uint16_t) [a-zA-Z_]+ = [0-9]+; [a-zA-Z_]+ <= ' src` e com contador estreito contra `size()`.

**Lua (datapack):**
- Texto do cliente nunca é padrão Lua (segundo argumento de `string.match`/`find`/`gmatch`/`gsub`):
  usar `s:find(texto, 1, true)`. Um padrão forjado de ~20 bytes faz backtracking por segundos a
  cada chamada e congela o dispatcher; um malformado dá erro de script.
- Caminho de arquivo nunca montado com texto do cliente sem restringir a caracteres de nome
  (de preferência checado no C++ antes do evento).
- Nada de shell no Lua: `os.execute`/`io.popen` com texto montado executa o que vier no texto (aspas, `;`, `$( )`) e trava o
  loop com um processo por chamada. Diretório: `FS.mkdir`/`FS.mkdir_p` (binding nativo `fsCreateDirectories`). Validação:
  `luajit tests/lua/test_fs_no_shell.lua` e `grep -rn "os.execute\|io.popen" data*/` vazio.
- Moeda e quantidade do pacote: no mínimo 1 e dentro do limite antes de query ou mutação.
- Strings do pacote: limitar o tamanho antes de query, laço ou resposta.
- Nome digitado pelo jogador: `Game.getNormalizedPlayerName` (consulta leve); nunca
  `Game.getOfflinePlayer`, `Bank.transfer`/`Bank.transferToGuild` ou outra API que aceite nome
  offline antes das checagens baratas (saldo, quantidade, dono). Talkactions de jogador já têm 1 s
  de intervalo (`TalkActionCooldown`), mas um caminho caro por mensagem continua caro.
- Janela modal que entrega algo uma vez: conferir a condição (storage, saldo, item) de novo no
  callback da escolha. Cada envio da janela tem um id novo, e o cliente pode responder todas.
- Validação: `luajit -bl <arquivo>` e `luac5.4 -p <arquivo>`, mais um harness com stubs como
  `tools/lua/store_parsers_check.lua` (rodar também contra o arquivo do upstream: lá tem que falhar).
- Tabela Lua com chave numérica (`[id] = {...}`): chave repetida some sem aviso (o Lua guarda só a
  última). Vários itens com o mesmo aid entram numa chave só, com `itemId` por posição em `itemPos`
  (o `loadLuaMapAction` lê). Depois de editar ou de um merge que toque `data*/`, rodar
  `find data data-otservbr-global data-canary -name '*.lua' -not -path '*/monster/*' -print0 | xargs -0 python3 -I tools/datapack/duplicate_keys_check.py`
  (precisa dar 0).

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
| `IBanRepository` (`account/ban_repository.hpp`) | `BanRepositoryDB(IDatabase&)`: ban de conta (vencido vai para `account_ban_history`), ban de IP (vencido é apagado; `loadAddressBans` lê todos e devolve nada em erro, lista vazia em tabela vazia), namelock; o SQL do antigo `IOBan` (25.16, 25.21) | `account/in_memory_ban_repository.hpp` |
| `IHouseRepository` (`map/house/house_repository.hpp`) | `HouseRepositoryDB(IDatabase&)`: `houses` (leitura na subida, gravação no save do servidor), `house_lists`, dono e novo dono, identidade do dono; e os blobs de itens das casas (`tile_store`, `loadTileItems`/`saveTileItems`); o SQL que estava no `House` e no `IOMapSerialize` (25.17/25.18) | `map/in_memory_house_repository.hpp` |
| `IPlayerProgressRepository` (`creatures/players/player_progress_repository.hpp`) | `PlayerProgressRepositoryDB(IDatabase&)`: histórico do forge, blob de slots do wheel, líderes de highscore dos títulos (coluna de skill só como identificador simples), contagem de hirelings, mortes recentes e kills PvP da Cyclopedia (assíncronas, `done` no dispatcher), nível/vocação dos personagens de uma conta (badges); o SQL que estava nos components (25.19) | `creatures/in_memory_player_progress_repository.hpp` |
| `IServerConfigRepository` (`game/server_config_repository.hpp`) | `ServerConfigRepositoryDB(IDatabase&)`: chave/valor de `server_config` (`motd_num`, `motd_hash`, `players_record`, valores escapados) e a linha do `boosted_creature`; o SQL que estava no `game.cpp` (25.20) | `game/in_memory_server_config_repository.hpp` |
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
- Log em teste: `InjectionFixture::logger()` **não** é o `InMemoryLogger` que o `g_logger()` usa (instâncias
  diferentes). Para afirmar sobre o log, use `dynamic_cast<InMemoryLogger &>(g_logger())` e prove com um
  controle (um teste que gera o log e o vê).
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
- Todo preset grava o executável no mesmo `./canary` da raiz: o último build (inclusive o de testes, que
  também compila o alvo `canary`) é o que sobe. Antes de uma checagem ao vivo, rebuild do
  `linux-release-metrics` logo antes e confira a porta 9464 respondendo; um "antes" ao vivo já saiu do
  binário de testes com a correção dentro e pareceu provar que o código antigo recusava.
- Integração: MySQL no container docker `mysql` (imagem `mysql:8`). Credenciais em
  `tests/test.env` (`TESTS_ENV_DEFAULT`) ou `TEST_ENV_FILE`. Cada teste roda dentro de
  `BEGIN`/`ROLLBACK` (`databaseTest`). Caminhos async (`g_databaseTasks()`) ficam fora dessa
  transação; não teste por integração.
- Duas lições de validação:
  - um binário gtest que cai não imprime resumo, então confira o **exit code** (o grep de
    "PASSED" sozinho engana);
  - rode o `tools/merge/syntax_check.sh` **depois** do build, porque com o PCH desatualizado
    ele acusa erros falsos.
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

**Datapack de monstros (`data-otservbr-global/monster/`, Phase 24):** as correções do fork vivem
no manifesto `tools/datapack/fixes.json`, não à mão nos arquivos. Em conflito num arquivo de
monstro: pegue a versão do upstream (`git checkout --theirs -- <arquivo>`) e rode
`python3 -I tools/datapack/wiki_check.py apply`, que reaplica só o que ainda falta. Cada fix guarda o
valor que substitui (`from`): o `apply` só troca um valor enquanto o arquivo ainda tem esse `from`;
se o upstream mudou o valor para outra coisa, ele **reporta** `changed upstream (...)`, sai com código
1 e não sobrescreve (decida à mão e ajuste o manifesto). Aumento de `maxCount` não desfaz um valor
maior do upstream; redução (`maxCount` abaixo do `from`) é exata e só com entrada única; item que o
upstream já adicionou fica como está (também com outra grafia ou outro id: o `apply` reporta
`listed as another entry`); `remove_loot` só apaga as entradas de uma linha iguais ao `from` (sumida
= `already`; entrada em várias linhas = conflito); rodar duas vezes não muda nada. Depois, smoke boot. Para provar
o lado do upstream: `git archive <base> data-otservbr-global/monster` numa pasta temporária,
`wiki_check.py apply --root <pasta>` e `diff -rq` contra a branch (em 2026-11-13, com base
`997af9180`: 0 conflitos; 4 arquivos só com linhas de loot em outra ordem).

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
| `game.cpp` `Game::loadItemsPrice` | `Game::loadItemsPrice` (encadeia `loadStatistics` → `setStatistics` → `loadBestOfferPrices`, fora do loop do jogo; o banco devolve só o melhor preço por item e tier) e `Game::rebuildItemsPrice` (laços de média e máximo); SQL em `MarketRepositoryDB` | manual: corpo dividido (README); `game.hpp` ganhou `rebuildItemsPrice` e o include de `io/market_repository.hpp` |
| `src/canary_server.cpp`: parsing de `worldType` | `src/game/world_type_config.cpp` (`world_type_config::parse`) | se o upstream mudar nomes de world type, atualizar o parse e `world_type_config_test.cpp`; manter `"open-pvp"` |
| `src/creatures/players/components/pvp/expert_pvp.cpp` | mesmo arquivo, com `g_game()`/config → `world()` (`ExpertPvpWorld`) | merge normal; manter `world().` nas linhas tocadas |

**Correções e trocas da branch dentro de arquivos do upstream** (o merge **não** pode
desfazer; se o upstream mexer nessas linhas, reaplique):

| Arquivo upstream | O que a branch mudou | Por quê |
|---|---|---|
| `src/io/iologindata.cpp` `gameWorldAuthentication` | personagem marcado para deleção é recusado (busca sem diferenciar maiúsculas na lista da conta); IP no log | o `players[name] != 0` original nunca recusava, porque a lista da conta já exclui deletados (teste `CharacterMarkedForDeletionIsRefused`) |
| `src/database/database.cpp` `Database::getInstance` | `inject<Database>()` → `DI::port<Database>()` | com um container de teste ativo, `inject<>` criava um `Database` separado; os adapters (que recebem o `IDatabase` do container de produção) falavam com outra conexão, nunca conectada, e o `escapeBlob` sem handle derrubava o processo. Agora `g_database()` e o `IDatabase` injetado são o mesmo objeto também nos testes |
| `src/database/databasetasks.cpp` `DatabaseTasks::getInstance` | `inject<DatabaseTasks>()` → `DI::port<DatabaseTasks>()` | mesmo caso do `Database`: com container de teste, as tarefas assíncronas rodariam num `Database` separado e não conectado (teste `DatabaseAndTasksIgnoreAnUnrelatedTestContainer`) |
| `game.cpp` `Game::playerUpdateHouseWindow` (hoje em `player_interaction_service.cpp`) | antes do parse: texto acima de 8 KB recusado; 1 edição/s por player (`RequestCooldown`); métrica `house_list_updates_rejected{reason}`; a janela de edição é consumida mesmo na recusa | o parse roda um regex sobre o texto inteiro (até ~64 KB) e pode fazer até 100 `SELECT` síncronos por envio |
| `game.cpp` `Game::playerReportBug` (hoje em `player_interaction_service.cpp`) | 1 relatório por minuto por player; mensagem truncada em 2048 caracteres; métricas `bug_reports_rejected`/`bug_reports_truncated` | o datapack (`Player:onReportBug`) grava cada relatório num arquivo por player; sem limite, um cliente enchia o disco (~64 KB por pacote) |
| `game.cpp` `Game::playerReportRuleViolationReport` (hoje em `player_interaction_service.cpp`; o `Game` só delega) | nome do alvo só com 1–30 letras, espaços, `'` ou `-` (`PlayerInteractionService::isReportableName`); 1 report a cada 10 s por player; comentário e tradução truncados em 2048; aviso e métrica `rule_violation_reports_rejected{reason}` (`invalid_target`, `cooldown`) | o datapack (`Player:onReportRuleViolation`) grava em `reports/players/<reporter>-<alvo>-<tipo>.txt` com o alvo do pacote: cada alvo novo criava um arquivo de até ~64 KB sem limite de taxa, e com `/` e `..` o caminho saía da pasta (o `hasPendingReport` cria `reports/players/<nome>/` para todo reporter; um reporter `Bob` com alvo `x/../../..` passa pela pasta de um `Bob-x`) e anexava texto do atacante a um arquivo arbitrário terminado em `-<tipo>.txt` (teste `rule_violation_report_test.cpp`) |
| `data/libs/gamestore/parsers.lua` (`fuzzySearchOffer`, `parseTransferableCoins`) | busca do store literal (`find(texto, 1, true)`), vazia ou acima de 64 caracteres não varre; transferência de coins recusa `amount < 1` e destinatário vazio ou acima de 30 caracteres antes de qualquer query | **DoS remoto**: a busca usava o texto do cliente como padrão Lua em `string.match`; `".-"` ×10 + `"#"` (21 bytes) custa ~27 s por nome de oferta e a busca percorre todas as ofertas, congelando o dispatcher; padrão malformado dava erro de script. Transferência de 0 coins não custava nada e fazia SELECT, UPDATE, 2 INSERTs e reabria a loja (~4/s por player). Verificado com harness Lua de stubs (o upstream estoura o timeout) |
| `data/libs/gamestore/store.lua` (`canChangeToName`) e `data/libs/gamestore/purchases.lua` (`processNameChangePurchase`) | nome novo só com letras e espaço (como o de hireling); formato validado antes de `Game.getNormalizedPlayerName(nome, true)` | a regra era uma lista de caracteres proibidos: bytes de controle e letras não ASCII passavam (homóglifo imitando staff, e um nome que o report de violação não aceita); o lookup que pode carregar personagem offline rodava antes da validação. Harness `tools/lua/store_name_check.lua` (o upstream falha 4 checagens) |
| `src/creatures/players/components/player_vip.cpp` (`editGroup`, `removeGroup`, `addGroup`, `edit`) | `editGroup` ignora id inexistente; editar e remover recusam grupos não customizáveis; `addGroup` respeita `getMaxGroupEntries()`; nome de grupo e descrição truncados em 128 (`PlayerVIP::MaxTextLength`, tamanho das colunas) | **crash remoto**: `editGroup` com id forjado derreferenciava nulo e derrubava o servidor; o pacote também renomeava/apagava os grupos padrão, criava grupos sem premium e mandava até ~64 KB ao banco |
| `src/creatures/players/components/wheel/player_wheel.cpp` (`improveGemGrade`, `getGemGrade`) | `improveGemGrade` recusa, antes de tocar as tabelas, a posição que não é slot de modificador do tipo de fragmento (e, no Greater, da vocação): `PlayerWheel::isGradablePosition`; aviso no log e métrica `wheel_requests_rejected{reason="invalid_grade_position"}`; `getGemGrade` devolve 0 fora da tabela | **corrupção de memória remota**: o byte de posição do pacote (0–255) indexava `m_basicGrades[49]`/`m_supremeGrades[95]` sem checagem; lia e, com fragmentos e gold, escrevia até ~200 bytes além da tabela (teste `wheel_grade_position_test.cpp`) |
| `src/io/iobestiary.cpp` `sendBuyCharmRune` (ação 1) e `src/creatures/players/player.cpp` `getCharmTier`/`setCharmTier` | atribuir criatura exige o charm desbloqueado (`hasCharmUnlockedRuneBit`, o mesmo teste da janela) e raça do bestiário; aviso e métrica `charm_requests_rejected{reason}` (`locked_charm`, `unknown_race`); `getCharmTier` usa `>=` (era `>`, um além da tabela) e `setCharmTier` ignora id fora da tabela | pacote forjado colocava charm **não comprado** numa criatura; o Gut (loot) não olha tier e pagava de graça. Raça fora do bestiário também passava: boss tem `raceid = bossRaceId` e o combate casava o charm com o boss (teste `charm_rune_test.cpp`) |
| `src/io/ioprey.cpp` `parsePreyAction` (`PreyAction_ListAll_Selection`) | raça fora do bestiário (`getMonsterTypeByRaceId` nulo) é recusada; aviso e métrica `prey_requests_rejected{reason="unknown_race"}` | a checagem original era `mtype && !isPreyable`: raça desconhecida passava. Boss recebe `info.raceid = bossRaceId` (fora do bestiário), então um pacote forjado ativava prey (dano, defesa, XP, loot) contra qualquer boss (teste `prey_selection_test.cpp`) |
| `game.cpp` `Game::playerCyclopediaHouseBid` (hoje em `src/game/cyclopedia/cyclopedia_service.cpp`) | antes de tudo, lance só em casa sem dono e em estado `Available` (aviso e métrica `house_auction_requests_rejected{reason="not_on_auction"}`); recusa do `canBidHouse` agora retorna | o upstream mandava a recusa do `canBidHouse` ao cliente e seguia com o lance (rookgaard e free account davam lance). Também aceitava lance em casa com dono: numa transferência pendente o herdeiro fica nos campos de bidder; o lance o substituía, o reembolso creditava ao herdeiro dinheiro que ele nunca pagou e o restart entregava a casa ao autor do lance (teste `house_auction_bid_test.cpp`). A metade "estado `Available`" fica de propósito: nada volta o estado em memória para `Available` em runtime (casa despejada segue `Rented` até o restart, e é isso que a lista do cliente mostra e o save grava), então um lance nela ficaria preso. Regras do leilão (commit seguinte): lance entre 1 e `MaxHouseBid` (máximo de `int(11)`, tipo das colunas); "um lance por conta" derivado das casas em memória (`CyclopediaService::bidsOnAnotherHouse`: casa `Available` cujo bidder é um personagem da conta), aplicado no lance, na transferência (`OnlyOneBid`) e no aceite (`AlreadyBid`), porque `accounts.house_bid_id` nunca é gravado; o líder só sobe o próprio lance; lance perdedor só conta se superar o `highestBid` (antes um lance menor **baixava** o preço: o alt do líder dava 1 gold e o líder levava quase de graça); a transferência grava o nome real do destinatário, não a grafia do pacote. Métrica `house_auction_requests_rejected{reason}` (`bid_out_of_range`, `only_one_bid`, `bid_not_raised`, `bid_below_price`). Testes `HouseAuctionRulesTest` com bidders válidos (vocação, premium, conta) cobrem também os fluxos legítimos |
| `game.cpp` `Game::playerCyclopediaHouseTransfer` (hoje em `src/game/cyclopedia/cyclopedia_service.cpp`) | 1 pedido de transferência por segundo por player (`RequestCooldown`, `HouseTransferCooldownMs`); nome do destinatário acima de 30 caracteres recusado com aviso; casa `Rented` **e** dono = quem pede checados **antes** de resolver o nome; métrica `house_auction_requests_rejected{reason}` (`transfer_throttled`, `recipient_name_too_long`) | o upstream resolvia o destinatário com `getPlayerByName(nome, true)` antes de qualquer checagem: qualquer player fazia o servidor carregar um personagem offline inteiro do banco por pacote (o `canTransferHouse` carrega de novo por GUID), e nome inexistente gerava um `error` no log com texto do atacante (teste `house_auction_transfer_test.cpp`) |
| `src/game/chat/chat_service.cpp` `playerOpenPrivateChannel` | destinatário com mais de `NETWORKMESSAGE_PLAYERNAME_MAXLENGTH` (30) é recusado antes do `formatPlayerName` | evitava um `SELECT` com até ~64 KB escapados por pacote |
| `src/account/account_repository.cpp` | `inject<AccountRepository>()` → `DI::port<AccountRepository>()` | testes com container próprio caíam (nullptr) em tudo que passa por `Account` |
| `src/creatures/players/components/player_vip.cpp` | `IOLoginData::*VIP*` → `g_vipRepository()`; guarda de `account` nulo em `edit` | porta VIP (19.3); null-deref |
| `src/creatures/players/components/pvp/expert_pvp.cpp` | `g_game()`/config → `world()` | porta `ExpertPvpWorld` (18.4) |
| `src/server/network/connection/connection.cpp` | preâmbulo "world name" que não bate com `serverName` vira aviso + fechamento | diagnóstico de login (`b6fe41f24`) |
| `src/server/network/connection/connection.cpp` `Connection::send` e `internalSend` | toda mudança na fila passa por `enqueueOutput`/`popOutputFront`/`clearOutput`/`clearOutputExceptFront` (contador global exato, liberado também no destrutor); orçamento global `maxOutputQueueMessagesTotal` (padrão 16384 ≈ 1 GB): no teto, só cai quem já tem `OUTPUT_QUEUE_FAIR_SHARE` (64) mensagens; teto da fila de saída (`maxOutputQueueMessages`, padrão 2048, `0` desativa): ao estourar, descarta a fila **menos a mensagem da frente**, desconecta, loga com IP e conta `connection_output_queue_overflow_total`; o handler do `async_write` captura a própria `OutputMessage` | ataque de leitor lento: a fila não tinha teto e o timeout de escrita é por escrita (cada mensagem segura ~64 KB) |
| `src/config/config_enums.hpp`, `src/config/configmanager.cpp`, `config.lua.dist` | chaves `MAX_OUTPUT_QUEUE_MESSAGES` / `maxOutputQueueMessages` (padrão 2048), `MAX_OUTPUT_QUEUE_MESSAGES_TOTAL` / `maxOutputQueueMessagesTotal` (padrão 16384) e `MAX_PENDING_CONNECTIONS_PER_IP` / `maxPendingConnectionsPerIp` (padrão 10, só conexões ainda não logadas), ao lado de `maxPacketsPerSecond` | limites escolhidos pelo usuário |
| `src/server/server.cpp` `ServicePort::onAccept` e `ConnectionManager` (`connection.hpp/.cpp`) | `ConnectionManager::admitFromIp` depois do `Ban::acceptConnection`; a vaga é liberada no `releaseConnection` e no login (`ProtocolGame::login`/`connect` chamam `markLoggedIn`); contador em `src/server/network/connection/ip_connection_limiter.cpp`; métrica `connections_refused_total{reason="per_ip_pending_limit"}`, aviso limitado por taxa | só a taxa de conexão era limitada; um bot acumulava sockets sem logar. Conta só quem não logou por causa do CGNAT (muitos jogadores reais num IP), decisão do usuário |
| `src/items/bed.cpp` `BedItem::canUse`; `src/map/house/house.cpp/.hpp` | o nível de acesso do dorminhoco vem de `g_playerRepository().loadHouseAccessFacts(guid)` (uma consulta) e dos overloads novos `House::getHouseAccessLevel(const HouseAccessFacts &)` / `AccessList::isInList(guid, guildRankId)`, que espelham as versões com `Player` (mantenha as duas em sincronia) | usar uma cama ocupada carregava o dorminhoco inteiro (~20 queries + itens) no loop do jogo a cada uso (~5/s por player). Caminho caro achado na auditoria; **não confirmado** que seja o bug real de cama relatado: o usuário também não conhece o vetor (2026-11-12), então não há como confirmar |
| `src/items/containers/mailbox/mailbox.cpp/.hpp` (`sendItem`) | destinatário resolvido por `Mailbox::resolveReceiver`: nome vazio ou acima de 30 recusado; online primeiro; offline só depois de `getGuidByName` (um `SELECT id`) achar o nome, e aí o load completo de antes. Sem limite de taxa (escolha do usuário) | a etiqueta é texto do jogador: nome que não existe fazia load completo e logava `error` com o texto a cada item empurrado na caixa (~5/s por player). Entrega a offline existente continua com load + save síncrono (mesma classe da cama; limite por remetente recusado pelo usuário) (teste `mailbox_receiver_test.cpp`) |
| `src/canary_server.cpp` `initializeDatabase` | `schema_hardening::ensureIndexes(...)` logo depois de `DatabaseManager::updateDatabase()` | índices da branch sem migration numerada: `src/database/schema_hardening.cpp` só adiciona índices `solid_*` que faltam (sem conflito com as migrations do upstream, sem editar `schema.sql`, nunca remove). Primeiro: `solid_market_history_stats` |
| `game.cpp` `Game::playerRequestOutfit` (hoje em `outfit_service.cpp`) e `OutfitService::playerChangeOutfit(uint32_t, ...)` | intervalo por player: janela de outfit 1 vez/s, troca 1 vez a cada 500 ms (`RequestCooldown`, `src/utils/request_cooldown.hpp`); métrica `outfit_requests_throttled{request}`. O overload com `Player` (chamadas internas) não é limitado | `shine`/`ddos` do bot: `0xD2` monta a janela inteira e `0xD3` é transmitido a todos os espectadores |
| `src/server/network/protocol/protocolgame.cpp` `ProtocolGame::parsePacket` | `const packet_watch::Scope packetWatch(recvbyte, m_isLivestreamViewer ? nullptr : player, getIP());` logo após ler o opcode (+ include); o espectador de livestream é medido como ninguém (o `player` dele é o caster) | latência por opcode e aviso de pacote lento com player/IP (`src/server/network/protocol/packet_watch.cpp`) |
| `game.cpp` `cacheQueryHighscore` (hoje em `highscore_repository_db.cpp`) e `processHighscoreResults` (hoje em `highscore_service.cpp`) | `highscore_cache::prune` antes de inserir (expiradas saem; no teto, o cache é limpo) + métrica `highscore_cache_evictions` | as chaves vêm do cliente; no upstream os caches crescem sem fim |
| `game.cpp` `playerHighscores` (hoje em `highscore_service.cpp`) | página 0 em `HIGHSCORE_GETENTRIES` é recusada antes da query; página fresca no cache de resultado sai sem query; intervalo de 1s por player; teto de 8 queries em andamento no servidor (contador liberado no topo de `processHighscoreResults`, mesmo se o player saiu); métricas `highscore_requests_total{type}`, `highscore_served_from_cache`, `highscore_requests_rejected{reason}` | `(page - 1)` dava underflow e rodava o ranking inteiro; toda query de highscore ocupa a única conexão do `Database` (com mutex) e atrasa saves, e bots com vários personagens podiam saturá-la |
| `src/server/network/protocol/protocolgame.cpp` `ProtocolGame::onRecvFirstMessage` | o bloco da sessão existente (`getPlayerByName`/`getDeadPlayer` → "already logged in", versão de client diferente, `foundPlayer->client->disconnectClient(...)`) foi movido **inteiro** para depois do `gameWorldAuthentication`, logo antes do `dispatchProtocolTask(login)` | **derrubar qualquer jogador sem a senha** (igual no upstream desde `c07696b2c`): o bloco rodava antes do challenge, do ban de IP e da senha, então o nome do personagem e um bloco RSA (chave pública) bastavam para derrubar o client da vítima em loop (o personagem fica no mundo; em open-pvp morre sem curar) e ler a versão do client dela. Reproduzido e provado com sockets reais (`tools/smoke/login_takeover_check.py`): antes, uma conexão com conta inexistente derrubava a vítima; depois, a vítima fica e o relogin do dono continua substituindo a conexão antiga |
| `src/lua/functions/core/game/game_functions.cpp` `luaGameGetNormalizedPlayerName` | nome vazio ou acima de 30 → `nil`; online pelo mapa de nomes; offline por `g_playerRepository().formatPlayerName` (um `SELECT name`); `isNewName` ignorado (só silenciava o log) | usava `getPlayerByName(nome, true)`: personagem offline carregado inteiro a cada `!transfer`/NPC do banco, e nome inexistente logava `error` com o texto digitado (teste `PlayerNameLookupFunctionsTest`) |
| `src/lua/functions/core/game/bank_functions.cpp` `luaBankTransfer`, `luaBankTransferToGuild` | `amount == 0` ou saldo insuficiente da origem → `false` **antes** de resolver o destino | o destino por nome passa por `Lua::getPlayer(L, arg, true)`/`getGuild(..., true)` e carregava o personagem offline mesmo quando a transferência não podia acontecer (`!transfer Nome,1` sem dinheiro = 2 loads por mensagem) |
| `data/libs/functions/fs.lua`, `src/lua/functions/core/game/global_functions.cpp/.hpp` (`fsCreateDirectories`) (PR upstream #4102, aberta) | porta da PR: `FS.mkdir`/`FS.mkdir_p` chamam o binding nativo, sem shell; o binding só chama `directory_creation::createDirectories` (`src/utils/directory_creation.cpp`, da branch), que também captura a exceção da conversão do caminho (ponto aberto da revisão do upstream). `tests/lua/test_fs.lua` igual ao da PR; `test_fs_no_shell.lua` é da branch | `FS.mkdir` rodava `mkdir "<caminho>"` num shell (aspas, crase ou `$( )` no caminho executavam comando) e o erro nunca era detectado (`os.execute` devolve número no LuaJIT). Quando a PR entrar no upstream: aceitar o `fs.lua` deles e manter nosso corpo do binding |
| `src/lua/creature/talkaction.cpp` `TalkActions::checkWord` | antes do `executeSay`: `TalkActionCooldown::global().admit(*player, OTSYS_TIME())` (`src/lua/creature/talkaction_cooldown.cpp`): 1 talkaction por segundo por player, staff (`isAccessPlayer`) sem limite; recusa consome a palavra, manda "exhausted" e conta `talkaction_requests_throttled` (valor escolhido pelo usuário) | talkactions rodam antes do mute e não tinham intervalo: um client rodava uma por pacote (`!transfer`, `!livestream` grava no banco, `!online` percorre todos) |
| `src/lib/logging/log_with_spd_log.cpp` `info`/`warn`/`error`/`critical` | `if (!log_budget::admit()) return;` (+ include); orçamento em `src/lib/logging/log_budget.cpp`, aberto pelo `packet_watch::Scope` (membro `logBudget`, fechado antes do aviso de pacote lento): 3 linhas por pacote e 20 por player por minuto, staff sem limite (valores escolhidos pelo usuário); o que é cortado conta em `packet_log_lines_suppressed{opcode}` e cada player cortado é nomeado (IP, opcodes, contagem) num aviso no máximo 1 vez por minuto, para que encher o orçamento com ruído não esconda o autor (achado da revisão de segurança); o estado por player é podado quando fica uma janela quieto (mesmo com linhas pendentes) e a poda só roda de novo quando o mapa dobra | o log é síncrono no dispatcher; `0x03` da proficiência gerava até 255 `error` por pacote, e nome inexistente em várias rotas gerava `error` com texto do atacante: disco cheio e loop do jogo lento. Linhas fora de um pacote (tarefas agendadas, boot) não são limitadas |
| `data/scripts/talkactions/player/reward.lua` | o storage `PlayerWeaponReward` é conferido de novo no callback da escolha | cada `!reward` abria outra janela com id novo e responder todas entregava uma arma de exercício (64400 cargas) por janela (`toggleReceiveReward`, desligado por padrão). Harness `tools/lua/reward_check.lua` (o upstream entrega 2) |
| `protocolgame.cpp` `parseQuickLootBlackWhitelist`; `LootService::playerSetManagedContainer`/`Clear`/`Open`/`playerQuickLootBlackWhitelist` (`loot_service.cpp`, já da branch) | contagem da lista maior que os bytes restantes é recusada no parse; o service recusa categoria fora do enum (`isManagedContainerCategory`) e lista acima de 1000 ou filtro desconhecido (`normalizeQuickLootList`, que também ordena e tira duplicados); métrica `quickloot_requests_rejected{reason}` e aviso com o player (limitado pelo `log_budget`) | categoria do pacote virava `1 << category` (UB a partir de 32) e chave do mapa; a lista aceitava até 65535 entradas (leitura além do fim devolve 0) e é percorrida a cada item saqueado |
| `game.cpp` `Game::playerSetTyping` | corpo movido para `ChatService::playerSetTyping` (`chat_service.cpp`, mesmo nome: o `moved_merge.py` casa); só transmite mudança de estado, "começou a digitar" no máximo 1 vez/s, "parou" sempre passa depois de "começou", e um "digitando" parado há 60 s expira (relog com o mesmo id) (`src/game/chat/typing_throttle.hpp`); métrica `typing_updates_dropped` (valor escolhido pelo usuário) | `0x38` era transmitido a todos os espectadores de todos os andares sem intervalo (mesma classe do `0xD3`) |
| `src/creatures/players/livestream/livestream.cpp/.hpp` `LivestreamManager::changeViewerName` | `viewerRenameRefusal` (estático): 1 rename a cada 10 s por espectador (`LivestreamViewerInfo::lastRenameAt`) e nome igual ao do caster ou de player online recusado (valor escolhido pelo usuário) | o `/name` não tinha intervalo e cada rename é anunciado à sessão inteira; o espectador podia assumir o nome do caster ou de outro player no canal |
| `src/kv/kv.hpp/.cpp` (`KVStore::get`, `setLocked`, `flush`) | conjunto limitado `absent_` (até `MAX_ABSENT_KEYS` = 200.000, limpo inteiro ao encher) com as chaves que o `load` não achou: um `get` repetido de chave ausente não vai mais ao banco; `set`/`remove` tiram a chave do conjunto, `flush` o limpa e `forceLoad` consulta de novo. Nunca é salvo. Uma ausência só é lembrada se nenhum lote de saves (despejo ou `flush`) começou ou terminou durante a leitura (`writeGeneration_` avança nos dois momentos) e nenhum está em andamento (`savesInFlight_`): a chave sai da memória antes de chegar ao banco, e lembrar essa falta esconderia o valor até o próximo `flush` (revisão de segurança) | o swarm da Phase 25 mediu ~200 `SELECT` síncronos por segundo no `kv_store` com 100 jogadores: cooldown de todos os bosses e streak do daily reward a cada login, `last-mount` a cada janela de outfit, 5 taints do Soul War a cada exp ganha. A única escrita direta no `kv_store` fora do KV é um `DELETE` no boot (compatível) |
| `src/creatures/players/components/player_title.cpp` `PlayerTitle::checkHighscore` | o líder da categoria vem de `TitleLeaderCache::global().leader(...)` (`title_leader_cache.hpp`, do fork): uma consulta por categoria a cada 60 s | rodava uma consulta de ranking sem índice (`ORDER BY` em todos os players) por título de highscore a cada login (~10 por login no swarm) |
| `src/lib/metrics/metrics.cpp` `Metrics::initHistograms` | o `MeterSelector` da view usa `meterName`/`otelVersion`/schema vazio, como o `getMeter()`; a lista de faixas perdeu o `infinity()` explícito | a view procurava o meter `"performance"` com o schema URL; os histogramas nascem no meter `"stats"`, então as faixas finas nunca valiam e o exportador publicava só os buckets padrão (nada acima de 10 ms se distinguia). Com a view valendo, o `infinity()` explícito gerava um segundo `le="+Inf"` por série (série duplicada, que o Prometheus recusa) |
| `src/lua/functions/core/game/game_functions.cpp` `luaGameGetNormalizedPlayerName` | lê o 2º argumento com `Lua::getBoolean` e o ignora | o gerador de `docs/lua-api` infere o tipo pelo que a função lê; sem a leitura, documentava `isNewName` como `string` |
| `src/io/iologindata.cpp` | `#include "account/account_repository.hpp"` | usa `g_accountRepository()` e recebia a declaração de outro arquivo do bloco do unity build; um `.cpp` novo do merge da Phase 26 deslocou os blocos e o preset de métricas parou de compilar |
| `src/server/network/protocol/protocolgame.cpp` (`ProtocolGame::login`, `onRecvFirstMessage`), `protocollogin.cpp`, `creatures/players/management/ban.cpp/.hpp` | as regras de admissão do `login` (namelock, servidor fechando/fechado, só premium, personagens por conta, ban de conta e, depois do load, personagens fora de PZ) viraram `g_game().loginService().refuseBeforeLoad(...)`/`refuseOutsideProtectionZone(...)` (`src/game/login/login_service.cpp`); o ban de IP dos dois protocolos, `refuseBannedAddress(ip)`; o `IOBan` e o `BanInfo` saíram do `ban.cpp/.hpp` (o `Ban`, throttle de conexão, fica) | regra de negócio fora do adaptador de rede, testada sem cliente (`LoginServiceTest`) e SQL de ban num adapter (`BanRepositoryDBTest`), 25.16. Se o upstream mudar essas checagens no `login`, aplicar no `LoginService`; as mensagens são as mesmas |
| `src/map/house/house.cpp` (`setOwner`, `setNewOwnerGuid`), `src/io/iomapserialize.cpp/.hpp` (`loadHouseInfo`, `saveHouseInfo`, `loadHouseItems`, `saveHouseItems`; `SaveHouseInfoGuard` e `SaveHouseItemsGuard` removidos) | o SQL virou `g_houseRepository()` (`storeOwner`, `findOwnerIdentity`, `storeNewOwner`, `loadHouses`, `loadAccessLists`, `saveHouses`, `loadTileItems`, `saveTileItems`; o `IOMapSerialize` segue dono do formato do blob); as decisões da subida (transferência no restart, leilão vencido com devolução, transferência aceita/recusada, mudança) viraram `house_auction_rules::resolveAtStartup`, e o `loadHouseInfo` só aplica o resultado | regras que mexem com casas e dinheiro, antes sem teste (`HouseStartupRulesTest`, `HouseOwnerTest`, `HouseRepositoryDBTest`), 25.17. Se o upstream mudar esses ramos no `loadHouseInfo`, aplicar no `resolveAtStartup` |
| `creatures/players/components/` (`player_forge_history.cpp` `load/save`, `wheel/player_wheel.cpp` `loadDBPlayerSlotPointsOnLogin`/`saveDBPlayerSlotPointsOnLogout`, `player_title.cpp` `checkHighscore`, `player_cyclopedia.cpp` `loadSummaryData`/`loadDeathHistory`/`loadRecentKills`, `player_badge.cpp` `getPlayersInfoByAccount`) | o SQL virou `g_playerProgressRepository()`; os formatos de blob, o cache de líderes e as regras ficam nos components; a frase da morte e o status da kill viraram `PlayerCyclopedia::describeDeath`/`pvpKillStatus` (puras) | SQL fora do domínio e regra da Cyclopedia testada (`CyclopediaHistoryTest`, `ForgeHistorySaveTest`, `PlayerProgressRepositoryDBTest`), 25.19. Se o upstream mudar essas consultas nos components, aplicar no `PlayerProgressRepositoryDB` |
| `src/game/game.cpp` (`loadBoostedCreature`, `loadMotdNum`, `saveMotdNum`, `loadPlayersRecord`, `updatePlayersRecord`) | o SQL virou `g_serverConfigRepository()` (`find`/`insert`/`update`, `loadBoostedCreature`/`storeBoostedCreature`); a escolha do boosted e a regra do motd ficam no `Game` | último SQL do `game.cpp` fora dele (`ServerConfigTest`, `ServerConfigRepositoryDBTest`), 25.20 |
| `src/game/game.cpp` `Game::start` | `m_loginService->refreshAddressBans()` e um `cycleEvent` de `LoginService::AddressBanRefresh` (30 s) que chama `g_game().loginService().refreshAddressBans()` (sem capturar `this`) | o ban de IP das conexões pré-login lê um retrato em memória em vez do banco (25.21); ban novo ou retirado vale em até 30 s |
| `player.cpp` `canBidHouse`, `canTransferHouse`, `canAcceptTransferHouse` (hoje `src/map/house/house_auction_rules.cpp`, `house_auction_rules::canBid/canTransfer/canAcceptTransfer`) | saíram do `Player`; os chamadores (`cyclopedia_service.cpp`, `protocolgame_send_cyclopedia.cpp`) passam a casa e os personagens já resolvidos | regra de negócio fora da entidade e sem `g_game()` (25.12, teto do `player.cpp`); se o upstream mudar essas regras no `player.cpp`, aplicar em `house_auction_rules.cpp` |
| `player.cpp` `Player::onRemoveCreature` (hoje em `player_creature_event_component.cpp`) | `g_saveManager().savePlayer(player)` → `g_saveManager().saveLoggedOutPlayer(player)` | o save do logout levava ~10 ms do loop do jogo (máx. 300 ms no swarm); agora só a montagem do SQL fica no loop |
| `src/game/scheduling/save_manager.cpp/.hpp` | novo `SaveManager::saveLoggedOutPlayer`: grava as escritas de `IOLoginData::savePlayerGuard` com `query_recorder::Scope` (sob `Player::PlayerLock`), marca o personagem em `PlayerSaveBarrier` e commita numa conexão do pool (`DatabaseTasks::executeTransaction`, BEGIN…COMMIT, retry de deadlock); a barreira é liberada na thread do pool. Síncrono como antes com `toggleLogoutSaveAsync = false`, fora do estado normal do jogo ou sem pool. Métricas `player_logout_saves{result}` e `player_loads_waited_for_save` | gravar no loop e commitar fora evita ler o `Player` em outra thread (itens com decaimento seguem mudando no dispatcher depois do logout) |
| `src/io/iologindata.hpp` | `savePlayerGuard` foi para a seção pública | o `SaveManager` grava o save sem a transação local |
| `src/database/database.cpp` `Database::executeQuery` | no topo: `if (query_recorder::record(query)) return true;` (+ include) | com um `query_recorder::Scope` ativo na thread, as escritas são gravadas em vez de executadas (`src/database/query_recorder.cpp`, do fork); leituras seguem executando |
| `src/database/database.cpp/.hpp` (`beginTransaction`, `commit`, `rollback`, `retryQuery`, `storeQuery`) | campos `transactionOpen`/`transactionLost` e `loseTransaction()`: com transação aberta, `MYSQL_OPT_RECONNECT` desligado (`setAutoReconnect`), `sessionId` conferido depois de cada instrução (`stillInTransactionSession`) e erro de conexão não repetido (a transação fica perdida, o resto e o `commit` falham, o `rollback` só limpa); `isRecoverableError` público, `sessionId()`, `LockWaitTimeoutError`, `friend class DatabaseTasks` | o retry de 1 s reconectava dentro da transação e gravava o resto em autocommit (meio save, 25.9); se o upstream mexer no retry, manter o desvio de transação e rodar `DatabaseTransactionIT` |
| `src/creatures/players/player_repository_db.cpp` (`loadPlayerById`, `loadPlayerByName`, `increaseBankBalance`) e `src/lua/functions/core/libs/db_functions.cpp` (`db.query`, `db.asyncQuery`) | esperam `PlayerSaveBarrier` (`src/game/scheduling/player_save_barrier.cpp`, do fork) até o save ter `MaxWait` (1 s) de idade; se algum save pendente passou disso, todos respondem na hora até ele commitar: o load pelo GUID/nome (recusado se o save não commitou); o crédito de saldo (`increaseBankBalance`) não espera: com save pendente ele entra na fila do save (`deferUntilSaved`) e é commitado logo depois dele, antes de liberar o personagem (`SaveManager`, `finishLogoutSave`); o Lua, para escrita numa tabela de `PlayerSaveBarrier::SavedTables`, espera todos os saves pendentes e é recusado se algum travou (fail-closed). Se o upstream passar a salvar outra tabela no logout, incluir em `SavedTables` | um load antes do commit veria o personagem antigo (relog imediato, carta, mercado: duplicação/perda de itens); uma escrita direta na linha dele seria sobrescrita pelo save (aviso de um agente lateral; o save regrava `balance`, `isreward`, posição etc.). Prova: `tools/loadtest/relog_race_check.py` (20 ciclos de `!withdraw 10` + relog imediato: saldo e moedas exatos, 19 de 20 logins esperaram) |
| `src/server/network/protocol/protocolgame.cpp` `ProtocolGame::login` | antes do `loadPlayerById`: `PlayerSaveBarrier::global().waitForGuid(...)`; se o save ainda não commitou, `disconnectClient("Your character is still being saved. Please try again in a moment.")` | as esperas da barreira têm limite (`MaxWait`, 25.10): sem isso o relog cairia na mensagem genérica "contact an administrator" |
| `src/config/config_enums.hpp`, `configmanager.cpp`, `config.lua.dist` | `TOGGLE_LOGOUT_SAVE_ASYNC` / `toggleLogoutSaveAsync` (padrão `true`) | desliga o save assíncrono do logout sem recompilar |
| `src/database/databasetasks.cpp/.hpp` (`execute`, `store`; novos `startConnectionPool`, `shutdown`, `withConnection`) | as consultas assíncronas rodam numa conexão emprestada de um `ConnectionPool<Database>` (`src/database/connection_pool.hpp`, modelo do HikariCP: empresta e devolve por consulta, espera máxima de 5 s, conexão ociosa há 30 s é validada com `ping`, vida máxima de 30 min, conexão perdida reaberta no próximo empréstimo); sem pool ou com a espera esgotada, usam a conexão do jogo como antes. Métricas `database_task_connections_replaced{reason,opened}` e `database_task_connection_waits_exhausted` | dividiam a única conexão (com mutex) com o loop do jogo: enquanto uma consulta de fundo rodava (estatísticas do mercado ~100 ms, highscores, `db.asyncQuery`), toda consulta síncrona esperava. Só comandos independentes passam pelo pool (sem transação, sem `getLastInsertId`) |
| `src/database/database.cpp/.hpp` | `ping()`; o último erro do MySQL por thread (`lastErrorOnThisThread`, gravado em `retryQuery` e `storeQuery`); `DBTransaction::executeWithinTransaction*` rodam a transação mais uma vez quando ela foi vítima de deadlock (1213), por `retryOnceOnDeadlock`; métrica `database_transaction_deadlocks` | com conexões paralelas, uma transação (save de player, casas, kv) pode ser a vítima que o InnoDB desfaz; os callbacks regravam tudo a partir da memória, então rodar de novo é seguro. O lado síncrono **mantém** o retry do upstream (fica preso até o MySQL voltar): falhar rápido faria saves falharem com o banco fora e arriscaria perder itens |
| `src/canary_server.cpp` (`initializeDatabase`, `shutdown`) | `g_databaseTasks().startConnectionPool(databaseTaskConnections)` depois do `schema_hardening`; `g_databaseTasks().shutdown()` depois do `g_threadPool().shutdown()` | as conexões do pool fecham num ponto controlado, sem depender da ordem de destruição estática |
| `src/map/map.cpp/.hpp`, `src/game/game.cpp` `Game::shutdown`, `src/canary_server.cpp` `shutdown` | novo `Map::clearSpawns()` (spawns do mapa principal e dos 50 mapas custom); o `Game::shutdown` troca `map.spawnsMonster.clear(); map.spawnsNpc.clear();` por ele, e o `CanaryServer::shutdown` o chama de novo depois de `Spectators::clearCache()` | os spawns custom sobravam para o `~Game` no `exit`, depois que o singleton `Dispatcher` (construído depois do `Game`) já tinha morrido, e o `~SpawnNpc` cancelava o evento nele (crash no desligamento, 25.8). Se o upstream acrescentar outra lista de spawns ao `Map`, incluir em `clearSpawns` |
| `src/config/config_enums.hpp`, `configmanager.cpp`, `config.lua.dist` | `DATABASE_TASK_CONNECTIONS` / `databaseTaskConnections` (padrão 2; 0 mantém tudo na conexão do jogo) | escolha do agente com aval do usuário ("faça o que achar melhor") |
| `protocolgame.cpp` `parseExivaRestrictions`, `parseWeaponProficiency` (ação `0x01`); `game.hpp/.cpp` (delegates `admitExivaRestrictionsEdit`/`admitProficiencyListing`) | antes do trabalho: `g_game().admitExivaRestrictionsEdit(player)` (1 edição a cada 2 s) e `g_game().admitProficiencyListing(player)` (1 vez por segundo), em `PlayerInteractionService` (`RequestCooldown`); recusa manda "exhausted" e conta `client_requests_throttled{request}` (valores escolhidos pelo usuário) | exiva (só no-pvp): até 4 × `maxExivaWhitelist` `SELECT` síncronos por pacote; proficiência `0x01`: uma janela por arma treinada por pacote; nenhum dos dois tinha intervalo |
| `protocolgame.cpp` `parsePartyAnalyzerAction` | contagem de preços maior que os bytes restantes é recusada | o laço lia além do fim até 65535 vezes por pacote |
| `src/lua/functions/core/game/game_functions.cpp` `luaGameGetNormalizedGuildName`; porta `IGuildRepository::formatGuildName` (`guild_repository_db.cpp`, fake em `tests/fixture/io/in_memory_guild_repository.hpp`) | nome por uma consulta (`SELECT name ... LIMIT 1`), sem carregar a guild | os NPCs do banco passam o nome digitado: cada mensagem carregava a guild (nome, saldo, ranks) |
| `src/utils/tools.cpp` `formatDate`/`formatDateShort`/`formatDateTime` (issue upstream #4154) | porta idêntica do PR upstream #4179: formata `std::tm` local (`toLocalTime`), sem `time_point` | o fmt 12 imprimia o `time_point` em UTC e com nanossegundos (`22:13:20.000000000`); quando o #4179 entrar no upstream, o merge é limpo (teste `DateFormattingTest`) |
| `player.cpp` `addSkillAdvance`/`addManaSpent`/`addOfflineTrainingTries` (hoje em `player_experience_component.cpp`/`player_training_component.cpp`), `Player::sendSkillAdvance`, `ProtocolGame::sendSkillAdvance` (em `protocolgame_send.cpp`) (issue upstream #4114) | porta do PR upstream #4181: o aviso de avanço manda o id do evento `0x75/0x05` (`getSkillAdvanceEventId`: magia 1, espada 2, clava 3, machado 4, punho 5, distância 6, escudo 7, pesca 8) em vez do `getCipbiaSkill` | o client mostrava escudo como distância e espada como pesca. Quando o #4181 entrar no upstream, o `moved_merge.py` traz as 4 trocas de `player.cpp` para os components (já feitas: aceitar a nossa) (teste `SkillAdvanceGameEventTest`) |
| `game.cpp` `Game::playerBosstiarySlot`; `protocolgame.cpp` `parseSendBosstiarySlots` | as regras dos slots são `bosstiary_slot_rules::decide`/`isSlotUnlocked` (`src/game/bosstiary/bosstiary_slot_rules.cpp`, sem `Player`); o `Game` monta o `SlotState`, recusa com métrica `bosstiary_slot_requests_rejected{reason}` e só remove se o `removeMoney` passar; slot ocupado só remove (regra do Tibia, como no client), slot 1 abre com qualquer Prowess; a janela usa o mesmo `isSlotUnlocked` e `SecondSlotPoints`, a lista de escolha é `selectableBosses` (o mesmo conjunto que o `decide` aceita) e o bônus do slot é `slotLootBonus` (também no `luaPlayerGetBossBonus`, que o baú de recompensa usa); o preço da troca é `removalPrice` (regra do manual: a primeira do dia grátis, depois os valores do upstream) e toda remoção com `countsAsChange` sobe o contador (`Player::addRemoveTime` satura em 255); `storeBoostedBoss` volta o `removeTimes` de todos a 1 na transação do boss do dia | condição nova do upstream (slot, preço, boss permitido) vai para o `decide` com teste em `bosstiary_slot_rules_test.cpp`; o upstream aplicava o pacote como veio (qualquer boss, slot 2 sem pontos, remoção de graça sem dinheiro); o upstream nunca zerava o contador (decisão do usuário) |
| `protocolgame.cpp` `parseBestiarysendMonsterData` | o que revelar por nível de kills é `bestiary_disclosure::showsLootItem`/`showsStats`/`showsElementsAndLocations` (`src/game/bestiary/bestiary_disclosure.hpp`); a lista de loot para em 255 entradas (contagem de um byte) | regra nova do upstream vai para o header, com teste em `bestiary_disclosure_test.cpp`; a codificação segue no `ProtocolGame` |
| `src/lua/modules/modules.cpp` `Modules::executeOnRecvbyte`; `data/modules/modules.xml` (byte 95) | pacote dentro do `delay` do módulo é descartado e contado (`module_packets_throttled{opcode}`), retornando `true`; o taskboard (95) tem `delay="250"` | o upstream devolvia `false` e o pacote caía no switch do `ProtocolGame` (no 0xBA viraria `parseTaskHuntingAction`); manter o descarte e o `delay` |
| `player.cpp` `retrieveAllItemsFromDepotSearch`, `getDepotLocker` (hoje em `player_stash_component.cpp`); `game.cpp` `playerRequestDepotSearchRetrieve` (hoje em `loot_service.cpp`) | o retrieve usa o tier da busca aberta (`depotSearchOnItem.second`) para juntar e para recriar, e recusa item ou tier diferentes da busca (o service conta `depot_search_requests_rejected{reason="tier mismatch"}`); a quantidade a remover é calculada em 32 bits; `getDepotLocker` sem client cria o layout atual (com stash) em vez de derrubar o servidor | o upstream recriava os itens com o tier do pacote: 3 itens tier 0 viravam tier 5 (`depot_search_it.cpp`); e recriava **todos** os itens (cargas cheias, sem marca de loja, sem descrição). Na branch só a pilha simples (`isPlainStack`: serialização igual à de um item novo) vai pelo lote; o resto é movido como objeto (`internalMoveItem`) e item da loja vai para o Store Inbox com a marca (decisões do usuário 2026-11-19); manter |
| `protocolgame.cpp` `parseLeaderFinderWindow` (ação 2), `parseMemberFinderWindow` (ações 1 e 2) | as regras são `team_finder_rules::requestToJoin`/`cancelRequest`/`setMemberStatus` (`src/creatures/players/grouping/team_finder_rules.hpp`); o pedido vai para o anúncio do **líder** (`getTeamFinder(leader)`), o líder só é avisado de pedido novo, no máximo 1 vez a cada 2 s por solicitante (`JoinNoticeIntervalMs`, decisão do usuário), e o membro só de resposta nova a quem está no anúncio; recusas em `team_finder_requests_rejected{reason}` | o upstream mandava a mensagem a qualquer GUID online a cada pacote (ao vivo: 5 de 5 para um alvo fora do anúncio) e guardava o pedido no anúncio do próprio solicitante; regra nova vai para o header, com teste em `team_finder_rules_test.cpp` |
| `game.cpp` `playerSetMonsterPodium` (hoje em `outfit_service.cpp`); `player.cpp` `addItemFromStash` (hoje em `player_stash_component.cpp`) | o `PodiumMonsterRaceId` só é gravado depois de achar o monstro; item sem peso no stash é limitado pela quantidade | o upstream gravava o id antes de recusar e dividia por peso 0; manter |
| `player.cpp` `updateAimAtTargetSpells`; `protocolgame.cpp` `parseAimAtTarget` | o conjunto fica com no máximo `g_spells().getInstantSpells().size()` ids; contagem maior que os bytes do pacote é recusada | o upstream aceitava até 65.536 ids por jogador (255 por pacote, sem intervalo); manter |
| `src/database/database_port.hpp`, `database.hpp/.cpp` | `IDatabase::commitStatements(statements)`: uma transação pela API do `Database` (`DatabaseTasks::commitStatements`) | método novo da branch; upstream não tem |
| `player_vip.cpp` `PlayerVIP::edit`; `vip_repository*.hpp/.cpp` | `replaceVIPEntry` grava campos e grupos numa transação (`INSERT IGNORE` de várias linhas); cada grupo existente uma vez; a entrada sai dos grupos antigos também na memória | o upstream fazia UPDATE + DELETE + INSERT por grupo, cada um com commit, síncrono (10,7 ms por pacote com 255 ids); manter |
| `src/io/functions/iologindata_load_player.cpp` (grupos VIP); `creatures_definitions.hpp` `VIPGroupEntry::id` (`uint32_t`); `player_vip.*` (`VIPGroup::storedId`, `addGroupInternal`, `addGuidToStoredGroupInternal`, `addGroup`/`editGroup`/`removeGroup`) | grupos e vínculos carregados pelo id do banco (`getNumber<uint32_t>`); escritas pelo `storedId`; `addVIPGroupEntry` usa o `AUTO_INCREMENT` e devolve o id | o upstream lia `uint8_t`, descartava o id e escrevia com o 1..8 do client: VIP em grupo padrão nunca gravava; manter |
| `game.cpp` `playerRequestAddVip`/`playerRequestRemoveVip`/`playerRequestEditVip` (hoje em `player_interaction_service.cpp`); `protocolgame.cpp` `parseVipGroupActions` | `admitVipWrite`: 1 escrita de VIP por segundo por jogador, recusa com "exhausted" e `client_requests_throttled{request="vip"}` | o upstream gravava no banco a cada pacote, sem intervalo; manter (decisão do usuário) |
| `src/game/chat/chat_service.cpp` `playerChannelInvite`; `chat.hpp/.cpp` `PrivateChatChannel::invitePlayer(..., notifyInvited)`; `src/utils/request_cooldown.hpp` (chave `uint64_t`) | o convidado é avisado no máximo 1 vez a cada 2 s pelo mesmo dono (`InviteNoticeIntervalMs`, chave dono+convidado); o convite sempre acontece; aviso pulado em `client_requests_throttled{request="channel_invite_notice"}` | o upstream avisava a cada convite (convidar/excluir em ciclo mandava uma mensagem por ciclo a qualquer jogador online); manter (decisão do usuário) |
| `game.cpp` `Game::playerInviteToParty`; `party.hpp/.cpp` (`Party::isOnInviterScreen`) | o convidado tem de estar na tela do líder (alcance do client + 1) e visível (`canSeeCreature`); recusa em `party_invites_rejected{reason}` | o upstream aceitava o id de criatura de qualquer jogador online (convite/revogação em ciclo mandava mensagens a qualquer um); manter (decisão do usuário) |
| `game.cpp` `Game::playerInspectItem(player, pos)` | o item tem de estar à vista (`player->canSee`, como no `playerLookAt`); recusa em `inspection_requests_rejected{reason}` | o upstream inspecionava o item de cima de qualquer tile do mapa; manter |
| `protocolgame.cpp` `parseBestiarySendRaces`; `iobestiary.hpp/.cpp` (`IOBestiary::summarizeRaces`) | a lista de raças sai de uma passada só pelo bestiário; o `ProtocolGame` só codifica | o upstream percorria o bestiário duas vezes por raça (6,75 ms por pacote); mudança do upstream no laço vai para `summarizeRaces` |
| `protocolgame.cpp` `parseBestiarySendCreatures` (busca 1; o progresso de cada criatura vem de `IOBestiary::describeCreatures`); `parseSendBosstiary` | contagem de raças maior que os bytes do pacote é recusada e o contador é `uint32_t` com `<`; o mapa do bosstiary é lido por referência | o upstream tinha `for (uint16_t monsterCount = 1; monsterCount <= monsterAmount; ...)`: com 65535 o laço não termina e o dispatcher congela; manter |
| `src/io/io_bosstiary.cpp` `IOBosstiary::loadBoostedBoss` | o SQL foi para `IServerConfigRepository::loadBoostedBoss`/`storeBoostedBoss` (`ServerConfigRepositoryDB`, fake em `in_memory_server_config_repository.hpp`); a escolha é `IOBosstiary::chooseBoostedBoss` (pura); gravar o boss e soltá-lo dos slots dos jogadores é uma transação só | mudança do upstream na consulta vai para o adapter; na regra (data, sorteio entre archfoes), para `chooseBoostedBoss` com teste em `boosted_boss_choice_test.cpp` |
| `data/modules/scripts/daily_reward/daily_reward.lua` (`loadDailyReward`, `selectDailyReward`); `data/libs/functions/quests.lua` (`getQuestDataByMissionId`, `Game.addQuest`); `data/modules/scripts/hirelings/hireling_module.lua` `getOutfit`; `data/libs/systems/hireling.lua` (`lookfeet`); `data/libs/functions/game.lua` `setGlobalStorage` | o servidor lembra se a janela da recompensa diária abriu no shrine (`openedAtShrine`) e ignora o byte do client; índice `missionsById` (zerado no `addQuest`); outfit do hireling com cores até 132, addons até 3 e sem montaria; `lookfeet` vem de `lookFeet`; `setGlobalStorage` escapa o valor | o upstream confiava no byte "shrine" do client (recompensa sem token), percorria todas as quests por id de missão e aceitava outfit fora da paleta; reaplicar em conflito |
| `src/server/network/protocol/protocolgame.cpp` `disconnectClient` e a recusa de senha em `onRecvFirstMessage` (issue upstream #4153) | as duas montagens do `0x14` passam por `ProtocolGame::addLoginError` (declarado no `protocolgame.hpp`), que acrescenta o byte de motivo `0x00` quando `!oldProtocol && version >= 1523` | o client oficial 15.23+ lê esse byte; sem ele o pacote dá erro de parse e o client fecha em vez de mostrar "already logged in", ban ou senha errada. Se o upstream mexer nessas linhas, manter a chamada ao helper (teste `LoginErrorPacketTest`) |
| `src/server/network/protocol/protocolgame.cpp` `parseSendBuyCharmRune` (issue upstream #4125) | ação 3 (resetar todos os charms) despacha com `CHARM_NONE` e raça 0 sem ler mais nada | o client oficial manda só o byte da ação no reset; ler charmId e raceId gerava `[error] Not enough data` a cada reset (teste `CharmRuneParseTest`, com controle negativo) |
| `game.cpp` `Game::internalPlaceCreature`/`Game::placeCreature` (hoje em `creature_management_service.cpp`) (issue upstream #3513) | para player, `internalPlaceCreature` só põe o player nas zonas (`zone->creatureAdded`) e os callbacks de zona (`afterCreatureZoneChange`) rodam em `placeCreature` logo depois do laço de espectadores (`sendCreatureAppear`) e **antes** do `onPlacedCreature` (scripts de login), como era a ordem relativa antes; monstro e NPC seguem como antes (decisão do usuário) | `afterEnter` que mexe no player (ex.: `changeSpeed`) mandava pacote de uma criatura que o client ainda não conhecia, e o client fechava no login. Sem teste unitário (a ordem depende do pacote de entrada); o smoke de login confere que o `afterEnter` com `changeSpeed` dispara no login e o login segue |
| `data-otservbr-global/startup/tables/chest.lua` (`[6117]`, `[6121]`), `teleport_item.lua` (`[24873]` ×14), `item.lua` (`[4649]` repetido) | baú do Captain Iglue's Treasure virou `[6260]` e o saco do Minotaur Hell (32127,32065,12) virou `[6261]` (livres no mapa, nas tabelas e nos scripts; dentro do intervalo 5000–9000 do `quest_reward_common.lua`); os 14 teleports do Forgotten Knowledge numa chave só com `itemId` por posição; `[4649]` idêntico removido. Check `tools/datapack/duplicate_keys_check.py` (+ testes) | o Lua guarda só a última chave: os dois baús ficavam sem unique id (o `.otbm` não tem 6117/6121 neles), então não entregavam nada; os teleports já têm o aid no `.otbm` atual. Em conflito: manter uma chave por id e rodar o check |
| `data/libs/gamestore/player.lua` (`removeCoinsBalance`, `removeTransferableCoinsBalance`), `parsers.lua` (`parseBuyStoreOffer`), `purchases.lua` (renomear hireling, trocar sexo) (issue upstream #3742) | o saldo vai ao client depois de tirar as coins, e só pela função que tira (o envio extra depois do `makeCoinTransaction` saiu) | o client recebia o saldo duas vezes, a primeira com o valor antigo. Harness `tools/lua/store_balance_check.lua` (o upstream falha 2) |
| `game.cpp` `Game::loadItemsPrice`/`Game::rebuildItemsPrice`, `protocolgame_send.cpp` `ProtocolGame::sendItemsPrice` (issue upstream #2083) | `rebuildItemsPrice` devolve se algum preço mudou; só então `items_price_broadcast::start()` (`src/game/market/items_price_broadcast.cpp`, arquivo da branch) monta o `0xCD` **uma vez** (`ProtocolGame::addItemsPrice`, estático, também usado no login) e entrega em lotes de 50 jogadores a cada 100 ms; o callback só captura ids, a mensagem compartilhada e uma geração (um reenvio novo anula os lotes pendentes). Métrica: `method_latency` de `start`/`sendBatch` | o client guarda a lista do login (valor do item na Cyclopedia). A 1ª versão reenviava a lista montada por jogador numa tarefa só: 3,8 ms por jogador com a lista cheia (~6000 preços, 60 KB) no build de testes, ~2,6 s para 1000 online. Agora: 3 ms para montar + ~1 ms por lote. Se o upstream mexer no `sendItemsPrice`, a montagem vive em `addItemsPrice` (testes `ItemsPricePacketTest`, `ItemsPriceBroadcastTest`, `OnlyTheLatestRefreshSendsItsBatches`) |
| `data-otservbr-global/scripts/creaturescripts/customs/freequests.lua` (issue upstream #3478) | `DjinnWar.Factions` = 1 e `TheSecretLibrary.Questlog` = 1 acrescentados (storage de início da quest); Barbarian Test missões 2 e 3 = 2 (era 3); `GraveDanger.Questline` = 2 (era 14) | o questlog esconde missão com valor fora de `startValue..endValue` e quest sem storage de início: Secret Library, Factions, Barbarian Test 2/3 e Grave Danger nunca apareciam. Harness `tools/lua/freequests_check.lua` (o upstream falha 5 checagens); rodar de novo em todo merge que mexa no freequests ou no catálogo |
| `data-otservbr-global/scripts/quests/the_ancient_tombs/actions_oasis_lever_door.lua`, `data-otservbr-global/startup/tables/item.lua` `[12107]`, `data-otservbr-global/startup/others/functions.lua` `loadLuaMapAction` (issue upstream #3453) | quem acha a cenoura recebe o storage 12107 (o aid da porta); as duas entradas `[12107]` viraram uma, com `itemId = 1662` só na posição da porta; o loader aceita `itemId` por posição (`value.itemPos[i].itemId`, senão `value.itemId`) | a porta aberta (1663) está na `QuestDoorTable` e o `closing_door.lua` só deixa passar quem tem o storage do aid, então o player voltava; a segunda `[12107]` apagava a primeira (o Lua guarda só a última), e a tabela deixava de dar o aid às alavancas (no `otservbr.otbm` atual elas já têm o aid, então o efeito só aparece em mapa sem ele). Harness `tools/lua/oasis_door_check.lua` (o upstream falha 4 checagens). Em conflito no `item.lua`, manter uma chave por aid e usar `itemId` por posição |

**Banco de dados:** índice novo **nunca** vai em migration numerada (`data-*/migrations/N.lua`) nem
no `schema.sql`. O upstream numera as migrations, então um número nosso colidiria com o próximo
dele e faria servidores pularem a migration do upstream. Acrescente uma entrada em
`schema_hardening::wantedIndexes()`, que roda em todo boot depois das migrations e só cria o que
falta. Para remover índice redundante, meça antes e peça aprovação ao usuário.

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

## 6. Phase 20 (concluída)

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

## 6c. Phase 22 (concluída)

- **22.0**: mapa upstream → branch (§5b), tabela de chamadas IO → portas, `tools/merge/coverage_check.py`.
- **22.1**: `players_online` → `PlayerRepositoryDB::updateOnlinePlayers`. IT fora do
  `databaseTest`, porque o `BEGIN` do `DBTransaction` faz commit implícito da transação do harness.
- **22.2**: highscores → `IHighscoreRepository`/`HighscoreRepositoryDB`; callback sem `[this]`.
- **22.3**: 7 testes de login. **Bug corrigido**: personagem marcado para deleção entrava no
  mundo.
- **22.4/22.5**: caches de highscore com teto; página 0 recusada; página fresca sem query;
  intervalo de 1s por player; teto global de 8 queries; métricas.
- Resultado: ut 908/908, it 85/85. Pendente: abrir a janela de highscores no cliente uma vez.

## 6d. Phase 23 (em andamento, parada em ponto estável)

Medir e conter o que o cliente (ou um bot) consegue fazer o servidor gastar. Feito:
- **Monitoramento:** build `linux-release-metrics` validado (com `/metrics` respondendo) e
  `packet_watch` (latência por opcode e aviso de pacote lento com player/IP).
- **Inventário medido:** tabela no `todo.md` (23.2); catálogo do bot de ataque em
  `/home/nicollas/teste/proxy-bot`, lido sem executar.
- **Correções:**
  - teto da fila de saída (`maxOutputQueueMessages`), com o use-after-free da versão inicial
    corrigido;
  - intervalos de outfit;
  - teto de conexões não logadas por IP (`maxPendingConnectionsPerIp`, que não afeta CGNAT);
  - índice `solid_market_history_stats` via `schema_hardening` (sem migration numerada);
  - cama sem load completo do dorminhoco;
  - preços do mercado com as consultas fora do loop (o parse das linhas agrupadas ainda roda no
    dispatcher; sem linhas, o cache antigo é mantido);
  - `Database::getInstance` por `DI::port`.
- **Verificado sem mudança:** o `move` do bot é bloqueado pelas checagens de `playerMoveItem`;
  a posse de outfit é validada (`canWear`).
- **Auditoria de pacotes (valores do cliente usados como índice, id, nome ou caminho):** corrigidos
  VIP (crash), grade de gema do wheel (corrupção de memória), charm não comprado, prey em boss,
  lance de leilão (regras ignoradas, casa com dono, sequestro de transferência), transferência
  (load offline por pacote), report de violação (caminho de arquivo e disco) e busca/transferência
  do store em Lua (padrão Lua travava o servidor; transferência de 0 coins); detalhes na tabela
  de correções do §5b, regras no §2b. Verificados sem mudança: índice de prey (`raceIdList`), slot de inventário,
  ações de gema (`getGem` limitado), slots de pontos do wheel (enum), slot de imbuement
  (`canAddImbuement`), tier do forge (`tiers.contains`), perks de proficiência, aceite no mercado
  (`isUIExhausted(1000)` e troca real), info de personagem da Cyclopedia (só o próprio),
  `playerDebugAssert` (sem chamador), `follow` (pathfinding assíncrono).

- **Segunda rodada (23.5, 2026-11-13):** corrigidos o **derrube de qualquer jogador sem a senha**
  no login (sessão existente tratada antes da autenticação; igual no upstream; provado com sockets
  reais em `tools/smoke/login_takeover_check.py`), loads de personagem offline pelo `!transfer`,
  talkactions sem intervalo, log sem limite por pacote (`log_budget`), `!reward` repetível,
  categoria e lista do quick loot, "digitando" sem intervalo e `/name` do livestream. Detalhes na
  tabela do §5b e no `todo.md` (23.5).
- **Terceira rodada (23.6, 2026-11-19):** módulos Lua por recvbyte e SQL em Lua: recompensa diária sem token
  (byte "shrine" do client), rastreador de quests de 1,6 ms por pacote (índice de missões), outfit de hireling
  sem limite de cor/montaria; `setGlobalStorage` com escape. Detalhes no `todo.md` (23.6).

Pendente (ordem sugerida):
0. **Antes de produção:** uma sessão com client real no build de métricas (login, relog, andar,
   outfit, highscores trocando páginas rápido, cama) e depois um scrape de `/metrics`. O teto de
   conexões por IP já foi provado com sockets TCP reais no smoke (10 mantidas, 2 recusadas);
   login, `packet_watch` e intervalos de outfit/highscore com client real ainda não.
   Os testes da auditoria de pacotes cobrem as recusas; os caminhos legítimos precisam desta
   sessão: lance válido em casa à venda, transferência pelo dono (e o aceite), upgrade de grade de
   gema no wheel, atribuir charm desbloqueado, prey pela lista completa, report de violação e
   busca/transferência de coins no store. Da 23.6: slots do Bosstiary (atribuir, remover pagando e escolher
  outro; a troca direta é recusada, como no client), recompensa
  diária pelo shrine e pelo painel, outfit de hireling. VIP: uma escrita por segundo (adicionar vários VIPs, editar e criar
  grupo em seguida não pode ser barrado no uso normal; olhar `client_requests_throttled{request="vip"}`). Bosstiary: a primeira troca do dia
  sai grátis na janela e a segunda mostra 100k. Canal privado: convidar dois amigos seguidos (cada um
  avisado na hora; o intervalo é por dono e convidado).
  Taskboard: uma ação a cada 250 ms (`modules.xml`); abrir a janela e
  trocar de aba rápido olhando `module_packets_throttled{opcode="0x5F"}`: se subir sem clique rápido, o
  client oficial manda ações seguidas sozinho e a segunda some sem resposta (rever o intervalo). Depot
  search, "retrieve all": item com tier (o tier da busca volta igual), item com cargas ou
  imbuement (volta igual), item da loja (vai para o Store Inbox e é usável dali, inclusive pela hotkey), pilha grande
  (volta inteira), mochila com conteúdo (vem cheia) e algumas dezenas de itens não empilháveis de uma vez
  (olhar `packet_0x29` no `/metrics`). Team finder: pedir
  entrada num anúncio de outro jogador (agora o pedido vai para o anúncio do líder; antes ia para o do
  próprio solicitante e o líder nunca via) e o líder aceitar. Da 23.5: "digitando" (só mudanças são transmitidas agora;
   conferir se o client reenvia `0x38` para manter o indicador e o que vê quem chega no meio),
   lista do quick loot real (teto de 1000; acima disso é recusada inteira), categorias de container
   de loot que o client manda (nunca fora do enum), 1 talkaction por segundo no jogo normal, e o
   login com o client 15.25 (o smoke cobriu o 8.60; mesma função, layout de session key).
2. ~~Conexões próprias para `DatabaseTasks`~~: feito em 2026-11-14 (pool no modelo do HikariCP só para as consultas
   assíncronas, `databaseTaskConnections = 2`, e retry de transação vítima de deadlock; tabela do §5b). O lado
   síncrono continua com uma conexão e o retry do upstream.
3. A* antes do intervalo de ação em "usar item (com)": medir com `[PacketWatch]` em produção
   antes de mudar, porque altera o tempo de reação do jogo.
5. Teste manual dos highscores no cliente.
6. ~~Custo do `packet_watch::Scope`~~: medido em 2026-11-14 com o SDK do build de métricas
   (`tools/bench/packet_scope_bench.cpp`): 330–390 ns por pacote (nome copiado, 3 leituras de relógio, mapa de
   atributos, `Record` no histograma). 1000 jogadores a 25 pacotes/s ≈ 9 ms por segundo (< 1% de um núcleo): sem mudança.
8. ~~Ofertas só para preços~~: feito (`loadBestOfferPrices`: `MAX`/`MIN(price)` agrupado por item e tier no banco, sem
   subconsulta do nome do dono; IT `BestOfferPricesAreGroupedPerItemAndTier`).
9. ~~Mailbox~~: feito só a checagem leve de existência (escolha do usuário: opção 1, sem limite
   por remetente). Entrega a offline existente ainda custa load + save síncrono; se o
   `[PacketWatch]` mostrar abuso em produção, reavaliar o limite.
10. ~~Leilão de casas~~: feito (um lance por conta, só subir o próprio lance, lance menor não baixa
    o preço, valor dentro da coluna). Fica sem teste unitário o caminho de transferência com dono
    (o `House::setOwner` consulta o banco); cobrir na sessão com client real.
11. ~~Troca de nome no store~~: feito (lista branca de letras e espaço; formato validado antes do
    lookup que pode carregar personagem offline). Harness `tools/lua/store_name_check.lua`.

Resultado atual: ver a última linha de validação no `todo.md` (CHECKPOINT 23).

## 6e. Phase 24 (em andamento): datapack conferido com o TibiaWiki

- **Ferramenta** (`tools/datapack/`, só relatório, não edita o datapack):
  `python3 -I tools/datapack/wiki_check.py extract|fetch|report` → `build/datapack-wiki/`
  (`monsters.jsonl`, `cache/`, `report.json`, `report.md`). Testes:
  `python3 -I tools/datapack/test_wiki_check.py`. O `fetch` só baixa o que não está em cache
  (apague `build/datapack-wiki/cache` para atualizar); ~1 pedido/s, User-Agent genérico.
- **Regras do comparador** (detalhes no docstring e no `plan.md`): o servidor rola cada entrada de
  loot independente em 1/100000 e soma entradas repetidas; o bloco `Loot2` usado é o mais novo com
  ≥ 100 kills; chance só é sinalizada fora de um intervalo de Wilson (z = 3,29); `maxCount` só quando
  fica abaixo do observado; "só no datapack" só quando a ausência é significativa (≥ 7 quedas
  esperadas; remoção no item "Loot só no datapack" abaixo); elemento: `percent` = 100 − `DmgMod` do wiki (conferido
  nos 9 elementos do Dragon); campo que diverge em mais da metade vira questão sistemática.
- **Achados da primeira rodada:** mitigation diverge em 689 de 690 com razão ≈ 0,63 (fórmula ou
  unidade; investigar antes de mexer); muitos chances do datapack batem com estatísticas antigas
  (8.6) e não com as atuais; nomes ambíguos no `items.xml` (ex.: dragon trophy → 2681/2682)
  precisam de escolha manual.
- **Correções aplicadas** pelo manifesto `tools/datapack/fixes.json` (`plan` acumula; `apply`
  idempotente): 514 `maxCount` (24.4a) e 265 itens de loot (24.4b). Regras do `plan`: só
  empilhável para `maxCount`, nunca baixa, sem bosses (loot do baú de recompensa), salto acima de
  5× (e de 20) vai para revisão; adição sem moeda (o datapack paga muito gold em várias entradas de
  gold coin onde o wiki registra platinum), só estatística da versão 12+, só nome que resolve para
  um item. O resto fica na lista `review` do manifesto com o motivo.
- **Mitigation investigada:** o servidor usa `info.mitigation × multiplicador de defesa` como % de
  redução de dano (`Monster::getMitigation`, `Creature::mitigateDamage`) e manda o valor cru ao
  bestiário. O wiki diz que mitigation depende de escudo, defesa e **postura de combate**; a razão
  constante ~0,63 é compatível com valores de postura/fórmula diferente. Sem fonte que diga qual
  postura o wiki registra, não mexer.
- **Lotes seguintes:** 1014 chances (24.5a), 277 atributos (24.5b, só com raceId igual ao do
  wiki), 58 fixes em bosses de corpo (24.5d). Total no manifesto: 2024.
- **Boss = `rewardBoss`, não bosstiary:** 136 monstros `rewardBoss` estão fora do bosstiary (ex.:
  Ferumbras Mortal Shell, Necropharus, um Cave Spider de quest). O loot deles vem do baú: cada
  participante rola a tabela com `lootFactor` (`data/scripts/systems/reward_chest.lua`: 2× solo),
  então a chance do datapack é base e não se compara ao wiki. A primeira versão da regra olhava o
  bosstiary e aplicou 104 fixes neles; foram revertidos (`40ed1a2cd`). Bosses de corpo seguem as
  regras normais.
- **Revalidação (24.6):** o `plan` revalida todo o manifesto a cada execução; o que deixa de valer
  sai do manifesto e o arquivo é restaurado ao estado de `997af9180` antes de reaplicar. Regras
  acrescentadas: loot exige race id no datapack (`raceId` ou `bossRaceId`) e, se o wiki tiver, igual
  (variantes de quest e evento costumam reusar o nome sem race id); atributos ignoram valores que o
  wiki marca como incertos (`100%?`, `~500`). Saíram 122 fixes (110 de loot sem identidade, 12
  elementos incertos); entraram 42 atributos de bosses de corpo (identidade pelo `bossRaceId`).
  Total: 1963. A lista de revisão (com o motivo de cada item) fica em `build/datapack-wiki/review.json`,
  fora do repositório.
- **Revisão do que sobrou (24.7):** saltos grandes passam com evidência independente (outra versão
  concorda e nenhuma apoia o datapack, ou bloco único com ≥ 30 quedas em ≥ 1000 kills); decisões
  manuais em `tools/datapack/reviewed.json` (`apply`/`keep` com nota); elementos e imunidades de
  bosses de baú seguem as regras, mas HP/XP/armor de qualquer boss ficam para revisão (scripts de
  quest podem usar valores absolutos); nomes ambíguos/sufixados resolvidos pelas flags `take` e
  `market` do `appearances.dat` e gravados por id; estatística antiga só quando cabe na raridade
  atual da infobox (faixas do `Template:Loot Item`, tolerância de 25%) e o datapack não cabe.
  Manifesto: 2823 fixes.
- **Wiki = fonte da verdade (decisão do usuário, 2026-11-13):** o jogo original teve muitos
  rebalanceamentos, então atributos (HP, XP, armor, mitigation), elementos, imunidades e chances
  seguem o wiki sem limite de proporção, bosses inclusos. Os únicos portões: identidade (race id do
  datapack igual ao do wiki; loot exige race id no datapack), valores certos (sem `?`/`~`), loot de
  baú fora (chance base por participante) e estatística da versão 12+ (ou antiga confirmada pela
  raridade atual). Mitigation do datapack estava em ~0,63× o bestiário em quase todos.
- **Dinheiro (`coins`):** o dinheiro do wiki é o registrado (`total ÷ kills` × valor da moeda); a
  faixa escrita sai da média real por queda (`[1, 2×média−1]`), porque o servidor sorteia uniforme e
  a faixa observada exageraria o dinheiro. Só com ≥ 1000 kills; quando os itens de moeda diferem ou o
  dinheiro difere > 10%, todas as entradas de moeda são trocadas (com `minCount` quando preciso). O
  `coins` substitui os `max_count` de moeda do mesmo monstro. 127 monstros; 3 continuam 10–15%
  fora por arredondamento de faixas pequenas. Manifesto: 3680 fixes.
- **`from` = valor do upstream:** um fix planejado de novo depois do `apply` lê o nosso próprio
  valor; o merge mantém o `from` da primeira vez, e o `coins` novo recupera o valor do upstream
  quando substitui um aumento de `maxCount` já aplicado.
- **Bosses com mudança extrema confirmados (usuário, 2026-11-13: "siga a wiki"):** The Monster,
  Mitmah Vanguard, Shulgrax, Duke Krule, Count Vlarkorth, Chikhaton (HP), The Shatterer (HP e XP 0)
  e Deathstrike (XP) ficam com os valores do wiki; nenhum `keep` no `reviewed.json`.
- **Erro corrigido: item adicionado em dobro (24.7j).** O comparador casava nomes do wiki e do
  datapack só pelo texto: "gore horn (item)", "book (orange)", "darklight core (object)" não
  encontravam a entrada `gore horn`/`id = 2828 -- book` que o upstream já tinha, e o `add_loot`
  acrescentava outra. 34 adições: 14 ficaram com o mesmo id duas vezes (Gore Horn, lost souls,
  darklight do Rotten Blood: a chance somava), 16 acrescentaram um livro/mapa de outro id ao lado do
  certo (401 ao lado do 2828 laranja), 4 não chegaram a aplicar. Agora: `loot_aliases` pareia linha
  do wiki e entrada do datapack quando os ids se cruzam (um para um; dois para um vai para revisão),
  o `plan` não adiciona item que o datapack já tem com outro nome, o `validate` derruba os antigos
  (`listed_elsewhere`) e o `apply` recusa a adição (`listed as another entry`). Os 32 arquivos
  voltaram ao estado de `997af9180` e o manifesto foi reaplicado; invariante conferida em todos os
  arquivos: nenhuma entrada nova com id repetido em relação à base.
- **Loot só no datapack (`remove_loot`, decisão do usuário, 2026-11-13: "siga a wiki"):** removido
  quando a chance do datapack daria ≥ 7 quedas na amostra e o wiki não registrou nenhuma. Portões
  (`fixes.removal_problem`): identidade e baú como o resto do loot; moeda fica com o `coins`; id sem
  nome no `items.xml` fica; item que a tabela atual da página lista fica; linha do wiki que não
  resolve para item bloqueia o monstro (pode ser o mesmo drop com outro nome); estatística antiga
  só se a tabela atual existe e não tem o item. 78 remoções (55 com estatística antiga confirmada pela
  tabela atual, 23 recentes; nenhuma sem tabela atual). Conferência: a página do Rotworm diz que o
  Legion Helmet saiu do loot no Christmas Update 2009, e o datapack ainda tinha 1,89%. Ficaram fora
  207: baú 141, sem race id 36, listado na tabela atual 9, id sem nome 9, antiga sem tabela 8, moeda
  3, nome do wiki sem resolução 1. Manifesto: 3745 (depois 3916, ver redução de `maxCount`). "Nunca registrado" vale para o bloco usado e a tabela
  atual: 4 dos 78 aparecem em blocos antigos pequenos (ex.: Cobra Assassin ring of red plasma, 18 em 474 kills
  na 12.20, 0 em 43916 na 12.65). Varredura de obtenção: todos os 78 continuam vindo de outro monstro ou de
  NPC (eternal flames do Fire Overlord, reinvigorating seeds do Orclops Ravager, witchesbroom 3211 na loja
  da Nelliem); nenhum passo de quest ficou sem fonte.
- **Redução de `maxCount` (decisão do usuário, 2026-11-13: "sim"; alvo é o Tibia atual):** o servidor
  sorteia a quantidade uniforme em `[minCount, maxCount]`; se nenhuma das `n` quedas do bloco passou de `m`,
  a chance disso com o datapack é `((m − min + 1)/(max − min + 1))^n`. Abaixo de e^−7 (o mesmo limiar da
  remoção), o `maxCount` vira `m`. Só empilhável, entrada única, sem moeda, estatística 12+ (a raridade
  atual não fala de quantidade), `m` ≥ `minCount`, e linha do wiki coerente: 14 linhas sem `amount:` tinham
  `total` maior que vezes × 1 (o wiki viu pilhas sem registrar a faixa) e ficam de fora. 171 reduções
  (ex.: Icecold Book com 7 itens de até 8 para 1, em milhares de quedas). Manifesto: 3916.
- **Aumento de `maxCount` também só com estatística 12+ (decisão do usuário, 2026-11-13):** quantidade segue
  o jogo atual, então o aumento ganhou o mesmo portão da redução (`plan` e `validate`). Saíram 233 aumentos
  vindos de blocos anteriores à 12; os 177 arquivos voltaram a `997af9180` e o manifesto foi reaplicado
  (233 linhas voltaram ao valor do upstream). Ficam 144 aumentos e 171 reduções. Manifesto: 3683.
- **Versão das estatísticas (decisão do usuário, 2026-11-13: "15+ quando existir"):** o bloco mais novo
  com ≥ 100 kills manda (já era a regra); 23 monstros têm bloco 15+ menor (quase todos bosses, 1–90 kills)
  e usam o bloco maior anterior. O resto segue as regras de antes (12+, ou antiga confirmada pela
  tabela atual). A versão efetiva pelo `implemented` não foi adotada.
- **Rótulo de versão do `Loot2`:** o bloco "8.6" aparece em criaturas que não existiam na 8.6 (Candy
  Horror 13.40, Weretiger 13.20, Cult Believer 11.40); o rótulo parece ser o padrão do coletor antigo. Os
  portões de "estatística antiga" usam o rótulo, então são conservadores demais para essas criaturas
  (24.7k: não adotado).
- **Baú de recompensa segue o wiki (decisão do usuário, 2026-11-13):** cada participante rola a tabela com
  `chance × fator` (`reward_chest.lua`: `f·(1+f)^(contribuição/esperada)`, `f = 1/participantes^(1/3)`; 2,0 solo,
  ≈ 1,0 com 4 jogadores iguais, ≈ 0,68 com 10). A taxa por jogador do wiki vira a chance base (grupo típico de ~4).
  Exceções: entradas `unique` (só o maior contribuidor rola, `getBossReward`) ficam como no upstream (`REWARD_UNIQUE`),
  e item que falta no baú fica para revisão (`REWARD_MISSING`: pode ser `unique`). Entraram 1056 fixes em 107 bosses:
  375 chances (227 subiram, 148 desceram), 644 `maxCount` (596/48), 37 remoções (todos os itens seguem obtíveis em
  outra fonte). Os bosses de The Curse Spreads e a Percht Queen escreviam `chance = 13600000` (drop garantido acima de
  100%): o comparador limita a probabilidade a 100%, então o `plan` grava como `from` a chance crua da entrada
  (`datapack_raw_chance`); sem isso o `apply` acusava conflito. 48 drops garantidos viraram a taxa do wiki.
  Estatística antiga sem confirmação e identidade não confirmada ficam como no upstream (decisão do usuário).
  Manifesto: 4739.
- **Grupos pequenos (24.7o):** `tools/datapack/wiki_item_ids.json` guarda o `itemid` da página de item do wiki
  (`wiki_check.py item-ids`, 247 de 281 nomes) para nomes de loot que o `items.xml` não fixa. Ele entra só para nomes
  que o `items.xml` **não tem** (itens novos sem nome, como bulltaur horn = 44736, e variantes com sufixo, como giant
  shimmering pearl (green) = 281); nome que o `items.xml` tem, mesmo ambíguo, continua como o servidor o resolve (o
  datapack usa esses nomes; sobrescrever quebrou o par `{ id = 28568 } -- inkwell` ↔ "inkwell (black)" na primeira
  tentativa) e a página só escolhe o id de uma **adição** quando dá um id só. Adição com id da página é `exact`: o
  `apply` só recusa se aquele id já estiver listado (o pedestal vermelho 9063 entra ao lado do 9065 de outra cor).
  `validate` derruba fix tirado de linha do wiki com sufixo que a página diz ser outro item (a The Scourge of Oblivion
  tinha a taxa da pérola marrom na entrada verde; arquivo restaurado). Item em 2+ entradas vira `chance_split`: todas
  as chances escaladas pelo mesmo fator até a probabilidade combinada bater com o wiki (mantém a forma das entradas).
  Fix de chance de entrada única acha a própria entrada pelo valor quando outra do mesmo nome aparece depois.
  Resultado: 18 splits, 10 adições por id (dragon trophy 44158, sai 50183, lichen gobbler 43782, pérolas, frasco,
  pedestal, pergaminho), 3 remoções liberadas (as linhas do wiki que bloqueavam são outros itens) e 1 chance; ficaram
  `die` e `wooden trash` (a página lista vários ids equivalentes) e a coroa da Percht Queen (página sem `itemid`).
  Manifesto: 4770; lado do upstream: 0 conflitos, 4 arquivos só com outra ordem de linhas.
- **Controle negativo do smoke:** um item de loot inexistente injetado no Dragon gera
  `[LootFunctions::luaLootSetIdFromName] - Unknown loot item` e `could not be loaded`; o grep do
  smoke pega os dois.

## 6f. Phase 25: massa de teste e swarm de bots

- `tools/loadtest/seed.py` cria/restaura as contas `@loadtest.invalid` no banco do `config.lua` (decisão do usuário:
  o banco normal pode receber a massa). Bots (`lt_bot_*`, personagens `Swarm …`) e manuais (`lt_manual_*`,
  `Tester …`) ficam em contas separadas: o swarm só usa os bots. Rode com o servidor sem personagens de teste online
  (o logout salva por cima). Logins em `build/loadtest/characters.json`.
- `tools/loadtest/swarm.py` sobe N clientes 15.25 (`tibia_client.py`) a partir de 127.0.0.2+, respeitando os limites
  por IP; precisa do build de métricas com `metricsEnablePrometheus = true` para o relatório.
- Achados da primeira rodada (KV sem cache negativo, login caro por títulos, logout síncrono, buckets do exportador)
  estão no `todo.md` (25.4); viraram pendências de C++ para quando o usuário liberar.

## 6g. Phase 26 (concluída): issues abertas do upstream

- Worktree `/home/nicollas/lundaria/canary-upstream-issues`, branch `nicollassantos/upstream-issues`
  (saiu do `7e517fb00`; as mudanças não commitadas do checkout principal não estão nela). Triagem das
  58 issues e níveis de prova em `tasks/upstream_issues.md`; tasks e resultados no `todo.md` (Phase 26; aberta como 25 e renumerada no merge com a `solid`, que já tinha a Phase 25 de load test).
- Corrigidas: #4154 (porta do PR #4179), #4114 (porta do PR #4181), #4153, #4125, #3478, #3453, #3513,
  #3742, #2083, mais chaves repetidas nas tabelas de startup (baús do Captain Iglue's Treasure e do
  Minotaur Hell sem unique id). Linhas no §5b.
- Ferramentas novas: `tools/lua/freequests_check.lua`, `tools/lua/oasis_door_check.lua`,
  `tools/lua/store_balance_check.lua`, `tools/datapack/duplicate_keys_check.py` (+ testes).
- Decisões do usuário (2026-11-17): `!withdraw`/banco sem teto (#3374; medição no `todo.md` 26.12,
  até ~154 ms para o maior saque num container com espaço); #4091 e #4057 só registradas (edição do
  `.otbm` no RME).
- Falta: sessão com client real (cor do preço na Cyclopedia, #4153 com o client 15.25, campo/bomba,
  aljava).
- Merge na `nicollassantos/solid` (2026-11-17): conflito em `Game::loadItemsPrice` resolvido juntando a
  consulta `loadBestOfferPrices` da solid com o reenvio em lotes da #2083.

## 7. Smoke test do servidor (banco descartável)

1. Copiar `config.lua` para o scratchpad trocando `mysqlDatabase` para `"canary_smoke_<fase>"` e
   as portas para 17171/17172 (para não colidir com outro servidor). Para o smoke de login 8.60, fixe
   **as duas** portas legadas (`legacy860GameProtocolPort` e `legacy1100GameProtocolPort`): com uma delas
   em 0, as duas caem na mesma porta e o login 8.60 é fechado sem log (comportamento de conexão do 11.00).
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
| 22 | SQL de `players_online` e de highscores nos adapters; correção do login de personagem deletado; highscores protegem memória e MySQL (seção 6c) |

Contagem de testes: fim da Phase 19, `canary_ut` 867/867 e `canary_it` 56/56; fim da Phase 20,
885/885 e 68/68; fim da Phase 21, 888/888 e 72/72; fim da Phase 22, 908/908 e 85/85.
