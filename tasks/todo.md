# TODO — Refatoração SOLID + Hexagonal

> Atualizar status: `[ ]` pending → `[~]` in progress → `[x]` done

---

## Phase 0: Baseline de Testes

- [x] **0.1** — Verificar build limpo + 156 testes passando (110/120 passam; 10 falhas = DB integration sem MySQL — esperado)
- [x] **0.2** — Characterization tests `game.cpp`: `game_movement_it.cpp` (4 testes) + `game_items_it.cpp` (6 testes)
- [x] **0.3** — Characterization tests `player.cpp`: `player_stats_behavior_test.cpp` (9 testes) + `player_inventory_test.cpp` (6 testes)
- [x] **CHECKPOINT 0** — 27 novos testes passando (100%) ✓

## Phase 1: IConfigManager Port

- [x] **1.1** — Criar `src/config/config_port.hpp` (IConfigManager) + herança em ConfigManager
- [x] **1.2** — `tests/fixture/config/in_memory_config_manager.hpp` + binding DI em `container.hpp` + 7 testes unitários
- [x] **CHECKPOINT 1** — 253/253 testes passando; IConfigManager injetável ✓

## Phase 2: IDatabasePort

- [x] **2.1** — Criar `src/database/database_port.hpp` (IDatabase) + herança em Database + binding DI
- [x] **CHECKPOINT 2** — 253/253 testes passando; IDatabase injetável ✓

## Phase 3: IPlayerRepository

- [x] **3.1** — Criar `IPlayerRepository` port + `PlayerRepositoryDB` adaptador (delega IOLoginData)
- [x] **3.2** — `InMemoryPlayerRepository` + 5 testes unitários (save/load por name/id, reset)
- [x] **CHECKPOINT 3** — 258/258 testes passando; IPlayerRepository injetável ✓

## Phase 4: Game Decomposition

- [x] **4.1** — `MovementService` (src/game/movement/) — 258/258 testes passando; game.cpp = 12269 linhas
- [x] **4.2** — `CombatService` (src/creatures/combat/) — 258/258 testes; game.cpp = 10881 linhas (−1388)
- [x] **4.3** — `ItemService` (src/items/) — 258/258 testes; game.cpp = 9965 linhas (−934)
- [ ] **4.4** — `TradeService` + `IMarketRepository` + `MarketRepositoryDB` + testes
- [ ] **CHECKPOINT 4** — `game.cpp` < 8k linhas; servidor funcional → revisão humana
- [ ] **4.5a** — `HighscoreService`
- [ ] **4.5b** — `CreatureManagementService`
- [ ] **4.5c** — `SchedulingService`
- [ ] **4.5d** — `SoundService`
- [ ] **CHECKPOINT 5** — `game.cpp` < 4k linhas → revisão humana

## Phase 5: Player Domain Puro

- [ ] **5.1** — Reduzir includes em `player.hpp` (< 15 diretos)
- [ ] **5.2** — `MountService` (src/creatures/players/components/) + 2 testes
- [ ] **5.3** — `InventoryService` + 2 testes
- [ ] **5.4a** — `PreyService`
- [ ] **5.4b** — `TrainingService`
- [ ] **5.4c** — `SessionService`
- [ ] **CHECKPOINT 6** — `player.cpp` < 7k linhas → revisão humana

## Phase 6: ProtocolGame → Thin Adapter

- [ ] **6.1** — `INetworkCommandPort` + comandos tipados
- [ ] **6.2** — Migrar packet handlers → command dispatchers
- [ ] **CHECKPOINT 7** — `protocolgame.cpp` < 7k linhas → revisão humana

## Phase 7: Singletons Restantes

- [ ] **7.1** — Infrastructure: ThreadPool, OutputMessagePool, Webhook, Metrics, RSAManager
- [ ] **7.2** — Game data: Spells, Monsters, Npcs, Weapons, Outfits, Familiars, Vocations, Imbuements, Chat
- [ ] **7.3** — IO → Repositórios: IOBestiary, IOBosstiary, IOPrey
- [ ] **7.4** — Scheduling: Dispatcher, SaveManager, EventsScheduler, GlobalEvents
- [ ] **CHECKPOINT 8 (FINAL)** — Todos no DI; 250+ testes passando → revisão final
