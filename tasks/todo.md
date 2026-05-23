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
- [x] **4.4** — `TradeService` (src/game/trade/) + `MarketService` (src/game/market/) — 258/258 testes; game.cpp = 8790 linhas (−1175)
- [x] **CHECKPOINT 4** — game.cpp = 7841 (< 8k); 258/258 testes ✓
- [x] **4.5a** — `HighscoreService` (src/game/highscore/) — 258/258 testes; game.cpp = 8578 linhas (−212)
- [x] **4.5b** — `CreatureManagementService` (src/creatures/management/) — 258/258 testes; game.cpp = 8312 linhas (−266)
- [x] **4.5c** — `SchedulingService` — N/A (dispatcher calls são scattered; extraído como wrapper inviável)
- [x] **4.5d** — `SoundService` (src/game/sound/) — 258/258 testes; game.cpp = 8280 linhas (−32)
- [x] **4.5e** — `MovementService` playerMoveItem + `ItemService` isTryingToStow — 258/258 testes; game.cpp = 7841 linhas (−439)
- [x] **4.5f** — `OutfitService` (src/game/outfit/) + `CyclopediaService` (src/game/cyclopedia/) + playerCheckActivity → InteractionService — 258/258 testes; game.cpp = 3986 linhas (−3855)
- [x] **CHECKPOINT 5** — game.cpp = 3986 linhas (< 4k) ✓; fix InternalGame → GameHelpers header; ambos presets compilam; 258/258 testes ✓

## Phase 5: Player Domain Puro

- [ ] **5.1** — Reduzir includes em `player.hpp` (< 15 diretos)
- [x] **5.2** — `PlayerMountComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 12959 linhas (−161)
- [x] **5.3** — `PlayerInventoryComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 12826 linhas (−133)
- [x] **5.4b** — `PlayerTrainingComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 12691 linhas (−135)
- [x] **5.4a** — `PlayerPreyComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 12454 linhas (−237)
- [x] **5.4d** — `PlayerStashComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 10984 linhas (−1470); stash_component.cpp = 1604 linhas
- [x] **5.4e** — `PlayerForgeComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 10205 linhas (−779); forge_component.cpp = 933 linhas
- [x] **5.4f** — `PlayerExperienceComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 9836 linhas (−369); experience_component.cpp = 432 linhas
- [x] **5.4g** — `PlayerImbuementComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 9473 linhas (−363); imbuement_component.cpp = 463 linhas
- [x] **5.4h** — `PlayerDeathComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 9106 linhas (−367); death_component.cpp = 415 linhas
- [x] **5.4i** — `PlayerCylinderComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 8437 linhas (−669); cylinder_component.cpp = 470 linhas
- [x] **5.4j** — `PlayerCombatStatsComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 7871 linhas (−566); combat_stats_component.cpp = 700 linhas
- [x] **5.4k** — `PlayerCreatureEventComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 7618 linhas (−253); creature_event_component.cpp = 345 linhas
- [x] **5.4l** — `PlayerCombatEventComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 6992 linhas (−626); combat_event_component.cpp = 749 linhas (+4 hazard methods)
- [ ] **5.4c** — `SessionService`
- [x] **CHECKPOINT 6** — `player.cpp` = 6992 linhas (< 7k) ✓; 258/258 testes → revisão humana

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
