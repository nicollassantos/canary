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
