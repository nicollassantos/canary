# Levantamento: onde a branch ainda não segue SOLID e a arquitetura hexagonal

Pedido do usuário (2026-11-17): "procure por onde ainda não está implementado com SOLID e arquitetura
hexagonal". Retrato do código em `nicollassantos/solid` depois da Phase 25 e do merge da 26. Os números
vêm de `git grep` sobre `src/*.cpp` (linhas, não ocorrências) e servem para medir progresso; os critérios
de prioridade são os do `tasks/AGENTS.md` §1 e §6: uma porta só quando um fake permite um teste que hoje
não existe, e mudança em arquivo do upstream só quando o ganho paga o conflito de merge.

## O que já está no lugar

- **Driven ports com adapter dono do SQL e fake de teste:** `IConfigManager`, `IDatabase`, `IPlayerRepository`
  (nome/guid/saldo; load/save ainda delegam ao `IOLoginData`), `IVipRepository`, `IMarketRepository`,
  `IGuildRepository`, `IHighscoreRepository`, `IPlayerStorageRepository`, `AccountRepository`; portas por papel
  `CreatureFeedback`/`WorldRegistry`/`ExpertPvpWorld` nos components.
- **Casos de uso fora do `Game`:** services de movimento, interação, troca, loot, outfit, chat, forge, mercado,
  highscore, cyclopedia, som, banco, combate e criaturas (`game.cpp` em 3785 linhas, abaixo do teto de 4k).
- **`Player` dividido em components** e `ProtocolGame` em 6 arquivos (parse, envio por assunto, helpers).
- **Infraestrutura:** DI (`boost::di`) com `DI::port<T>()` para fakes em teste, pool de conexões para as
  consultas assíncronas, KV com cache de ausência, save do logout gravado e commitado fora do loop.

## O que falta (por ordem de ganho)

### 1. `player.cpp` passou do teto combinado (7002 linhas, teto 7000) — feito (25.12)

As regras de elegibilidade do leilão de casas (`canBidHouse`, `canTransferHouse`, `canAcceptTransferHouse`)
saíram do `Player` para `src/map/house/house_auction_rules.cpp`: funções que recebem `Player` e `House`
explícitos, sem `g_game()`, com testes diretos (`HouseAuctionEligibilityTest`). `player.cpp` em 6897 linhas.

### 2. SQL síncrono dentro de objetos de domínio (sem porta, sem fake, no loop do jogo)

| Onde | O que faz | Por que importa |
|---|---|---|
| `src/creatures/players/management/ban.cpp` (`IOBan`) | 3 consultas por login (ban de conta/IP) | login é caminho quente e hostil; sem fake, o caminho de ban não tem teste unitário |
| `src/map/house/house.cpp` | dono, transferência, lista de acesso (3 consultas) | casas tiveram 4 bugs na 23.3l; `House::setOwner` não tem teste porque consulta o banco (nota no §6d) |
| `src/game/game.cpp` | `loadBoostedCreature`, `loadMotdNum`, `loadPlayersRecord` (11 consultas em `server_config`) | pequeno; uma `IServerConfigRepository` tiraria o último SQL do `game.cpp` |
| `src/io/io_bosstiary.cpp` | boss boosted (4 consultas) | estático (`IOBosstiary`), sem fake |
| components: `player_forge_history.cpp` (3), `wheel/player_wheel.cpp` (2), `player_title.cpp` (1), `player_cyclopedia.cpp` (1), `player_badge.cpp` (1) | load de histórico, slots do wheel, ranking de títulos, contagem de hirelings, badges | SQL no domínio; parte roda a cada login (o swarm mediu) |
| `src/io/iomapserialize.cpp` (8) | itens e info das casas no save do servidor | estático; casa já é ponto de bug |

**Ação proposta:** portas `IBanRepository`, `IHouseRepository` (+ parte de casas do `IOMapSerialize`),
`IServerConfigRepository`, e mover o SQL dos components para os adapters existentes (`PlayerRepositoryDB` ou
um `PlayerProgressRepositoryDB`), cada um com fake e teste de integração, como nas Phases 20–22.

### 3. Load/save de player ainda é IO estático (`IOLoginData`, ~2000 linhas em `src/io/functions/`)

