# Refatoração SOLID + Arquitetura Hexagonal — Canary

## Contexto

Servidor MMORPG (Tibia/OpenTibia) com ~162k linhas em 454 arquivos C++. Três god objects dominam: `game.cpp` (12.5k), `player.cpp` (13k), `protocolgame.cpp` (10.7k). Existem 47 singletons — só 3 no container DI. 1.120 usos de `g_game` + 531 de `g_configManager` espalhados por 110+ arquivos tornam testes impossíveis e mudanças arriscadas.

**Fundação existente (reutilizar, não recriar):**

| Ativo | Localização | O que faz |
|---|---|---|
| `boost::di` container | `src/lib/di/` | `DI::get<T>()` / `DI::create<T>()` |
| Repository pattern completo | `src/account/` | Porta → Adaptador → Domain → Teste |
| `KVStore` → `KVSQL` | `src/kv/` | Template method + Decorator |
| `Logger` → `LogWithSpdLog` | `src/lib/logging/` | Já no DI |
| `Bank` domain module | `src/game/bank/` | Domínio puro sem deps infra |
| GTest + DI fixtures | `tests/fixture/` | `InMemoryAccountRepository`, `InMemoryLogger`, `InMemoryKV`, `InjectionFixture` |
| **31 arquivos de teste, ~156 test cases** | `tests/unit/` + `tests/integration/` | Account, KV, Forge, Lua, RSA, Player components |

**Módulos SEM cobertura (os god objects):**
- `game.cpp` (12.5k) → 4 testes superficiais
- `player.cpp` (13k) → só testes de components isolados
- `protocolgame.cpp` (10.7k) → ZERO testes
- IO modules (`iologindata`, `iomap`) → ZERO testes

---

## Decisões de Arquitetura

1. **Hexagonal (Ports & Adapters):** `domain/` puro → `application/` use cases → `infrastructure/` adaptadores → `ProtocolGame` como driving adapter
2. **DI over Singletons:** Portas abstratas + adaptadores concretos + bindings em `container.hpp`
3. **Vertical Slicing:** 1 subsistema completo por task. Nunca camadas horizontais
4. **Characterization tests primeiro:** Antes de refatorar qualquer god object, escrever testes que documentam comportamento atual
5. **ConfigManager como Port (Fase 1):** Desbloqueador — sem `IConfigManager` injetável, nenhum use case pode ser testado

---

## Grafo de Dependência

```
Logger ✓ (já no DI)
    │
    ├── IConfigManager (desbloqueador — Phase 1)
    │       │
    │       ├── IDatabasePort (Phase 2)
    │       │       └── IPlayerRepository (Phase 3)
    │       │
    │       ├── Game → use cases verticais (Phase 4):
    │       │       MovementService → CombatService → ItemService → TradeService → ...
    │       │
    │       └── Player → domain puro (Phase 5):
    │               componentes existentes + novos extraídos
    │
    └── ProtocolGame → thin adapter (Phase 6 — último)
```

---

## Phase 0: Baseline de Testes

### Task 0.1 — Verificar build e testes existentes
**Critérios:**
- [ ] Build sem erros (`canary_server`, `canary_ut`, `canary_it`)
- [ ] `ctest --output-on-failure` — todos os 156 testes passam
- [ ] Nenhum test flaky identificado

**Verificação:** `cd build && cmake --build . --parallel $(nproc) && ctest`
**Escopo:** XS | **Deps:** None

### Task 0.2 — Characterization tests: `game.cpp` (movimento + itens)
```
tests/integration/game/
  game_movement_it.cpp
  game_items_it.cpp
```
**Critérios:**
- [ ] 3+ testes de movimento (tile livre, tile bloqueado, teleport)
- [ ] 3+ testes de item (adicionar ao inventário, mover, remover)
- [ ] Todos passam sem modificar `game.cpp`

**Verificação:** `ctest -R game_movement -R game_items`
**Escopo:** M | **Deps:** Task 0.1

### Task 0.3 — Characterization tests: `player.cpp` (stats + inventory)
```
tests/unit/players/
  player_stats_behavior_test.cpp
  player_inventory_test.cpp
```
**Critérios:**
- [ ] 3+ testes de stats (health, mana, skills)
- [ ] 3+ testes de inventory (getInventoryItem, getWeapon)

**Escopo:** M | **Deps:** Task 0.1

**CHECKPOINT 0:** Build limpo + 10+ novos testes passando ✓ Revisão humana

---

## Phase 1: IConfigManager Port

### Task 1.1 — Criar `IConfigManager` port
```
src/config/
  config_port.hpp       ← IConfigManager (porta abstrata)
  configmanager.hpp     ← ConfigManager : public IConfigManager
```
**Critérios:**
- [ ] Todos os métodos de acesso viram `virtual` puros
- [ ] `g_configManager()` continua funcionando
- [ ] 156 testes passam

**Escopo:** S | **Deps:** Task 0.3

### Task 1.2 — `InMemoryConfigManager` + registro no DI
```
tests/fixture/config/in_memory_config_manager.hpp
src/lib/di/container.hpp  ← + binding IConfigManager
```
**Critérios:**
- [ ] `set<T>(key, value)` para bool/string/int/float
- [ ] `install(injector)` segue padrão DI
- [ ] 2+ testes que usam `InMemoryConfigManager`

**Escopo:** M | **Deps:** Task 1.1

**CHECKPOINT 1:** `IConfigManager` injetável + build limpo ✓ Revisão humana

---

## Phase 2: IDatabasePort

### Task 2.1 — Criar `IDatabasePort`
```
src/database/
  database_port.hpp     ← IDatabasePort
  database.hpp          ← Database : public IDatabasePort
```
**Critérios:**
- [ ] Abstrai: executeQuery, storeQuery, escapeString, beginTransaction, commit, rollback
- [ ] Binding em `container.hpp`
- [ ] Testes de `account_repository_db_it.cpp` passam

**Escopo:** M | **Deps:** Task 1.2

**CHECKPOINT 2:** Database injetável ✓ Revisão humana

---

## Phase 3: IPlayerRepository

### Task 3.1 — Criar `IPlayerRepository` port
```
src/creatures/players/
  player_repository.hpp        ← IPlayerRepository
  player_repository_db.hpp/cpp ← PlayerRepositoryDB (delega para IOLoginData)
```
**Critérios:**
- [ ] 4+ métodos load/save
- [ ] `IOLoginData` original inalterado

**Escopo:** M | **Deps:** Task 2.1

### Task 3.2 — `InMemoryPlayerRepository` + testes
```
tests/fixture/creatures/in_memory_player_repository.hpp
tests/unit/players/player_repository_test.cpp  ← 3+ testes
```
**Escopo:** M | **Deps:** Task 3.1

**CHECKPOINT 3:** Player persistência testável sem DB ✓ Revisão humana

---

## Phase 4: Game Decomposition — Vertical Slices

Cada task extrai 1 subsistema de `game.cpp`. Métodos originais delegam para o novo service. Servidor funcional após cada task.

### Task 4.1 — `MovementService`
Extrair: `internalMoveCreature`, `internalTeleport`, `playerMoveCreature`, `playerAutoWalk`
```
src/game/movement/movement_service.hpp/.cpp
tests/unit/game/movement_service_test.cpp  ← 3+ testes
```
**Critérios:** Service injetável; `game.cpp` reduz ≥ 200 linhas; testes passam
**Escopo:** L | **Deps:** Task 3.2

### Task 4.2 — `CombatService`
Extrair: `combatBlockHit`, `combatChangeHealth`, `combatChangeMana`, `applyCharmRune`, `applyLifeLeech`
```
src/creatures/combat/combat_service.hpp/.cpp
tests/unit/game/combat_service_test.cpp
```
**Critérios:** `game.cpp` reduz ≥ 300 linhas; `combat.cpp` inalterado
**Escopo:** L | **Deps:** Task 4.1

### Task 4.3 — `ItemService`
Extrair: `internalAddItem`, `internalRemoveItem`, `internalMoveItem`, `transformItem`, `addMoney`, `removeMoney`
```
src/items/item_service.hpp/.cpp
tests/unit/items/item_service_test.cpp
```
**Critérios:** `game.cpp` reduz ≥ 250 linhas
**Escopo:** L | **Deps:** Task 4.2

### Task 4.4 — `TradeService` + `IMarketRepository`
Extrair trade + market. `IOMarket` → `IMarketRepository` + `MarketRepositoryDB`
```
src/game/trade/trade_service.hpp/.cpp
src/io/market/market_repository.hpp + market_repository_db.hpp
tests/fixture/game/in_memory_market_repository.hpp
tests/unit/game/trade_service_test.cpp
```
**Critérios:** `game.cpp` reduz ≥ 400 linhas; ambos injetáveis
**Escopo:** L | **Deps:** Task 4.3

**CHECKPOINT 4:** `game.cpp` < 8k linhas; servidor funcional; revisão humana obrigatória

### Task 4.5 — Remaining Game subsystems (4 tasks S/M)

| Service | Métodos extraídos |
|---|---|
| `HighscoreService` | calculateHighscorePages, generateHighscoreQuery, loadPlayersRecord |
| `CreatureManagementService` | placeCreature, removeCreature, getCreatureByID, getPlayerByGUID |
| `SchedulingService` | wrapper de Dispatcher + SaveManager |
| `SoundService` | sendSingleSoundEffect, sendDoubleSoundEffect |

**Critérios:** `game.cpp` < 4k linhas (orchestrator leve)
**Deps:** Task 4.4

**CHECKPOINT 5:** `game.cpp` é orchestrator leve ✓ Revisão humana

---

## Phase 5: Player Domain Puro

### Task 5.1 — Reduzir includes em `player.hpp`
**Critérios:** < 15 includes diretos; build passa; zero mudança de comportamento
**Escopo:** M | **Deps:** Task 4.5

### Task 5.2 — `MountService`
Extrair 15 métodos de mount de `player.cpp`
```
src/creatures/players/components/mount_service.hpp/.cpp
tests/unit/players/components/mount_service_test.cpp
```
**Critérios:** `player.cpp` reduz ≥ 300 linhas; 2+ testes
**Escopo:** M | **Deps:** Task 5.1

### Task 5.3 — `InventoryService`
Extrair getInventoryItem, getAllInventoryItems, getWeapon, getEquippedItems
```
src/creatures/players/components/inventory_service.hpp/.cpp
tests/unit/players/components/inventory_service_test.cpp
```
**Critérios:** `player.cpp` reduz ≥ 400 linhas; 2+ testes
**Escopo:** M | **Deps:** Task 5.2

### Task 5.4 — Remaining Player subsystems
`PreyService` + `TrainingService` + `SessionService`
**Critérios:** `player.cpp` < 7k linhas
**Deps:** Task 5.3

**CHECKPOINT 6:** `player.cpp` < 7k linhas; Player = entidade de domínio ✓ Revisão humana

---

## Phase 6: ProtocolGame → Thin Adapter

### Task 6.1 — `INetworkCommandPort` (driving port)
Comandos tipados: `MoveCommand`, `AttackCommand`, `UseItemCommand`, `SayCommand`
```
src/server/network/network_command_port.hpp
src/server/network/commands/*.hpp
```
**Escopo:** M | **Deps:** Task 4.5

### Task 6.2 — Migrar packet handlers para command dispatchers
`parse*()` → deserializar → Command → dispatch via port
**Critérios:** `protocolgame.cpp` reduz ≥ 2k linhas; login/movimento funciona
**Escopo:** L | **Deps:** Task 6.1

**CHECKPOINT 7:** ProtocolGame < 7k linhas ✓ Revisão humana

---

## Phase 7: Singletons Restantes (44 → DI)

| Lote | Singletons | Risco |
|---|---|---|
| 7.1 Infrastructure | ThreadPool, OutputMessagePool, Webhook, Metrics, RSAManager | Baixo |
| 7.2 Game data | Spells, Monsters, Npcs, Weapons, Outfits, Familiars, Vocations, Imbuements, Chat | Médio |
| 7.3 IO → Repos | IOBestiary, IOBosstiary, IOPrey | Médio |
| 7.4 Scheduling | Dispatcher, SaveManager, EventsScheduler, GlobalEvents | Alto |

**CHECKPOINT 8 (FINAL):** Todos no DI; `ctest` 250+ testes; revisão final

---

## Riscos