Decisão anterior (§6): fora de escopo, porque o upstream edita esses arquivos com frequência. O
`IPlayerRepository` delega. Novidade da Phase 25: o save já roda contra um gravador de consultas
(`query_recorder`), então o adapter pode passar a ser dono da execução sem tocar no código de serialização.
**Ação:** manter fora, salvo pedido; a barreira de save (`PlayerSaveBarrier`) já vive no adapter.

### 4. Domínio chamando o `Game` global (1129 linhas com `g_game()`)

Maiores consumidores: `monster.cpp` 61, `player.cpp` 47, `spells.cpp` 43, `combat.cpp` 40, `tile.cpp` 39,
`player_attached_effects.cpp` 37, `wheel/player_wheel.cpp` 30, `condition.cpp` 28, `creature.cpp` 27,
`player_forge_component.cpp` 22, `party.cpp` 21. Por pasta: players 242, rede 233, bindings Lua 178,
combate 111, monstros 70.

O domínio (criaturas, combate, tile) depende do caso de uso global, o inverso do hexagonal. Trocar tudo é
renomear com custo de merge alto. **Ação:** portas por papel, como `CreatureFeedback`/`WorldRegistry`, só onde
um fake libera teste de comportamento sem cobertura: combate (`combat.cpp`, `condition.cpp`; o
`combat_service_test.cpp` registra que não consegue rodar `combatChangeHealth`) e monstro (`monster.cpp`).

### 5. Configuração global (455 linhas com `g_configManager()`)

Rede 103, players 67, `canary_server.cpp` 31, `game.cpp` 26, monstros 24, `ioprey.cpp` 22. Os services e
components da branch já usam a porta `IConfigManager`; o resto é código do upstream. **Ação:** trocar só nos
arquivos que já são da branch ou que ganharem teste; `protocolgame*.cpp` concentra a maior parte.

### 6. Adaptadores de entrada com regra de negócio

- `ProtocolGame` (adaptador de rede): 168 `g_game()` em `protocolgame.cpp`; funções longas com regra,
  não só parse: `onRecvFirstMessage` (261 linhas, autenticação e sessão), `login` (160), `parseSendBosstiarySlots`
  (133, monta a regra dos slots), `parseBestiarysendMonsterData` (117). **Ação:** levar login/sessão para um
  `LoginService` testável (a falha crítica da 23.5 estava nesse fluxo e só foi provada com sockets reais) e a
  montagem de bestiário/bosstiary para os services do assunto.
- Bindings Lua: 178 `g_game()` (`game_functions.cpp` 46, `player_functions.cpp` 27 e 5750 linhas). São adaptadores
  de entrada; aceitável, mas `player_functions.cpp` é o maior arquivo depois do `player.cpp`.

### 7. Classes grandes (SRP)

`player_functions.cpp` 5750, `protocolgame_send.cpp` 3792, `wheel/player_wheel.cpp` 4210 (component do upstream
que virou um subsistema inteiro), `monster.cpp` 4014, `item.cpp` 3676, `condition.cpp` 2955, `combat.cpp` 2885.
**Ação:** só quando outro item desta lista mexer no arquivo (dividir sem ganho de teste não paga o merge).

### 8. Service locator em vez de injeção

54 `getInstance()` e 47 aliases `g_*`; o container DI existe, mas quase tudo é resolvido globalmente na hora da
chamada (`DI::port<T>()`). Para teste isso já funciona (fake no container de teste). **Ação:** nenhuma ampla;
injetar pelo construtor nos services novos, como já é regra.

## Ordem sugerida (uma fase cada, com testes)

1. ~~`player.cpp` de volta abaixo de 7k (obrigatório).~~ Feito na 25.12 (6897 linhas).
2. `IBanRepository` + `LoginService` (login/sessão fora do `ProtocolGame`, com testes do fluxo que hoje só o
   smoke cobre).
3. `IHouseRepository` (casa: dono, transferência, listas, itens do `IOMapSerialize`), destravando o teste de
   `House::setOwner`.
4. SQL dos components para adapters (forge history, wheel, títulos, hirelings, badges) e `IServerConfigRepository`.
5. Portas de papel para combate/condições, onde liberam os testes de `combatChangeHealth`.