| Risco | Impacto | Mitigação |
|---|---|---|
| Dependências circulares em `game.cpp` | Alto | Forward declarations; compilar a cada step |
| Player ↔ Game co-dependem em ~80 pontos | Alto | Injetar services em Player em vez de `g_game()` |
| Lua bindings dependem de tipos concretos | Alto | Manter wrappers de compatibilidade; não tocar Lua até Phase 6 |
| Merge conflicts — projeto ativo | Alto | Branches curtos (< 1 semana abertos) |

---

## Estimativa

| Fase | Esforço (1 dev) |
|---|---|
| 0 — Characterization Tests | 3–4 dias |
| 1 — IConfigManager | 3–4 dias |
| 2 — IDatabasePort | 2–3 dias |
| 3 — IPlayerRepository | 3–4 dias |
| 4 — Game Decomposition | 5–8 semanas |
| 5 — Player Domain | 3–5 semanas |
| 6 — ProtocolGame Adapter | 2–3 semanas |
| 7 — Remaining Singletons | 3–4 semanas |
| **Total** | **~4–5 meses** |

---

## Verificação End-to-End (após cada checkpoint)

```bash
cd build && cmake --build . --parallel $(nproc)
ctest --output-on-failure
# Smoke test manual: login → movimento → combate PvE → usar item → chat
```

---

## Phase 8: Correções Rápidas + Fundação de Testes de Services

### Task 8.1 — SaveManager: remover g_game()
`src/game/scheduling/save_manager.cpp`

SaveManager tem `Game& game_` via construtor mas ainda chama `g_game()` 3x (linhas 127, 160, 175).
Substituir por `game_`. Adicionar binding em `container.hpp`.

**Critérios:**
- [ ] Nenhum `g_game()` em save_manager.cpp
- [ ] SaveManager registrado no DI container
- [ ] Build sem erro; 258+ testes passando

**Escopo:** XS | **Deps:** Nenhuma

### Task 8.2 — Infraestrutura de testes para services
`tests/fixtures/` (novos helpers reutilizáveis)

- `GameStub` — stub mínimo de `IGame` para injeção
- Adaptar `InjectionFixture` para montar DI container com services isolados
- Garantir que CMakeLists.txt inclui novos stubs

**Critérios:**
- [ ] `GameStub` compila sem dependência de `game.cpp`
- [ ] `InjectionFixture` instancia service sem deps reais

**Escopo:** S | **Deps:** Nenhuma

### Task 8.3 — Testes para CombatService
`src/game/combat/combat_service.cpp` (1.484 linhas, 0 testes)
Novo: `tests/unit/game/combat/combat_service_test.cpp`

Cobrir: playerSetAttackedCreature, playerRequestFightModes, guards (creature nula, player deslogado).

**Critérios:**
- [ ] ≥ 10 casos de teste
- [ ] `ctest` verde

**Escopo:** M | **Deps:** 8.2

### Task 8.4 — Testes para ItemService
`src/game/items/item_service.cpp` (1.128 linhas, 0 testes)
Novo: `tests/unit/game/items/item_service_test.cpp`

Cobrir: playerMoveItem (ranges, peso, capacidade), playerRequestAddVip/EditVip.

**Critérios:**
- [ ] ≥ 8 casos de teste; `ctest` verde

**Escopo:** M | **Deps:** 8.2

### Task 8.5 — Testes para PlayerInteractionService
`src/game/interaction/player_interaction_service.cpp` (1.436 linhas, 0 testes)
Novo: `tests/unit/game/interaction/player_interaction_service_test.cpp`

Cobrir: playerUpdateContainer, playerOpenContainer, playerCloseNpcChannel.

**Critérios:**
- [ ] ≥ 8 casos de teste; `ctest` verde

**Escopo:** M | **Deps:** 8.2

**CHECKPOINT 8:** SaveManager limpo; ≥ 26 novos testes de services ✓

---

## Phase 9: IOLoginData Port + Adapter

### Task 9.0 — Mapear responsabilidades de IOLoginData
Catalogar grupos funcionais e dependências globais em `src/io/iologindata.cpp` (1.925 linhas).

**Critérios:**
- [ ] Lista de métodos por grupo (load/save/account/etc.)
- [ ] Lista de singletons globais usados

**Escopo:** XS | **Deps:** Nenhuma

### Task 9.1 — IPlayerRepository port expandido
`src/io/player_repository.hpp` — expandir interface existente com métodos de IOLoginData.

**Critérios:**
- [ ] Interface pura sem includes de DB

**Escopo:** S | **Deps:** 9.0

### Task 9.2 — SqlPlayerRepository adapter completo
`src/io/sql_player_repository.cpp/.hpp`

Extrai de IOLoginData: loadPlayer / savePlayer / playerExists. Injeta `IDatabase&`.

**Critérios:**
- [ ] Sem `Database::getInstance()` dentro das funções extraídas
- [ ] Registrado no DI container

**Escopo:** M | **Deps:** 9.1

### Task 9.3 — Testes para IPlayerRepository
`tests/unit/io/player_repository_test.cpp` + `InMemoryPlayerRepository` fixture

Cobrir: save/load round-trip, playerExists, campos críticos preservados.

**Critérios:**
- [ ] ≥ 12 casos de teste; `ctest` verde

**Escopo:** M | **Deps:** 9.1, 9.2

**CHECKPOINT 9:** IPlayerRepository completo; IOLoginData encolheu; ≥ 12 novos testes ✓

---

## Phase 10: Testes para Player Components

### Task 10.1 — Testes para StashComponent
`src/creatures/players/components/player_stash_component.cpp` (1.604 linhas, 0 testes)
Novo: `tests/unit/creatures/players/stash_component_test.cpp`

**Critérios:**
- [ ] ≥ 8 casos de teste (save, load, limit, add/remove)

### Task 10.2 — Testes para ForgeComponent
Novo: `tests/unit/creatures/players/forge_component_test.cpp`

**Critérios:**
- [ ] ≥ 6 casos de teste (attempt, resource validation, success/failure)

### Task 10.3 — Testes para DeathComponent
Novo: `tests/unit/creatures/players/death_component_test.cpp`

**Critérios:**
- [ ] ≥ 6 casos de teste (death penalty, bless reduction, skull)

**CHECKPOINT 10:** ≥ 20 novos testes de components ✓

---

## Phase 11: IOMarket e IOGuild

### Task 11.1 — IMarketRepository + SqlMarketRepository
`src/io/iomarket.cpp` → métodos estáticos → instância injetável.

**Critérios:**
- [ ] Sem chamadas estáticas ao DB em implementations; DI binding adicionado

### Task 11.2 — IGuildRepository + SqlGuildRepository
`src/io/ioguild.cpp` → mesmo padrão de 11.1.

**Critérios:**
- [ ] DI binding adicionado; sem `Database::getInstance()` em GuildRepository

**CHECKPOINT FINAL (8–11):** 50+ novos testes; SaveManager + IOMarket + IOGuild sem singletons globais ✓

---

## Phase 18: Dívidas pós-merge + DI restante

| Task | Escopo | Critérios |
|---|---|---|
| 18.1 ConfigManager instância única | S | `inject<IConfigManager>()` e `g_configManager()` retornam o mesmo objeto no container de produção; teste cobre |
| 18.2 DEBUG_LOG nos presets de teste | XS | `linux-release-enabled-tests` / `windows-release-enabled-tests` com `DEBUG_LOG=ON`; suíte unitária 100% |
| 18.3 Testes PlayerDeathComponent | S | ≥ 6 testes (death penalty, bless reduction, skull) sem crash por vocação nula |
| 18.4 ExpertPvp sem `g_game()` | M | dependências explícitas; comportamento inalterado; testes existentes + novos verdes |
| 18.5 Split protocolgame_send.cpp | L | nenhum arquivo de protocolo > 4k linhas; zero mudança de comportamento; CMake + vcxproj |
| 18.6 DI restante (Scripts, Metrics, Modules) | L | registrados no container ou bloqueio documentado com causa verificada |

**Verificação por task:** build `linux-release-enabled-tests`, `tools/merge/syntax_check.sh` nos .cpp tocados, `canary_ut` + `canary_it`. Commit por task.

---

## Phase 19: DI sobre singletons nas camadas já extraídas

| Task | Escopo | Critérios |
|---|---|---|
| 19.1 Services sem `g_game()`/`g_configManager()` | S | 0 chamadas em `*_service.cpp`; comportamento inalterado |
| 19.2 Components com config via porta | M | 0 `g_configManager()` em `player_*_component.cpp`; testes de component instalam config em memória |
| 19.3 IO estático → portas | M | 0 `IOLoginData::`/`IOMarket::` fora de `src/io/` e dos adapters `*_repository_db.cpp` |
| 19.4 GuildRepositoryDB dono do SQL | M | adapter usa `IDatabase` injetado; `IOGuild` vira fachada ou é removido; teste de integração |

**Verificação por task:** build `linux-release-enabled-tests`, `tools/merge/syntax_check.sh`, `canary_ut` + `canary_it`; contagem de singletons registrada no todo.

## Phase 20: Adapters donos do SQL + mundo injetado nos components do branch

> Linha de base: `g_game()` 1143 / `g_configManager()` 458 em src/*.cpp. Components: 183 `g_game()` — 93 em arquivos só do branch (forge 26, mount 11, death 11, creature_event 10, imbuement 9, session 6, experience 6, stash 5, combat_event 5, inventory 4) e 90 em arquivos que existem upstream (attached_effects 37, wheel 30, cyclopedia 7, title 6, achievement 4, badge 3, expert_pvp_world 3). Grep: `grep -c 'g_game()' src/creatures/players/components/*.cpp src/creatures/players/components/*/*.cpp`.

| Task | Escopo | Critérios |
|---|---|---|
| 20.1 Market: `MarketRepositoryDB(IDatabase&, IConfigManager&)` dono do SQL | M | `IOMarket` removido; estatísticas + mutex no adapter (singleton DI); refund de ofertas expiradas sai do IO para `game/market` (lógica de jogo), corpo movido sem reordenar update/refund; callback reagendado sem capturas de `this`/referência; testes de integração (MySQL) + unitários do refund |
| 20.2 VIP: `VipRepositoryDB(IDatabase&)` dono do SQL | S | métodos VIP saem de `IOLoginData`; testes de integração |
| 20.3 Components do branch sem `g_game()` | M | portas pequenas por papel (não um "mundo" gordo), resolvidas na chamada; metas por arquivo; arquivos que existem upstream ficam fora |

Fora de escopo: load/save de player em `IOLoginData` (~2000 linhas, upstream edita com frequência); catálogos `getAttachedEffects`/`getIOWheel` em arquivos upstream.

**Verificação por task:** build `linux-release-enabled-tests` (preset), `tools/merge/syntax_check.sh`, `canary_ut` + `canary_it`; contagens registradas no todo. Diff não-commitado de `connection.cpp`/teste de diagnóstico não é desta fase — commits por caminho explícito.

## Phase 21: `PlayerRepositoryDB` dono do SQL de nomes/saldo + cobertura do reembolso de venda

> Upstream `main` = fork `main` = merge-base `666a60f6f` (sem merge pendente em 2026-11-11).

| Task | Escopo | Critérios |
|---|---|---|
| 21.1 `PlayerRepositoryDB(IDatabase&)` | S | `getNameByGuid`, `getGuidByName`, `getGuidByNameEx`, `formatPlayerName`, `increaseBankBalance` movidos de `IOLoginData` idênticos (diff conferido); testes de integração (nome com aspas, busca com outra caixa devolve a grafia gravada, aumento de saldo, desconhecido → 0/false/vazio); README de merge + `tasks/AGENTS.md` atualizados |
| 21.2 Reembolso de oferta de venda | S | unit test: dono online recebe 150 empilháveis como 100 + 50, N não empilháveis como N itens, tier aplicado; nenhum acesso a MySQL/SaveManager (repositório em memória instalado); `ItemType` restaurado no TearDown |
| 21.3 Smoke semeado com oferta de venda | S | dono offline: carregar, entregar na inbox, salvar; linhas da inbox somam a quantidade; oferta de compra também passa pelo `increaseBankBalance` movido |

Fora de escopo: `gameWorldAuthentication` (lógica de autenticação, sem teste, fica em `IOLoginData`); `IOLoginData::getAccountType` (código morto, mas o arquivo é do upstream, então apagar só gera conflito); load/save de player. Defeito conhecido: `getGuidByNameEx` lê `g_game().groups`, e para tirar isso seria preciso alargar a porta.

## Phase 22: SQL restante dos services em adapters + estabilidade de highscores + testes de login

> Objetivo do fork: testável, estável, fácil de manter à mão (ver `tasks/AGENTS.md` §1).

| Task | Escopo | Critérios |
|---|---|---|
| 22.1 `players_online` | S | `IPlayerRepository::updateOnlinePlayers(guids)`; SQL + transação movidos de `CyclopediaService` para `PlayerRepositoryDB` sem mudar a lógica; unit test do service com repositório em memória; IT fora do `databaseTest` (o `BEGIN` do `DBTransaction` faz commit implícito da transação do harness), limpando as próprias linhas |
| 22.2 Highscores | M | porta `IHighscoreRepository` + `HighscoreRepositoryDB` (as 4 funções de query movidas com o mesmo nome/assinatura, para o `moved_merge.py` ainda casar); o adapter roda a query e entrega um `std::optional<HighscorePage>` já parseado; o callback diferido só captura valores (sai o `[this]`); a ordem player → reset da task → sem dados → cache é mantida; unit tests do service com fake; IT (empate divide rank, página "our rank", filtro de vocação, GM excluído e `total`, página 0) |
| 22.3 Testes de `gameWorldAuthentication` | S | só testes de integração do login (senha certa/errada, personagem de outra conta, conta inexistente) |
| 22.4 Caches de highscore com limite | S | entradas expiradas são removidas; chave com página/GUID do cliente não cresce sem limite; teste |

| 22.5 Highscores protegem o MySQL | S | página de entradas fresca no cache é servida sem query; intervalo mínimo de 1s por player; teto global de 8 queries em andamento (contador decrementa sempre, inclusive se o player saiu); métricas `highscore_requests_total`, `highscore_served_from_cache`, `highscore_requests_rejected{reason}`; testes com fake (relógio controlável). Motivo: `Database` usa uma única conexão com mutex, então highscores em fila atrasam saves |

Fora de escopo: SQL em arquivos que existem upstream (`player_forge_history`, `player_wheel`, `player_cyclopedia`, `player_badge`, `ban`, Lua `db_functions`). Cada movimento entra no mapa §5b do `tasks/AGENTS.md` e no README de merge no mesmo commit.

## Phase 23: medir e conter caminhos cliente → banco síncrono

Motivo: a conexão do `Database` é única e protegida por mutex. Uma query síncrona disparada por pacote do cliente para o loop principal para todos os jogadores e disputa a vez com os saves. Ordem: **medir primeiro** (instrumento de dispatch + inventário com dados semeados), **depois corrigir** (fazer menos trabalho → cache limitado → limite de taxa → assíncrono), sempre test-first e com linha na tabela de correções em arquivos upstream (§5b).

Casos já lidos no código:
- `BedItem::canUse`: usar a cama ocupada de uma casa carrega o dorminhoco inteiro do MySQL (`loadPlayerById`, ~20+ queries: inventário, depot, inbox, store inbox, recompensas, storages…) só para comparar nível de acesso. O uso de item é limitado por `timeBetweenActions` (200ms ≈ 5/s por player).
- Navegação do mercado (`MarketService::playerBrowseMarket*`): queries síncronas por pedido.

| Task | Escopo | Critérios |
|---|---|---|
| 23.0 Build com métricas | S | build tree `linux-release-metrics` (vcpkg com OpenTelemetry); os contadores da 22.4/22.5 compilam com `FEATURE_METRICS=ON`; documentar o uso |
| 23.1 Instrumento de dispatch | M | onde os pacotes são despachados: contador e histograma de latência rotulados só por opcode; aviso de dispatch lento, limitado por taxa (padrão `takeWriteErrorWarning`), com opcode, player, IP e ms; testes |
| 23.2 Inventário de fora para dentro | M | partindo dos pacotes: chamadas com `allowOffline = true` em `getPlayerByGUID`/`getPlayerByName`, `loadPlayerBy*`, repositórios síncronos. Para cada caminho: gatilho, limitador existente, número de queries (delta de `Questions` em teste de integração), se o cliente controla um multiplicador. Medido com dados semeados (dezenas de milhares de ofertas, um player cheio de itens) e `EXPLAIN` nas queries do mercado. `house.cpp:733` dentro de laço sobre lista do cliente? |
| 23.3 Correções priorizadas pela medição | M | cama primeiro, se a medição confirmar; mercado só se os números mostrarem custo |
| 23.4 Pendências da 22 | S | auditoria de irmãos (outros leitores de `getAccountPlayers`; `[this` em callbacks do dispatcher/`g_databaseTasks()` em arquivos da branch); vazamento de vaga do highscore se um callback se perder (`DatabaseTasks::store` sempre chama de volta?); seção 6 do AGENTS como estado atual |

Pendente com o usuário: testar highscores no cliente trocando filtros e páginas rápido (o intervalo responde "sem dados").

## Phase 24 (aberta em 2026-11-12): datapack atualizado a partir do TibiaWiki

Pedido do usuário (2026-11-12): depois do hardening, vasculhar o TibiaWiki em busca de ajustes no datapack (loot, drops e o resto) e atualizar nossos arquivos.

- **Fonte:** a API MediaWiki do TibiaWiki (`tibia.fandom.com/api.php`), com wikitext das infoboxes de criatura e das páginas de loot statistics. API em vez de raspar HTML; cache local; ~1 pedido/s; User-Agent genérico (sem dados do usuário).
- **Licença:** o conteúdo do TibiaWiki é CC BY-SA. Usamos fatos (chances, quantidades, status) e não texto; atribuição nos commits.
- **Processo:**
  1. Script em `tools/datapack/` gera um relatório de divergências por monstro: loot (item, chance, quantidade), HP, experiência, elementos, imunidades.
  2. Revisão humana do relatório.
  3. Correções em lotes pequenos em `data-otservbr-global/monster/`.
  4. Testes de carga do datapack e smoke boot a cada lote.
- **Cuidado com o merge:** o upstream também edita `data-otservbr-global`. Cada lote fica pequeno e por arquivo de monstro, e conflitos seguem a regra do §5b.

### Como o servidor usa o loot (base da comparação)

- `MonsterType:generateLootRoll` (`data/libs/functions/monstertype.lua`) rola **cada entrada de forma independente**: cai se `getLootRandom()` (0–100000 dividido pela taxa) for menor que `chance × fator × (0,95–1,05)`. `chance` está em 1/100000 (89920 = 89,92%).
- Quantidade de empilhável: uniforme em `[minCount, maxCount]` (padrão 1); item com cargas usa as cargas; os demais, 1.
- Entradas repetidas do mesmo item somam no mesmo resultado, então a chance de "o item aparecer" é `1 − Π(1 − p_i)`. É isso que se compara com o `times/kills` do wiki.
- Nome de loot resolve por `Item::items.nameToItems`: nome desconhecido gera aviso; nome com vários ids usa o primeiro e avisa. Uma adição só pode ser proposta se o nome do wiki resolver para um único item.

### Tasks

- **24.1** — Ferramenta de relatório em `tools/datapack/` (sem editar o datapack):
  - extrator `luajit` com stubs que carrega cada arquivo de monstro e emite JSON (nome, HP, XP, armor, mitigation, elementos, imunidades, loot com `id`/`name`/`chance`/`minCount`/`maxCount`/`child`); contagem conferida com o que o servidor registra;
  - coletor da API do MediaWiki (`redirects=1`, mapeia `normalized`/`redirects`, segue `continue`, separa `missing`, guarda revid e data de cada página), cache fora do repositório, ~1 pedido/s, User-Agent genérico;
  - parser dos blocos `Loot2` (por versão, comparada numericamente; o mais novo com kills suficientes; ignora `Empty`; aceita linha sem `amount`);
  - comparador: chance fora de um intervalo binomial em torno de `times/kills`; `maxCount` do datapack abaixo da maior quantidade observada (a observada é só um limite inferior); item faltando/sobrando; HP, XP, armor, elementos (`percent` do datapack = 100 − `DmgMod` do wiki, a confirmar em amostra). Campo que diverge na maioria dos monstros vira questão de unidade, não 1600 achados;
  - testes Python com fixtures sintéticas no formato do wiki; relatório gerado fora do repositório.
- **24.2** — Revisão do relatório pelo usuário. Nada é removido automaticamente (drops de evento, quest e do fork são legítimos); o wiki é editável por qualquer um, então revids fixos e limites para saltos grandes.
- **24.3** — Decidir com o usuário a estratégia de merge antes do primeiro lote (o upstream reescreve esses blocos de loot): de preferência um `--apply` idempotente que reaplica as correções sobre a versão do upstream. Registrar no §5b.
- **24.4** — Lotes pequenos de correção, com carga do datapack e smoke a cada lote.

## Phase 25 (aberta em 2026-11-14): massa de teste e swarm de bots

Pedido do usuário: várias contas e personagens em níveis e lugares diferentes, e um swarm de bots, para testar o
servidor antes da sessão com client real (pendências 0, 3 e 5 do §6d). Decisões: 200 personagens (50 contas × 4),
100 bots online; níveis 8–600 com mais gente em 8–150, vocações variadas, metade em templos e metade em áreas de
caça compatíveis com o nível; protocolo 15.25 já nesta fase.

- **Só ambiente descartável:** a massa vai para um banco `canary_loadtest_*` (nunca o banco do usuário), o servidor
  roda com config copiada para o scratchpad; o swarm só conecta em endereços de loopback (127.0.0.0/8) e cada
  grupo de bots sai de um `127.0.0.x` diferente, para respeitar os limites por IP em vez de afrouxá-los. Nada do
  repo `proxy-bot` é usado.
- **25.1** — Cliente 15.25 em Python (só biblioteca padrão): desafio moderno, primeira mensagem com RSA (sessão
  `conta\nsenha` com `authType = "password"`), framing moderno (blocos de 8, sequência, padding, XTEA), keepalive
  `0x1E`; a saída do servidor é só drenada (sem parsear o mapa). Provado com um bot logando e ficando online.
- **25.2** — Gerador da massa (`tools/loadtest/seed.py`): contas, personagens com nível/HP/mana/cap/skills coerentes,
  saldo, corda no slot de munição (para o "usar item com" a distância), posições dos templos (lidas do `.otbm`) e de
  centros de spawn (`otservbr-monster.xml`) compatíveis com o nível.
- **25.3** — Swarm (`tools/loadtest/swarm.py`): perfis (caçador com magias de área, andarilho, falador, comerciante
  de banco/talkaction, quem relogga, A* do "usar item com" a distância, pacotes do protocolo atual: digitando,
  quick loot, proficiência, highscores), ritmo humano, relatório e scrape do `/metrics`.
- **25.4** — Rodada de carga no build de métricas com banco descartável; relatório (latência por pacote, pacotes
  lentos, `query_latency`, pool de conexões, custo do A*).

## Phase 26 (aberta em 2026-11-13 como 25, renumerada no merge com a solid): issues abertas do upstream

Pedido do usuário (2026-11-13): anotar todas as issues abertas de `opentibiabr/canary`, verificar quais
acontecem de verdade e corrigir as reais. Trabalho na worktree `canary-upstream-issues` (branch
`nicollassantos/upstream-issues`, saída do HEAD `7e517fb00`). O `main` do upstream estava em `666a60f6f`,
a base do último merge: nenhuma correção do upstream ficou de fora.

Triagem (58 issues, só leitura + programa avulso): 9 reais, 4 parciais ou dependentes do mapa, 5 já
corrigidas, 9 não reproduzíveis ou não são bug, 7 decidíveis com teste/smoke, 14 só com client, 10 de
conteúdo/feature. Relatório completo e níveis de prova em `tasks/upstream_issues.md`.

Decisões do usuário (2026-11-13): porta do Oasis dá o storage 12107 a quem acha a cenoura; callbacks de
zona adiados só no login do player; opcionais aceitos (teto no saque, chaves duplicadas do startup, store e
preços, varredura do mapa); verificar o grupo "decidível com teste" antes de corrigir.

Regras: portar PR do upstream quando existir (merge limpo depois); cada task com build, `syntax_check`, ut +
it, commit próprio e linha no §5b do `tasks/AGENTS.md` quando tocar arquivo do upstream; Lua com
`luajit -bl`/`luac5.4 -p` e harness que falha no upstream.

### Tasks

- **26.1** — #4154 datas locais sem nanossegundos (porta do PR #4179: `tools.cpp` + teste).
- **26.2** — #4114 IDs do aviso de avanço de skill (porta do PR #4181 para os components).
- **26.3** — #4153 byte de motivo no `0x14` para clients 15.23+ (helper único + teste dos bytes).
- **26.4** — #4125 reset de charm lê só os campos enviados (teste).
- **26.5** — #3478 freequests (Secret Library, Barbarian Test) + harness contra o catálogo do questlog.
- **26.6** — #3453 porta do Oasis (storage 12107) + chave `[12107]` duplicada no startup.
- **26.7** — #3513 callbacks de zona depois do pacote de entrada no login do player.
- **26.8** — Chaves duplicadas nas tabelas de startup: conferir cada uma + check que impede a repetição.
- **26.9** — #3742 saldo do store enviado uma vez; #2083 preços reenviados aos online após o recálculo.
- **26.10** — Varredura do `.otbm` (só leitura) para #4091, #4057 e #3438.
- **26.11** — Verificação do grupo decidível: #3584/#3426 (campo/bomba), #3745 (aljava), #2553/#3549 (hazard), #3059 (`/n`).
- **26.12** — #3374 medir o saque grande e pôr teto (valor escolhido pelo usuário).
- **CHECKPOINT 26** — build, ut/it, preset de métricas, smoke boot com data-otservbr-global.

## Phase 27 (aberta em 2026-11-19): PRs abertas do upstream (#4102, #4184)

Pedido do usuário (2026-11-19): verificar se as PRs #4183, #4184 e #4102 do `opentibiabr/canary` são pertinentes e
se já existem aqui; implementar e testar as pertinentes, mantendo o padrão da branch. Nenhuma das três entrou no
upstream (`main` ainda em `666a60f6f`). Worktree `canary-upstream-prs` (branch `nicollassantos/upstream-prs`, saída
da `solid` em `b20d80830`).

Decisão do usuário: **#4183 (Grave Danger) ignorada**. Motivo do veredito: contraria o §6e ("siga a wiki": a PR mantém
Count Vlarkorth com 75000 de vida, o wiki e a branch têm 300000; põe 300000/150000 em Earl Osam/Sir Nictros, que o
wiki marca `?`), é aproximação declarada (~86%), sem validação em jogo e depende de uma API de mundos que não temos.
Do #4184 só entram as correções de bug; o conteúdo (bundles, 179 ofertas de casa, `items.xml`, imagens externas,
binding `hasOutfit`) espera o merge do upstream. `kit:setOwner(player)` (kit preso ao comprador) fica fora: muda
regra de jogo sem fonte.

Arquitetura: binding Lua e scripts do store são adaptadores finos; a lógica nova vai para unidade pequena com teste.
Sem porta de filesystem (um só ponto de uso, não pagaria com teste). Nomes e contrato das PRs mantidos
(`fsCreateDirectories`, `FS.mkdir`) para o merge com o upstream sair limpo.

### Tasks

- **27.1** — #4102 núcleo: `src/utils/directory_creation.{hpp,cpp}` (`createDirectories(path)` → erro opcional;
  `std::filesystem::create_directories` com `error_code` e a exceção da conversão do caminho capturada, item aberto
  da revisão do upstream) + gtest em diretório temporário (aninhado, já existe, nomes com metacaracteres de shell,
  arquivo no caminho, byte inválido). CMake, vcxproj, lista de testes.
- **27.2** — #4102 Lua: `fsCreateDirectories` em `global_functions.cpp` só chamando o núcleo; `fs.lua` e
  `tests/lua/test_fs.lua` da PR (com reload do core); o teste falha contra o `fs.lua` atual; smoke cria
  `reports/bugs/<nome>/`.
- **27.3** — #4184 capacidade: `processHouseRelatedPurchase` passava a quantidade como id ao
  `canReceiveStoreItems` (checava 1 vaga com 36 ofertas entregando mais de um kit). Teste que falha antes em
  `tests/lua/test_gamestore_furniture_deliveries.lua`: 5 kits com 3 vagas → recusa, nada entregue, nada cobrado.
- **27.4** — #4184 tudo ou nada: kit que falha remove os já criados; pagamento que falha depois da entrega desfaz a
  entrega (handler devolve `rollback`). Testes: falha no meio, pagamento falho, compra normal.
- **CHECKPOINT 27** — build, ut/it, `syntax_check`, `luajit tests/lua/test_*.lua`, harnesses de `tools/lua`, smoke.

## Phase 28 (proposta em 2026-11-19): ban por IP sem punir inocentes (CGNAT) e IPv6

Pedido do usuário (2026-11-19): "vale a pena suportar IPv6 com fallback para IPv4?". Motivo dado: as operadoras usam
CGNAT e um ban por IP pode banir jogadores inocentes. Produção tem IPv6; o OTClient (`../otclient`) pode ser alterado.

**Objetivo real:** ban por endereço que acerte só o infrator. O IPv6 resolve isso só para quem conecta por IPv6 (um /64
costuma ser uma casa ou um aparelho); quem continua só com IPv4 atrás de CGNAT segue dividindo IP com centenas de
pessoas. Por isso a fase tem dois trilhos: proteger o ban de IPv4 compartilhado (vale já, sem IPv6) e aceitar IPv6
com a identidade por prefixo.

**Estado hoje (lido em 2026-11-19):**
- listener só IPv4 (`src/server/server.cpp:157-159`, `address_v4`); IP do cliente é `uint32_t`
  (`Connection::getIP`, `to_v4()`), usado em ~31 arquivos (ban e taxa de conexão, `IpConnectionLimiter`,
  `ProtocolStatus`, `log_budget`, `packet_watch`, livestream, `ProtocolSessionHintStore`, login);
- banco: `ip_bans.ip int(11)` e `players.lastip int unsigned` (schema do upstream, só IPv4);
- `/ipban` (GOD, 7 dias, `data/scripts/talkactions/god/ip_ban.lua`) bane o `lastip` do personagem sem olhar se o IP é
  compartilhado; o login recusa qualquer conta do IP (`LoginService::refuseBannedAddress`);
- **bug achado (2026-11-19, conferido num banco descartável com o `sql_mode` estrito do projeto):** `ip_bans.ip` é
  `int(11)` **com sinal** e recebe o IPv4 em ordem de rede (`htonl`); todo IP com último octeto ≥ 128 passa de 2³¹
  (ex.: `200.150.10.200` → 3356137160) e o `INSERT` falha com `ERROR 1264 Out of range`. Metade dos IPs não pode ser
  banida, e o `/ipban` ignora o erro e responde "has been IP banned";
- CGNAT não é detectável pela faixa (o servidor vê o IP público do NAT); o sinal disponível é quantas contas usam o IP;
- clientes: o 8.60 recebe o IP do mundo como número IPv4 (nunca IPv6); o moderno recebe texto (hostname serve). O
  OTClient tenta só o primeiro endereço resolvido (`otclient/src/framework/net/connection.cpp:97-111`, sem fallback) e
  `getIp()` chama `to_v4()` (lança numa conexão IPv6). Suporte do client oficial 15.25 a IPv6: **sem fonte**, testar.
  `login-server` (HTTP do 15.25) é imagem externa: ver se consulta `ip_bans`.

**Regra de segurança da fase:** em IPv6 todo limite e todo ban contam por prefixo /64, nunca por endereço (um VPS
recebe um /64 inteiro; contar por endereço desliga a defesa do fork). Listener IPv6 desligado por padrão até os
limites estarem migrados.

### Tasks

Trilho 1 — IPv4 (independe de IPv6):
- **28.0** — Coluna do ban: `ip_bans.ip` e `players.lastip` viram `DECIMAL(39,0) UNSIGNED` (D1). O IPv4 continua o
  mesmo número (consultas do upstream, do `login-server` e do MyAAC seguem iguais) e cabe um IPv6 de 128 bits. Feito
  no boot pelo `schema_hardening`, idempotente: só altera quando a coluna ainda é inteira; tipo inesperado (o upstream
  mudou) é reportado e não mexe. `/ipban` passa a conferir o resultado do `INSERT`. Teste de integração (MySQL): IP
  com último octeto ≥ 128 é banido e recusado no login; dois boots seguidos não mudam nada. (S)
- **28.1** — `/ipban` mostra quantas contas usaram o IP como último IP (e quantas estão online nele); com 2 ou mais,
  recusa sem confirmação explícita (`/ipban nome,force`). Teste Lua (harness) com o banco simulado.
  Aceite: banir um IP de uma conta só funciona como antes; IP com várias contas exige `force` e o log do comando
  registra a contagem. (S)

Trilho 2 — IPv6:
- **28.2** — `ClientAddress` (`src/server/network/connection/client_address.{hpp,cpp}`): 16 bytes, IPv4 guardado como
  endereço mapeado; `isV4()`, `v4()` (o `uint32_t` de hoje), `limitKey()` (IPv4 inteiro ou prefixo /64), `toString()`.
  gtest: mapeamento ida e volta, mesmo /64 = mesma chave, /64 vizinho = outra. Sem mudança de comportamento. (S)
- **28.3** — `Connection` guarda o `ClientAddress`; `getIP()` continua devolvendo o IPv4 (0 para IPv6) para os
  consumidores antigos; novo `getAddress()`. Endereço `::ffff:a.b.c.d` vira IPv4. (S)
- **28.4** — Limites por `limitKey()`: `IpConnectionLimiter`, taxa do `Ban::acceptConnection`, intervalo do
  `ProtocolStatus`, teto por IP do livestream. gtest: 100 endereços do mesmo /64 contam como um. (M)
- **28.5** — Log e diagnóstico com endereço textual (`log_budget`, `packet_watch`, avisos de recusa) e
  `ProtocolSessionHintStore` pela chave. (M)
- **28.6** — Listener opcional nos dois protocolos (`bindIpv6`, padrão `false`; socket v6 com `v6_only=false`), com
  `bindOnlyGlobalAddress` aceitando endereço IPv6. Smoke com sockets reais em `127.0.0.1` e `::1`: login do 8.60 por
  IPv4 igual a hoje, teto de pendentes por /64 em `::1`. (S)
- **28.7** — Ban de IPv6 por /64 na mesma coluna (`ip_bans.ip` guarda o prefixo /64 como inteiro de 128 bits; não
  colide com IPv4, que fica abaixo de 2³²), `LoginService::refuseBannedAddress` pelo `ClientAddress`,
  `/ipban`/`/unban` com IPv6 (Lua lê a coluna como texto: número de 128 bits não cabe em `double`), último endereço
  IPv6 em `players.lastip`. Teste de integração (MySQL). (M)
- **28.8** — OTClient (`../otclient`, fora deste repositório): tentar todos os endereços resolvidos (IPv6 e depois
  IPv4) com timeout curto por tentativa; `getIp()` sem `to_v4()`. Prova contra o servidor em `::1` e `127.0.0.1`. (M)
- **28.9** — Teste com o client oficial 15.25 por hostname com A + AAAA; conferir o `login-server`. Só depois disso
  publicar o AAAA em produção. (S, manual)

Checkpoints: depois de 28.1 (pode ir para produção sozinho); depois de 28.2–28.5 (ut/it, smoke IPv4 igual a hoje);
depois de 28.6–28.7 (smoke `::1` + MySQL); depois de 28.8–28.9 (clients reais).

### Decisões (2026-11-19)
- **D1** — Mesmas colunas, com o tipo ampliado (pedido do usuário). Tipo `DECIMAL(39,0) UNSIGNED`, e não `VARBINARY`:
  com binário, toda consulta do upstream que compara a coluna com número deixaria de casar sem erro depois de um
  merge. Mecanismo: `schema_hardening` no boot (amplia a regra de "só índices" para "ampliar tipo de coluna, nunca
  reduzir"), sem migration numerada e sem editar `schema.sql`. **A confirmar com o usuário antes da 28.0.**
- **D2** — `/ipban` em IP de várias contas avisa e exige `force`.
- **D3** — Ban de IPv6 por /64 fixo.

### Riscos
| Risco | Impacto | Mitigação |
|---|---|---|
| Limite contado por endereço em IPv6 | Alto: defesa contra bots desligada | listener IPv6 só depois da 28.4; teste por /64 |
| AAAA publicado antes dos clients | Alto: login quebra no OTClient atual | 28.8 e 28.9 antes de publicar |
| Client oficial sem IPv6 | Médio | ele usa o A; testar na 28.9 |
| Conflito de merge (~31 arquivos do upstream) | Médio | `getIP()` mantido; mudança por receptor; linhas no §5b |
| Upstream muda `ip_bans`/`lastip` depois | Médio | `schema_hardening` só amplia de inteiro para `DECIMAL`; outro tipo é reportado e não mexe |
| Ferramenta externa lê IPv6 como número de 64 bits | Baixo | só linhas IPv6 (≥ 2⁶⁴); conferir `login-server` e MyAAC na 28.9 |
