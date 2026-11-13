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

- [x] **5.1** — Reduzir includes em `player.hpp` — 35 → 9 diretos (< 15); componentes agregados em `player_components.hpp`; 258/258 testes ✓
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
- [x] **5.4c** — `PlayerSessionComponent` (src/creatures/players/components/) — 258/258 testes; player.cpp = 6956 linhas (−36); session_component.cpp = 96 linhas
- [x] **CHECKPOINT 6** — `player.cpp` = 6992 linhas (< 7k) ✓; 258/258 testes → revisão humana

## Phase 6: ProtocolGame → Thin Adapter

- [x] **6.1** — `INetworkCommandPort` + `GameCommandDispatcher` + parseAutoWalk/parseAttack/parseFollow → command pattern
- [x] **6.2** — Split `protocolgame_send.cpp` (7112 linhas) — protocolgame.cpp = 3396 linhas (−7345); 258/258 testes ✓
- [x] **CHECKPOINT 7** — `protocolgame.cpp` = 3396 linhas (< 7k) ✓; 258/258 testes → revisão humana

## Phase 7: Singletons Restantes

- [x] **7.1** — ThreadPool, OutputMessagePool, Webhook, RSAManager → DI singleton; metrics::Metrics auto-wires (no-op variant circular dep); 258/258 testes ✓
- [x] **7.2** — Spells, Monsters, Npcs, Weapons, Outfits, Familiars, Vocations, Imbuements, Chat → DI singleton; 258/258 testes ✓
- [x] **7.3** — IOBestiary, IOBosstiary, IOPrey → DI singleton; 258/258 testes ✓
- [x] **7.4** — Dispatcher, EventsScheduler, GlobalEvents → DI singleton; SaveManager skipped (needs Game& — game.hpp adds 22+ transitive includes to container.hpp); EventsScheduler::getInstance moved to .cpp; 258/258 testes ✓
- [x] **7.5** — Lua subsystems: Actions, CreatureEvents, Events, MoveEvents, TalkActions → DI singleton; Scripts skipped (holds LuaScriptInterface by value, exceeds boost::di 10-param limit); 258/258 testes ✓
- [x] **CHECKPOINT 8 (FINAL)** — 32 singletons registered; 258/258 testes ✓; remaining: SaveManager (Game& dep), Scripts (LuaScriptInterface value member), metrics::Metrics (no-op circular dep), Modules (circular dep)

## Phase 8: Correções Rápidas + Testes de Services

- [x] **8.1** — SaveManager: substituir 3x `g_game()` por `game_`; registrado no DI container (container.hpp)
- [x] **8.2** — Container::Container(2-param) bug fix: agora define `m_maxItems` corretamente (sem fallback nulo)
- [x] **8.3** — Testes para CombatService (5 casos: blockHit COMBAT_NONE, healing positive, agony, fight modes, setAttackedCreature)
- [x] **8.4** — Testes para ItemService (8 casos: internalAddItem ok/null, internalRemoveItem, findItemOfType, playerMoveItem guard)
- [x] **8.5** — Testes para PlayerInteractionService (8 casos: closeContainer, moveUp, updateContainer, rotateItem, browseField, writeItem, stashWithdraw guards)
- [x] **CHECKPOINT 8** — 297/297 testes ✓; SaveManager limpo; 39 novos testes adicionados

## Phase 9: IOLoginData Port + Adapter

- [x] **9.0** — Mapear grupos funcionais e singletons em IOLoginData
- [x] **9.1** — IPlayerRepository port expandido (getGuidByName, getNameByGuid, increaseBankBalance)
- [x] **9.2** — PlayerRepositoryDB adapter; 15 call sites migrados de IOLoginData:: para g_playerRepository()
- [x] **9.3** — 8 novos testes para IPlayerRepository; bankBalances map + reset() fix
- [x] **CHECKPOINT 9** — 305/305 testes ✓; IPlayerRepository completo

## Phase 10: Testes de Player Components

- [x] **10.1** — StashComponent (8 testes: addItemOnStash, getStashItemCount, getStashItems)
- [x] **10.2** — ForgeComponent (8 testes: forgeDusts/forgeDustLevel set/add/remove/clamp) + fix underflow uint64
- [x] **10.3** — TrainingComponent (7 testes: offlineTrainingTime/Skill); DeathComponent skip: isPromoted() crashs com vocation=null no default Player
- [x] **CHECKPOINT 10** — 23 novos testes; 328/328 ✓

## Phase 11: IOMarket e IOGuild

- [x] **11.1** — IMarketRepository + MarketRepositoryDB; todos call sites CRUD migrados (game.cpp, market_service.cpp, protocolgame_send.cpp); InMemoryMarketRepository fixture; 14 testes TDD
- [x] **11.2** — IGuildRepository + GuildRepositoryDB; 10 call sites migrados; InMemoryGuildRepository fixture; 8 testes
- [x] **CHECKPOINT 11** — 350/350 testes ✓; IMarketRepository + IGuildRepository + IPlayerRepository + IConfigManager + IDatabase portas criadas; all adapters injectable

## Phase 12: Cobertura Adicional (TDD contínuo)

- [x] **12.1** — Bank: inject IConfigManager + 14 testes (credit/debit/transferTo/town check/denied names)
- [x] **12.2** — PlayerAchievement: 10 testes (isUnlocked, addPoints/removePoints, clamp, per-player isolation)
- [x] **12.3** — PlayerVIP: 17 testes (getMaxEntries/Groups, exists/addInternal, getFreeId, groups CRUD)
- [x] **12.4** — PlayerTitle: 9 testes (isTitleUnlocked, getCurrentTitle/setCurrentTitle KV, getNameBySex)
- [x] **CHECKPOINT 12** — 400/400 testes ✓

## Phase 13: Cobertura de Componentes de Player (TDD contínuo)

- [x] **13.1** — PlayerBadge: 8 testes (hasBadge, loyalty, accountAge, tournamentParticipation/Points)
- [x] **13.2** — PlayerForgeHistory: 8 testes (get empty, add/stores/multiple/dedup-timestamp, remove/correct/noop)
- [x] **13.3** — PlayerInventoryComponent: 18 testes (getInventoryItem, getInventoryItemsId, getInventoryItemsFromId, getAllInventoryItems, getEquippedItems; Cylinder base cast trick)
- [x] **13.4** — PlayerMountComponent: 13 testes (getCurrentMount/setCurrentMount, isRandomMounted/setRandomMount, getLastMount kv/storage, hasMount bitflag)
- [x] **13.5** — PlayerCreatureEventComponent: 12 smoke tests (guard paths, trade/loot early returns, client=null guards)
- [x] **13.6** — PlayerImbuementComponent: 15 smoke tests (client guards, clearAllImbuements, addItemImbuementStats zero-imbuement, updateImbuementTrackerStats, onApplyImbuement/onClearImbuement null guards)
- [x] **13.7** — PlayerCombatStatsComponent: 20 testes (attackRawTotal, attackTotal 3 fight modes, getDistanceAttackSkill, calculateFlatDamageHealing, getCombatTacticsMitigation, getMantra, getShieldAndWeapon)
- [x] **13.8** — PlayerCombatEventComponent: 12 smoke tests (isPzLocked, hasShield, hazardSystemPoints, onAttackedCreatureBlockHit, onAddCondition, onCleanseCondition, onIdleStatus)
- [x] **13.9** — PlayerCylinderComponent: 9 testes (getFirstIndex, getLastIndex, getThingIndex, getItemTypeCount)
- [x] **13.10** — PlayerSessionComponent: 8 testes (setProtection/isProtected, setLoginProtection/isLoginProtected/resetLoginProtection, canLogout no tile)
- [x] **13.11** — WeaponProficiency: 21 testes (addStat/getStat/resetStats, addSkillBonus/getSkillBonus/resetSkillBonuses, powerfulFoe, specializedMagic, getBosstiaryExperience, getBestiaryExperience, getAutoAttackCritical)
- [x] **13.12** — WheelGemUtils: 11 testes (getHealthValue/getManaValue/getCapacityValue per vocation; unknown modifier → 0)
- [x] **13.13** — ItemAttribute: 13 testes (hasAttribute, getAttributeValue, getAttributeString, removeAttribute)
- [x] **13.14** — CustomAttribute: 10 testes (constructors int64/string/double/bool, hasValue, getAttribute, setValue)
- [x] **13.15** — Area (zone geometry): 13 testes (contains, intersects static/instance, PositionIterator)
- [x] **13.16** — vector_sort: 11 testes (empty/size, contains, sorted order, erase, erase_if, clear)
- [x] **13.17** — arraylist: 11 testes (empty/size, push_front order, contains, erase, erase_if, clear)
- [x] **13.18** — Condition getters: 10 testes (getId/getType/getTicks/getSubId, isPersistent 3 cases, isRemovableOnDeath 3 cases)
- [x] **CHECKPOINT 13** — 576/576 testes ✓

## Phase 14: Merge main upstream (6 commits since v3.6.0)

- [x] **14.1** — Verificar 576/576 testes passando antes do merge
- [x] **14.2a** — `creature.hpp`: adicionar `WalkStartPolicy` enum; atualizar assinaturas `getEventStepTicks`, `startAutoWalk`, `addEventWalk`
- [x] **14.2b** — `creature.cpp`: aplicar 3 hunks do first-step fix (startAutoWalk, addEventWalk body, getEventStepTicks logic)
- [x] **14.2c** — `game.cpp`: playerMove + forcePlayerMove → passar `ImmediateWhenReady` como 3º arg
- [x] **14.2d** — `movement_service.cpp`: playerAutoWalk + forcePlayerAutoWalk → usar policy single vs multi-step
- [x] **14.3** — DB perf (0c02da916): aplicar hunks matching em `iologindata.cpp`, `iologindata_save_player.cpp`, `database.cpp`
- [x] **14.4** — Market sort (e6d575bed): `ORDER BY price + created` em `iomarket.cpp`
- [x] **14.5** — Aceitar wholesale: mazarius.lua, docs, CI, docker
- [x] **14.6** — Build + 595/595 testes passando (576 → 595 com WalkStartPolicy + 15 outros novos)
- [x] **14.7** — Testes TDD para WalkStartPolicy (4 casos: EnumValues_AreDistinct, RespectDelay, ImmediateWhenReady, ordering invariant)
- [x] **14.8** — Commit merge com todo.md atualizado
- [x] **CHECKPOINT 14** — 595/595 testes ✓; merge integrado

## Phase 15: Cobertura de Services (TDD contínuo)

- [x] **15.1** — MovementService: 6 testes (internalMoveCreature null/baseSpeed0/rooted; internalTeleport null/samePos/removed)
- [x] **15.2** — TradeService: 8 testes (getTradeErrorDescription 6 casos; internalStartTrade alreadyTrading/storeItem)
- [x] **15.3** — HighscoreService: 8 testes estáticos (calculateHighscorePages 3 casos; getSkillNameById 5 casos incl. unknown→reset)
- [x] **15.4** — Creature walk behavior: 4 testes (startAutoWalk empty/rooted/ignoreConditions/multi-step LIFO)
- [x] **15.5** — CreatureManagementService: 18 testes (getMonsterByID 0+unknown; getNpcByID 0+unknown; getPlayerByID not registered; getCreatureByName empty+unknown; getNpcByName empty+unknown; getPlayerByName empty+not registered+registered; getPlayerByGUID 0+unknown; getPlayerByNameWildcard empty+too long+exact unknown+wildcard prefix unknown)
- [x] **15.6** — fix WalkStartPolicy segfault: `makeReadyPlayer()` now sets `Group` before `setSpeed(0)` (Player::getStepSpeed → hasFlag crashes on null group)
- [x] **15.7** — ChatService: 7 testes (playerBroadcastMessage no-flag/with-flag; playerYell level1/YELLTICKS/level10-ok; playerSpeakTo unknown-receiver/registered-receiver)
- [x] **15.8** — ForgeService: 4 testes (addInfluencedMonster null; makeInfluencedMonster/makeFiendishMonster empty monsters; makeFiendishMonster createForgeable=true no monsters)
- [x] **15.9** — LootService: 5 testes (getObjectCategory null→NONE; internalCollectManagedItems null-player/null-item→NOTPOSSIBLE; findManagedContainer no-containers/no-backpack→nullptr)
- [x] **CHECKPOINT 15** — 655/655 testes ✓; +60 novos testes (total desde Phase 0)

## Phase 16: Merge main upstream (10 commits since Phase 14)

- [x] **16.1** — Verificar 655/655 testes passando antes do merge
- [x] **16.2** — Aceitar wholesale: CI/workflows (b3d9188cb, 3b9b6f0c6, bce1f6308), vcpkg (5aeca4f6d), YAML docs (d415619c8)
- [x] **16.3** — Forge fix (bd6217f37): `exclude` param em `PlayerForgeComponent::getForgeItemFromId` + call sites + player.cpp delegate + player.hpp
- [x] **16.4** — NPC protocol (5ee38ce90): `SPEECHBUBBLE_SAILOR/BANKER` em creatures_definitions.hpp + lua_enums.cpp; `playerNpcGreet` else-if; `AddPlayerStats` uint16 level-percent (protocolgame_send.cpp); NPC Lua files wholesale; CLIENT_VERSION 1511→1513
- [x] **16.5** — Release metadata (68181dcdc, 60f9c62af): `SERVER_RELEASE_VERSION="3.6.1"` + docker/CI wholesale
- [x] **16.6** — Aceitar multiprotocol novos arquivos (82b4778a5): protocol_profile, transport_codec, protocol_session_hint, protocol_port_utils; atualizar CMakeLists; multiprotocol_test.cpp
- [x] **16.7** — Aplicar multiprotocol em arquivos existentes: connection (getLocalPort/setTransportCodec → public), protocollogin (resolveLoginLayout + onRecvFirstMessage rewrite), protocolgame (onConnectionAccepted, onRecvFirstMessage, logout, login, connect, writeToOutputBuffer), protocolgame_send (sendLoginChallenge), protocolgame_helpers (helpers anon namespace), canary_server (startup log + warnLegacy860), game.cpp::start() (multi-port), player login log
- [x] **16.8** — Build + 721/721 testes passando (717 unit + 4 forge integration novos)
- [x] **16.9** — Port forge integration tests (bd6217f37): 4 testes de regressão forge de origin/main (ForgeFuseItems/ForgeTransfer IdenticalId/SingleCopy)
- [x] **16.10** — Commit merge com todo.md atualizado
- [x] **CHECKPOINT 16** — 721/721 testes ✓; SERVER_RELEASE_VERSION="3.6.1"; CLIENT_VERSION=1513; multiprotocol integrado; ChatServiceTest SEGFAULT corrigido (heap injector em TearDownTestSuite)

## Phase 17: Merge main upstream (107 commits, 60f9c62af..666a60f6f) — merge real com 2 parentes

- [x] **17.0** — Completar porte Phase 16: commit dos ~1050 arquivos que ficaram fora de d2c13d162 (core.hpp 3.6.1/1513, NPC Lua, multiprotocol connection/protocol/CMake)
- [x] **17.1** — Baseline: build + 670/670 unit tests antes do merge; backup local `backup/solid-pre-phase17-wip`
- [x] **17.2** — Classificar arquivos tocados por 1df549ec5..60f9c62af: 1076 portados idênticos, 29 nunca portados (docs/workflows/AGENTS.md/quests.lua/icons_functions.lua/vcxproj), 18 adaptados → portados/nunca-portados resolvidos para origin/main
- [x] **17.3** — Merge real `git merge origin/main` (base 1df549ec5) — próximos merges usam base 666a60f6f
- [x] **17.4** — Adaptados: canary_server/protocol.hpp/protocollogin.hpp/pch/connection.hpp → main (diferenças eram cosméticas); creature.hpp/protocolgame.hpp → main + friends/fwd decls do branch; configmanager.hpp → overrides + deferUntilLoaded/isLoaded
- [x] **17.5** — protocolgame: merge 3-way por função entre protocolgame.cpp / protocolgame_send.cpp / protocolgame_helpers.hpp (82 take-theirs, 39 novas, 4 merges manuais); completou porte multiprotocol que faltava (GetCipsoft860TileDescription, sendCipsoft860SpecialTextMessage, Tibia1100 imbuement, AddPlayerStats 8.60)
- [x] **17.6** — game.cpp → services: merge 3-way por função no local real (18 conflitos resolvidos em Movement/Interaction/Trade/Loot/Outfit/Chat/Forge/CreatureManagement; `sendEffects` +attacker em CombatService; `playerSetFightModes` +PvpMode)
- [x] **17.7** — player.cpp → components: 11 conflitos resolvidos em Experience/Training/Death/Session/CombatEvent/Prey/CreatureEvent/Cylinder; qualificadores `m_player.` em hunks limpos
- [x] **17.8** — Aplicar arquitetura ao código novo: `monster_post_think` (game/scheduling), `queuePlayerAutoWalk*` → MovementService, `playerEquipItem` → PlayerInteractionService, `getItemTypeCount(4)`/`removeItemOfType`/`removeItemCountById`/`getAllContainers` → components, `addPzLockTicks` → PlayerSessionComponent, `PlayerRaceIconOverlays` (domínio puro)
- [x] **17.9** — Limites: game.cpp < 4k; player.cpp < 7k; protocolgame.cpp < 7k
- [x] **17.10** — vcxproj: +37 .cpp / +45 .hpp do branch (nova regra AGENTS.md)
- [x] **17.11** — Testes novos: PlayerRaceIconOverlays (11), PlayerInventoryComponent removeItemOfType/getAllContainers (7)
- [x] **CHECKPOINT 17** — build canary + canary_ut + canary_it verde; unit 830/831 + integration 51/51. Única falha: `ConnectionWriteDiagnosticsTest.WriteFailureRetainsPeerAndReportsClosureWithBoundedWarnings` (teste upstream exige `DEBUG_LOG=ON`; CI upstream roda testes só em `linux-debug`; preset `linux-release-enabled-tests` não define)
- [x] **17.12** — Auditoria pós-merge: `CombatService::sendBlockEffect` não tinha recebido o `source` do upstream (cópia morta em game.cpp foi a atualizada) → corrigido e cópia morta removida; auditoria por função em game/player sem outras perdas; repos delegam p/ IO (sem SQL duplicado)
- [x] **17.13** — Build tree recriado (cache tinha find_* da árvore vcpkg antiga); checagem `-fsyntax-only` não-unity dos .cpp do branch → 2 includes faltando corrigidos
- [x] **17.14** — Smoke test: servidor sobe (`server online`, datapack data-canary, DB descartável) e encerra limpo via SIGINT

## Phase 18: Dívidas pós-merge + DI restante

- [x] **18.1** — ConfigManager: instância única — binding DI de `IConfigManager` deve resolver para o mesmo objeto carregado de `g_configManager()` (hoje cria 2ª instância nunca carregada) + teste — `di::bind<IConfigManager>().to(ConfigManager::getInstance())`; teste `ConfigManagerContainerTest`
- [x] **18.1b** — `worldType = "open-pvp"`: Open PvP não-retro (upstream removeu `toggleServerIsRetroPVP` e passou `"pvp"` a alias de retro); parsing em `game/world_type_config` + 7 testes; smoke com `data-otservbr-global` → `World type set as OPEN-PVP`, zero erros Lua (único erro: `loadLuaMapUnique` uid 14092 item 2473, pré-existente, arquivo não mudou no merge)
- [x] **18.2** — Presets de teste com `DEBUG_LOG=ON` (paridade com CI upstream `linux-debug`; executáveis de teste já compilavam com `-DDEBUG_LOG`, `canary_core` não) → 839/839 unit + 51/51 integration
- [x] **18.3** — `PlayerDeathComponent` lê config pela porta `IConfigManager`; `Vocations::loadFromXml(file)` para fixtures; 10 testes (getLostPercent fixo/fórmula/promoção/bless/clamp; addUnjustifiedDead self/abaixo/red/black) — 849/849
- [x] **18.4** — `ExpertPvp`: porta `ExpertPvpWorld` (classe concreta virtual — injector de teste auto-constrói concretos não-ligados; abstratos dariam assert) com adapter padrão sobre `g_game()`/config; 0× `g_game()`/`g_configManager()` em expert_pvp.cpp; 6 testes com mundo falso — 855/855
- [x] **18.5** — Split `protocolgame_send.cpp` 7679 → `_send.cpp` 3748 (core/containers) + `_send_cyclopedia.cpp` 1562 + `_send_economy.cpp` 1467 (market/store/forge/prey/imbuement) + `_send_social.cpp` 1109 (chat/VIP/party); 204 funções conservadas byte-a-byte; CMake + vcxproj
- [x] **18.6** — DI restante: `Scripts` no container (causa real do erro boost::di `has_to_many_constructor_parameters` era o ctor privado, não o membro por valor); `Modules` e `metrics::Metrics` já resolviam via `inject<T>()` (binding implícito) — binding explícito bloqueado por ciclo de include (`modules.hpp`/`metrics.hpp` incluem `container.hpp`); 3 testes provam instância única; smoke boot/shutdown com data-otservbr-global exit 0 — 858/858 + 51/51
- [x] **CHECKPOINT 18** — Phase 18 completa

## Phase 19: DI sobre singletons nas camadas já extraídas

> Linha de base: `g_game()` 1148 / `g_configManager()` 526 chamadas em src/*.cpp

- [x] **19.1** — Services sem singletons: 23 → 0 (`config_`/`game_` em membros; helpers livres do market recebem `Game&`/`IConfigManager&`; `CreatureManagementService` recebe `IConfigManager&`); removidas cópias mortas de `playerCanUseItem*OnHouseTile` (interaction + game.cpp; upstream também não chama a variante `With`) — 858/858 + 51/51
- [x] **19.2** — Player components leem config por `IConfigManager::getInstance()` (50 → 0); porta segura em testes: container de teste sem binding cai no `ConfigManager` global (`DI::isBoundInTestContainer<T>()`); 4 testes (fallback, in-memory, depot limit free/grupo) — 862/862 + 51/51; totais agora g_game 1143 / g_configManager 459
- [x] **19.3** — 28 → 0 chamadas `IOLoginData::`/`IOMarket::` fora de `src/io/`: `IPlayerRepository` += loadPlayerById(disableIrrelevantInfo)/formatPlayerName/getGuidByNameEx/gameWorldAuthentication; nova porta `IVipRepository` (+`VipRepositoryDB`, fixture em memória); `IMarketRepository` += checkExpiredOffers/updateStatistics/estatísticas; `DI::port<T>()` (fake de teste ou binding de produção) para todas as portas; guarda de `account` em `PlayerVIP::edit` (null deref); 4 testes VIP — 866/866 + 51/51
- [x] **19.4** — `GuildRepositoryDB(IDatabase &)` dono do SQL (injeção por construtor); `IOGuild` removido; 5 testes de integração com MySQL + 1 teste de container — 867/867 + 56/56
- [x] **CHECKPOINT 19** — services 0 singletons; components 0 `g_configManager()`; 0 IO estático fora de `src/io/`; totais: g_game 1143 / g_configManager 458 (de 1148 / 526)

## Phase 20: Adapters donos do SQL + mundo injetado nos components do branch

> Linha de base: g_game 1143 / g_configManager 458; components 183 `g_game()` (93 em arquivos só do branch, 90 em arquivos upstream)

- [x] **20.1** — Market: `MarketRepositoryDB(IDatabase&, IConfigManager&)` dono do SQL + estatísticas (mapas + mutex); `IOMarket` removido; porta `checkExpiredOffers` → `loadExpiredOffers(cutoff, callback)` (callback só captura valores); refund de expiradas → `game/market/market_expiry` (corpo movido sem reordenar mover-histórico→reembolso, diff conferido); 4 unit (refund) + 8 IT (MySQL) + 1 container — 874/874 + 64/64; totais g_game 1140 / g_configManager 452 (contagem por linha, `git grep`)
- [x] **20.2** — VIP: `VipRepositoryDB(IDatabase&)` dono do SQL (10 métodos movidos idênticos; só `std::ignore` nos `executeQuery` soltos); VIP sai de `IOLoginData`; 4 testes IT (entrada, edição/remoção, ciclo de grupo, membro de grupo) — 874/874 + 68/68
- [x] **20.3pre** — teste prova que `IDatabase` injetado == `Database` conectado (mesma classe de bug da 18.1); smoke boot data-otservbr-global online 7s, zero `[error]`, exit 0
- [x] **20.3** — Components do branch: portas por papel `CreatureFeedback` (8 métodos, 20 chamadas) e `WorldRegistry` (8 métodos, 16 chamadas), concretas-virtuais, assinaturas = `Game` (só receptor muda; tabela em `tools/merge/README.md`); 93 → 57 `g_game()` nos arquivos só do branch; itens/use cases/catálogos ficam em `g_game()` (justificado em `tasks/AGENTS.md`); 10 testes com fakes — 885/885 + 68/68
- [x] **CHECKPOINT 20** — `IOMarket` removido, SQL de market/VIP/guild nos adapters; `g_game()` 1143 → 1104 (+16 nos adapters das portas), `g_configManager()` 458 → 452

## Phase 21: `PlayerRepositoryDB` dono do SQL de nomes/saldo + cobertura do reembolso de venda

- [x] **21.1** — `PlayerRepositoryDB(IDatabase&)` dono de `getNameByGuid`/`getGuidByName`/`getGuidByNameEx`/`formatPlayerName`/`increaseBankBalance` (movidos idênticos; só `std::ignore`); 4 testes IT (aspas, caixa diferente → grafia gravada, saldo, desconhecidos); README de merge + AGENTS — 888/888 + 72/72
- [x] **21.2** — 3 unit tests do reembolso de venda: dono online recebe 150 empilháveis como 100 + 50, 3 não empilháveis como 3 itens, dono desconhecido → só histórico; `InMemoryPlayerRepository` no container (sem MySQL); `ItemType` normalizado e restaurado. Tier não afirmado: `Item::setTier` lê o `g_configManager()` global (não carregado em unit test). Achados: `Player()` não cria inbox (produção usa `Player(nullptr)`); container insere no início
- [x] **21.3** — Smoke semeado (data-otservbr-global, banco descartável): oferta de compra 2×500 de um dono e de venda de 150 small emeralds (3032) de outro dono offline → `market_offers` vazio, 2 linhas `EXPIRED`, saldo do comprador 1000, inbox do vendedor 2 linhas somando 150; exit 0. Segundo smoke com um único dono (venda antes da compra) também passou: saldo 1000, inbox 150. `SaveManager::savePlayer` salva player offline de forma síncrona (`doSavePlayer`), então não há sobrescrita de saldo; a separação de donos no primeiro smoke só isolava os dois caminhos
- [x] **CHECKPOINT 21** — `IOLoginData` agora só tem load/save + `gameWorldAuthentication` (+ `getAccountType` morto, mantido por ser do upstream); reembolso de expiradas coberto (compra e venda, online e offline); 888/888 + 72/72

## Phase 22: SQL restante dos services em adapters + estabilidade de highscores + testes de login

- [x] **22.0** — Mapa upstream → branch (§5b), tabela de chamadas IO → portas, `tools/merge/coverage_check.py` (achou 88 funções do protocolgame fora do comando de merge; corrigido); cobertura 416/416 game.cpp, 830/830 player.cpp, 398/398 protocolgame.cpp
- [x] **22.1** — `players_online`: `IPlayerRepository::updateOnlinePlayers(guids)`; SQL + transação movidos do `CyclopediaService` para o `PlayerRepositoryDB` (mesma lógica, mesmas mensagens de log); service só monta a lista de GUIDs; 3 unit tests (lista vazia, GUIDs, falha logada) + 1 IT fora do `databaseTest` limpando as próprias linhas (rodado 2× sem resíduo); mapa §5b + README ("split between a service and an adapter") — 891/891 + 73/73
- [x] **22.2** — Highscores: porta `IHighscoreRepository` + `HighscoreRepositoryDB` (5 funções de query movidas idênticas, mesmos nomes; query cache saiu do `Game`); adapter roda a query e entrega `std::optional<HighscorePage>`; `HighscoreService::processHighscoreResults(Game&, ...)` estático e callback só com valores (sai o `[this]` que violava a regra de callback diferido); delegates mortos do `Game` removidos; 8 unit (fake entrega depois, como o dispatcher) + 5 IT (empate, GM fora/`total`, our rank → página 2, filtro de vocação com promoção, página 0 sem linhas); README com comandos de cobertura prontos — 899/899 + 78/78
- [x] **22.3** — 7 testes de integração do login (`gameWorldAuthentication`). **Bug de segurança achado e corrigido**: personagem marcado para deleção entrava no mundo (a lista da conta exclui deletados e `players[name] != 0` lia ausente como "não deletado"); agora a busca é sem diferenciar maiúsculas na lista e o IP vai no log. `AccountRepository::getInstance` → `DI::port` (container de teste sem binding dava nullptr). Correções em arquivos upstream listadas no §5b do AGENTS — 899/899 + 85/85
- [x] **22.4** — `highscore_cache::prune` (remove expiradas; no teto de 4096, o cache é limpo) antes de inserir nos dois caches (query e resultado) + métrica `highscore_cache_evictions{cache}`; página 0 em `HIGHSCORE_GETENTRIES` recusada antes da query + `highscore_requests_rejected{reason}`; 4 unit (prune e teto com 8192 páginas) + 2 do service (página 0, our rank ignora página) — 905/905 + 85/85
- [x] **22.5** — Highscores protegem o MySQL: página de entradas fresca no cache sai sem query (mesmo dentro do intervalo); 1s por player (`takeRequestSlot`, mapa podado); teto de 8 queries em andamento no servidor, liberado no topo de `processHighscoreResults` (vale para player que saiu); métricas `highscore_requests_total{type}`, `highscore_served_from_cache`, `highscore_requests_rejected{reason}`; fake com fila de callbacks e relógio controlável; 3 testes novos — 908/908 + 85/85
- [x] **CHECKPOINT 22** — 908/908 + 85/85; smoke semeado (data-otservbr-global, banco descartável): online 7s, 0 `[error]`, exit 0, reembolso conferido (saldo 1000, inbox 150); cobertura 416/416 game.cpp, 830/830 player.cpp, 398/398 protocolgame.cpp; grep de IO estático vazio; game.cpp 3787 linhas, player.cpp 6984; `g_game()` 1121 linhas = 1104 da Phase 20 + 16 dos adapters `CreatureFeedback`/`WorldRegistry` (antes não rastreados pelo `git grep`) + 1 do callback de highscore, que resolve o `Game` na chegada de propósito. **Bug de segurança corrigido**: login em personagem marcado para deleção. **Pendente**: abrir a janela de highscores no cliente uma vez (o caminho assíncrono não roda no boot)

## Phase 23: medir e conter caminhos cliente → banco síncrono

- [x] **23.0** — Build `linux-release-metrics` ok (OpenTelemetry via vcpkg); smoke com exporter: `/metrics` com 7316 linhas
- [x] **23.1** — `packet_watch::Scope` em `ProtocolGame::parsePacket`: latência por opcode (`method_latency{method="packet_0xNN"}`), `slow_packets_total{opcode}`, aviso `[PacketWatch]` (≥50ms, no máx. 1 a cada 5s, com contagem de suprimidos); 4 testes — 912/912 + 85/85; compila nos presets de testes e de métricas
- [x] **23.2** — Inventário medido (2026-11-12). Cada caminho com gatilho → limitador atual → custo → multiplicador do cliente:
  | Caminho | Gatilho | Limitador hoje | Custo medido/lido | Multiplicador | Prioridade |
  |---|---|---|---|---|---|
  | Fila de saída de leitor lento | qualquer saída | nenhum → **corrigido** (23.3a, `maxOutputQueueMessages`) | ~64 KB por mensagem, ~100/s | conexões | feito |
  | `market_history` estatísticas | `Game::loadItemsPrice` a cada `marketRefreshPricesInterval` (30 min), **no dispatcher** | nenhum | 300k linhas: **207 ms** sem índice (ALL + temporária), **96 ms** com `(state, itemtype, sale, tier, price)` (ref, Using index) | cresce com o histórico | alta: índice + tirar do dispatcher |
  | Cama ocupada (`BedItem::canUse`) | usar cama de casa com dorminhoco | `timeBetweenActions` (200 ms ≈ 5/s por player) | `loadPlayerById` completo (players, itens, depot, inbox, store, charms, conta…) | bots × players | alta: fazer menos trabalho |
  | Usar item com destino longe | `0x83` | **nenhum antes do A*** (o `getPathTo` roda antes do `canDoAction`) | A* limitado por `maxSearchDist = 7` | pacotes/s (25 em média, rajadas ~75) | média |
  | Outfit (`shine`/`ddos` do bot) | `0xD2` monta a janela inteira; `0xD3` sem intervalo, transmitido a todos os espectadores | só `maxPacketsPerSecond` | CPU + banda × espectadores | pacotes/s × espectadores | média: intervalo por player |
  | Lista de acesso de casa | editar lista | até 100 linhas por edição | até ~100 `SELECT` síncronos + `std::regex_replace` em até ~64 KB | edições/s de quem tem casa | média |
  | Navegação do mercado | abrir/navegar | `maxPacketsPerSecond` | consultas indexadas (`(sale,itemtype)`, `player_id`, `(player_id,sale)`) | pacotes/s | baixa, medir com tráfego |
  | Conexões por IP | conectar | taxa: >5 em 5 s com intervalo ≤ 500 ms bloqueia 3 s; **sem teto de simultâneas** | socket + buffers por conexão | IPs | média: produto (perguntar) |
  | Game migrations do upstream | primeiro boot de um banco | uma vez por banco | 5 migrations × todos os players (load + save) | — | nenhuma (custo único previsto) |
  - Digests do smoke (`performance_schema`): fora do `market_history`, as consultas sem índice são de boot (`houses`, `tile_store`, `boosted_*`, `player_hirelings`, `account_bans` por `expires_at`).
- [ ] **23.3** — Correções priorizadas pela medição (cama primeiro se confirmada)
  - [x] **23.3b** — Outfit: janela (`0xD2`) 1 vez/s e troca (`0xD3`) 1 vez a cada 500 ms por player (valores escolhidos pelo usuário); `Game::playerRequestOutfit` movido para o `OutfitService`; helper `RequestCooldown` (`src/utils/request_cooldown.hpp`, com poda), também usado pelo highscore; métrica `outfit_requests_throttled{request}`; 8 testes — 924/924 + 85/85, preset de métricas ok
  - [x] **23.3c** — Teto de conexões **ainda não logadas** por IP: `maxPendingConnectionsPerIp` (padrão 10, `0` desativa). A vaga é liberada no login (`ProtocolGame::login`/`connect` → `ConnectionManager::markLoggedIn`) e no fechamento. Conta só quem não logou por causa do CGNAT (decisão do usuário, depois do alerta). `IpConnectionLimiter` com 6 testes; recusa → `connections_refused_total{reason="per_ip_pending_limit"}` + aviso limitado por taxa — 930/930 + 85/85, preset de métricas ok
  - Fingerprint de hardware via client: avaliado e não implementado. O bot pode forjar o valor, então serviria só como sinal auxiliar de moderação; exige client próprio e cuidado com a LGPD
  - [x] **23.3d** — `schema_hardening` (`src/database/schema_hardening.cpp`): roda no boot depois das migrations do upstream e só adiciona índices `solid_*` que faltam (pula se outro índice já começa pelas mesmas colunas; falha loga e o boot segue; loga a duração). Sem migration numerada e sem editar `schema.sql`, então não conflita com o upstream. Primeiro índice: `solid_market_history_stats (state, itemtype, sale, tier, price)` (300k linhas: 207 → 96 ms). 3 IT (idempotência, equivalente com outro nome, colunas iniciais); smoke com dois boots no mesmo banco: o 1º cria em 17 ms, o 2º não muda nada — 930/930 + 88/88
  - [x] **23.3e** — Cama (caminho caro achado na auditoria; não confirmado que seja o bug real relatado): `BedItem::canUse` deixou de carregar o dorminhoco inteiro (~20 queries + itens) a cada uso; usa `IPlayerRepository::loadHouseAccessFacts` (uma consulta: conta, flag `CanEditHouses` do grupo, rank de guild) e os overloads `House::getHouseAccessLevel(facts)` / `AccessList::isInList(guid, rank)`, que espelham as versões com `Player`. 2 IT (fatos e níveis dono/sub-dono/convidado/estranho/`*` numa casa real). Achados no caminho: `Database::getInstance` passou a usar `DI::port` (no IT os adapters usavam um `Database` não conectado); `AccessList::parseList` remove dígitos dos nomes
  - [x] **23.3f** — Estatísticas do mercado fora do loop: `Game::loadItemsPrice` encadeia `loadStatistics` → `setStatistics` → `loadActiveOffers` (assíncronos, callbacks sem captura); `Game::rebuildItemsPrice` guarda a regra (média aceita elevada à melhor oferta de compra), com 3 testes; o boot mantém o `updateStatistics()` síncrono — 933/933 + 90/90, smoke ok
  - [x] **`move` do bot verificado** (leitura de código): `playerMoveItem` exige o intervalo de ação, o id exato na posição/stackpos, o mesmo andar e distância ≤ 1 (senão o player anda por caminho real), e recusa item não empurrável ou com `UNIQUEID` (`NOTMOVABLE`), item de outro dono, saída de casa e item não pegável no container (`CANNOTPICKUP`). O pacote fixo (item 8607 = "archway" aqui) mira outro servidor
  - [x] **23.3f-fix** — Revisão de segurança: a carga assíncrona sem linhas apagava as estatísticas em cache (a síncrona mantinha). `loadStatistics` agora informa `loaded`; o `Game` só troca o cache quando houve linhas; teste `RefreshWithoutRowsKeepsTheCachedStatistics`
  - [x] **Teto por IP provado com sockets reais** (smoke): 12 conexões TCP com 0,6s de intervalo → 10 mantidas, 2 fechadas, aviso `[ConnectionManager::admitFromIp]`; depois de fechar 3, uma nova é aceita
  - [x] **23.3g** — Orçamento global da fila de saída: `maxOutputQueueMessagesTotal` (padrão 16384 ≈ 1 GB, escolha do usuário); no teto, só cai quem tem 64+ mensagens; contador global exato via helpers (`enqueueOutput`/`popOutputFront`/`clearOutput`/`clearOutputExceptFront`, liberado no destrutor); 3 testes. Recursão infinita introduzida por substituição em massa no próprio helper achada e corrigida antes do commit — 937/937 + 90/90
  - [x] **23.3h** — `DatabaseTasks::getInstance` por `DI::port` (teste `DatabaseAndTasksIgnoreAnUnrelatedTestContainer`)
  - [x] **23.3i** — Lista de acesso de casa: texto acima de 8 KB recusado antes do parse; 1 edição/s por player; janela consumida na recusa; métrica `house_list_updates_rejected{reason}`; 4 testes
  - [x] **23.3j** — **Enchimento de disco por relatório de bug** (`0xE6`): o `Player:onReportBug` do datapack grava a mensagem do cliente (até ~64 KB) em `data/reports/bugs/<nome>/report.txt` a cada pacote. Agora: 1 relatório/min por player, mensagem truncada em 2048; `playerReportBug` movido para o `PlayerInteractionService`. (`playerDebugAssert`, que também escreve em arquivo, não está ligado a nenhum pacote.) Auditoria dos 35 `msg.getString()` sem teto: pendente para os demais handlers
  - [x] **23.3k** — **Crash remoto em `PlayerVIP::editGroup`**: id de grupo do pacote sem checagem → null deref derrubava o servidor. Corrigido junto com os irmãos: grupos padrão não podem ser renomeados/apagados por pacote forjado; `addGroup` respeita o limite de grupos; nome/descrição de VIP truncados em 128; destinatário de canal privado > 30 recusado antes do SQL. 5 testes de regressão. Auditoria dos 35 `msg.getString()`: VIP, canal privado e relatório de bug corrigidos; `AddVip` (≤ 25), livros (`maxTextLength`), convites de canal e bestiary já seguros; mensagem de guild e nome de hireling são descartados; opcode estendido só compara o texto
  - [x] **23.3l** — Auditoria de valores do pacote usados como índice/id/nome/caminho (`priorize os bugs e segurança`). Corrigidos, cada um com teste de regressão e linha na tabela do §5b:
    - wheel: posição da grade de gema (byte 0–255) indexava `m_basicGrades[49]`/`m_supremeGrades[95]` → **leitura/escrita fora da tabela** (`c9c301f5e`);
    - charm: atribuir criatura com charm não comprado (Gut pagava de graça) e com raça fora do bestiário (boss) (`2daa45771` + correção desta revisão);
    - prey "lista completa": raça fora do bestiário aceita → **prey em boss** (`71e12c268`);
    - leilão: recusa do `canBidHouse` não retornava; lance em casa com dono **sequestrava transferência pendente** (`029f8dfc6`);
    - transferência de casa: load completo de personagem offline por pacote antes de checar dono (`3713d6544`);
    - report de violação: alvo do pacote no caminho do arquivo (**append em arquivo arbitrário** com `..`) e sem limite de taxa (`aca095141`);
    - store (Lua): busca usada como padrão Lua → **um pacote congela o servidor**; transferência de 0 coins com 4 queries (`89799b1b2`).
    Regras destiladas no §2b do `tasks/AGENTS.md`; harness `tools/lua/store_parsers_check.lua`. Depois: mailbox (só checagem leve de existência, escolha do usuário; `2d963e4d9`), regras do leilão (`0a35eea6c`) e nome no store (`7c66ceb18`)
  - Proposta (próxima fase): **segunda conexão MySQL só para `DatabaseTasks`**. Com uma conexão única, uma consulta de fundo (~100 ms com 300k linhas) ainda faz o loop esperar se ele precisar do banco no mesmo instante
  - [x] **23.3a-fix** — Use-after-free introduzido pelo `34c4e384a`: esvaziar a fila com uma escrita em andamento liberava o buffer que o asio ainda lia. Agora o handler do `async_write` segura a `OutputMessage` e o estouro mantém a mensagem da frente; teste `OverflowWithAWriteInFlightKeepsItsBufferAlive` — 916/916 + 85/85, preset de métricas ok
  - [x] **23.3a** — Teto da fila de saída por conexão (ataque de leitor lento do bot): `maxOutputQueueMessages` (padrão 2048 ≈ 128 MB / ~20 s de cliente parado, `0` desativa; valor escolhido pelo usuário); estouro → esvazia a fila, desconecta, aviso com IP, `connection_output_queue_overflow_total`; lido pela porta `IConfigManager`; 3 testes; `config.lua` local atualizado com a chave
- [x] **23.4** — Pendências da 22 (irmãos, vaga do highscore, handoff)
  - Auditoria feita (só leitura):
    - outros leitores de `getAccountPlayers` (`protocollogin`, `CreatureManagementService::getPlayersByAccount`, `PlayerBadge::getPlayersInfoByAccount`) percorrem a lista e não repetem o bug do login;
    - `[this]` em tasks de player nos services: vem do upstream (era `[this]` do `Game`), e os services são do `Game` e vivem o processo todo, então não foi alterado (custo de merge alto, risco baixo);
    - `DatabaseTasks::store` sempre despacha o callback e `storeQuery` não lança, logo a vaga do highscore só vaza no shutdown.
  - Observação para a 23.2: em erro recuperável de conexão, `Database::storeQuery`/`executeQuery` repetem para sempre (`goto retry` + sleep de 1s) **segurando o mutex**; MySQL fora do ar trava toda query síncrona do loop principal.
  - Inventário: `getPlayersByAccount(acc, allowOffline = true)` carrega cada personagem da conta do banco (multiplicador).
- [ ] **23.5** — Segunda rodada da auditoria do client hostil (retomada em 2026-11-13, pedido do usuário: "procure por falhas que o client possa explorar"). Escopo: handlers de pacote que a 23.3l não cobriu, caminho de login (antes da autenticação), módulos Lua por recvbyte, talkactions de jogador e NPCs com texto do jogador. Candidatos lidos no código (custo real **não medido**, como na cama):
  - **CRÍTICO — derrubar qualquer jogador sem a senha** (`ProtocolGame::onRecvFirstMessage`, igual no upstream desde `c07696b2c`, 2023): com o personagem do pacote online, `foundPlayer->client->disconnectClient(...)` roda **antes** do challenge, do ban de IP e do `gameWorldAuthentication`. Basta o nome do personagem e um bloco RSA (a chave é pública): uma conexão a cada ~500 ms por IP mantém a vítima fora do client enquanto o personagem fica no mundo (open-pvp: morre sem poder curar). Com perfil de client diferente, a resposta revela a versão do client da vítima. Correção: tratar a sessão existente só depois da autenticação (mudança no fluxo de login, pedir aprovação)
  - **`!transfer Nome,valor`** (`data/scripts/talkactions/player/bank.lua`, ligado por padrão, qualquer jogador): depois de validar o valor, `Game.getNormalizedPlayerName(nome)` faz `getPlayerByName(nome, true)` (personagem offline carregado inteiro) e `Bank.transfer` → `BankFunctions::getBank` → `Lua::getPlayer(L, arg, true)` carrega de novo; o saldo de quem manda só é conferido depois. Nome inexistente: `SELECT` + `error` no log com o texto do atacante. Talkactions não têm intervalo (`checkPlayerCanSayTalkAction` roda antes do mute). Ou seja, até 2 loads completos por mensagem de chat, sem precisar ter dinheiro
  - **Log sem limite por pacote**: `WeaponProficiency::setSelectedPerk` loga `error`/`warn` a cada perk inválido; um `0x03` traz até 255 perks → até 255 linhas por pacote. O logger é síncrono no dispatcher (stdout). Mesma classe em outros caminhos (`getPlayerByName` com nome inexistente, avisos de `Bank::transferTo`). Correção candidata: um orçamento de log por pacote no `packet_watch::Scope` (como o `SlowPacketReporter`), em vez de remendo por linha
  - **`!reward`** (`data/scripts/talkactions/player/reward.lua`; `toggleReceiveReward = false` por padrão): o storage é conferido ao abrir a janela, não na escolha; cada `!reward` abre outra janela modal com id novo, e responder todas entrega uma arma de exercício (64400 cargas) por janela
  - **Lista do quick loot (`0x91`)**: contagem u16 sem conferir os bytes restantes (`msg.get` além do fim devolve 0), então até 65535 entradas por pacote; busca linear por item saqueado. Não é salva no banco
  - **Categoria do container gerenciado (`0x90`)**: byte do pacote vira `ObjectCategory_t` sem checagem; `1 << category` é UB para ≥ 32 e vira chave do mapa
  - **Livestream**: `/name` e `/show` do espectador sem intervalo (o rename é transmitido à sessão mesmo para espectador mutado); o nome não é comparado com o do caster
  - **Digitando (`0x38`)**: transmitido a todos os espectadores sem intervalo (mesma classe do `0xD3`)
  - Conferidos sem mudança: limites do `setSelectedPerk` (nível e índice), `Item::items[id]` (com checagem de limite), compra/venda em NPC (preço em 64 bits, teto de quantidade, `isUIExhausted`), modal do treino offline (escolha validada), paginação do histórico do store (numérica, com intervalo de 250 ms), chat do espectador (com intervalo). SQL em Lua: o grep só pegou concatenações numéricas, mas não cobre `string.format` nem consultas montadas em variável; **não** conta como verificado
  - Lidos depois: `Connection::parseHeader`/`parseProxyIdentification`/`parsePacket` logam no máximo uma linha por conexão e fecham (limitado pela taxa de conexões por IP); login de espectador (`castViewerLogin`, senha da stream, teto por IP e por stream); `logout` e reenvio de container do espectador (só leitura do caster)
  - Falta ler: `ProtocolStatus`, módulos Lua por recvbyte (`data/modules`), laços com contagem do pacote e `static_cast<Enum>` em team finder, `parseGetObjectInfo`, exiva, pódio, stash e depot search
  - Decisões do usuário (2026-11-13): corrigir o login agora; `!transfer` com consulta leve + 1 talkaction por segundo por player (staff sem limite); orçamento de log de 3 linhas por pacote e 20 por player por minuto; menores: `!reward` + categoria, lista do quick loot com teto de 1000, digitando só na mudança e no máximo 1 vez/s, `/name` do livestream a cada 10 s
  - [x] **23.5a** — Login: o bloco da sessão existente foi para depois do `gameWorldAuthentication`. Prova com sockets reais (`tools/smoke/login_takeover_check.py`, protocolo 8.60, banco descartável): no binário anterior, uma conexão com conta inexistente e senha errada para o nome da vítima derrubou o client dela (`victim alive=False`); no binário corrigido (`linux-release`), a vítima ficou (`victim alive=True`), o atacante caiu e o relogin do dono com a senha certa substituiu a conexão antiga e entrou (`OK`); log do boot sem erro além do `Couldn't load account [nosuchaccount]` esperado
  - [x] **23.5b** — `!transfer`: `Game.getNormalizedPlayerName` por consulta leve (sem load, sem `error`); `Bank.transfer`/`transferToGuild` recusam valor 0 e falta de saldo antes de resolver o destino (`PlayerNameLookupFunctionsTest`, 5 testes: 0 loads para normalizar, 1 load — só a origem — para transferência sem saldo)
  - [x] **23.5c** — `TalkActionCooldown`: 1 talkaction por segundo por player, staff livre; métrica `talkaction_requests_throttled` (3 testes)
  - [x] **23.5d** — `log_budget`: 3 linhas por pacote, 20 por player por minuto, excedente em `packet_log_lines_suppressed{opcode}` + resumo a cada 5 s; o aviso de pacote lento fica fora do orçamento (6 testes)
  - [x] **23.5e** — Menores: `!reward` (harness `tools/lua/reward_check.lua`: branch passa, upstream entrega 2 armas), categoria e lista do quick loot (`quickloot_requests_rejected{reason}`), digitando no `ChatService` (`typing_updates_dropped`), `/name` do livestream (`viewerRenameRefusal`); 7 testes. Achado no caminho: suites que criam `Player()` caíam rodando sozinhas (catálogo de itens não carregado); `MailboxReceiverTest` corrigida junto
  - [x] **23.5f** — Revisão (advisor + revisão automática de segurança): o corte de log não pode esconder o autor (cada player cortado é nomeado com IP, opcodes e contagem no máximo 1 vez por minuto; o resumo global saiu); staff sem orçamento (`/reload` com scripts quebrados mostra tudo); espectador de livestream medido como ninguém (gastava o orçamento do caster); teste do gancho real (`LogWithSpdLog` com sink em memória: 10 `error` → 3 linhas); "digitando" parado há 60 s expira (relog); lista/categoria do quick loot recusada deixa aviso com o player; segunda revisão de segurança: entradas com linhas pendentes nunca eram podadas (mapa crescia e, cheio, era varrido a cada linha) — agora a poda remove quem ficou uma janela quieto, mesmo pendente, e só roda de novo quando o mapa dobra
  - [x] **23.5g** — Decisões do usuário sobre o relatório (2026-11-13): este fork ainda não está em produção (sem hotfix); o usuário avisou o upstream no Discord; a branch pode ser publicada (o push é do usuário: a regra "nunca dar push" continua valendo para o agente); `tools/smoke/login_takeover_check.py` fica no repo; team finder fica como está. Feito: exiva 1 edição a cada 2 s por player e janela de proficiência (`0x01`) 1 vez por segundo (`PlayerInteractionService::admitExivaRestrictionsEdit`/`admitProficiencyListing`, métrica `client_requests_throttled{request}`, 4 testes); party analyzer recusa contagem maior que os bytes; `Game.getNormalizedGuildName` por `IGuildRepository::formatGuildName` (uma consulta, sem carregar a guild; 1 UT + 1 IT)
  - Sobras: módulos Lua por recvbyte (`data/modules`) e SQL em Lua (o grep só pegou concatenações numéricas; não verificado). `ProtocolStatus` lido: intervalo por IP (`statusTimeout`), localhost e o IP configurado isentos
  - Commits intermediários (`7fce8c9f2`, `35791bb1f`, `7b7835968`, `e56d5ace7`) não foram compilados isolados; o build e os testes valem para a ponta
  - Validação: ut 1017/1017, it 91/91, preset de métricas ok, `syntax_check` 0 erros, smoke do login com boot completo (online em 6 s, exit 0, sem erro de Lua), banco descartável apagado
- [ ] **CHECKPOINT 23** — parada em ponto estável em 2026-11-12, retomada na 23.5 em 2026-11-13 (pendências em `tasks/AGENTS.md` §6d); 1017/1017 + 91/91, preset de métricas ok

## Phase 24 — datapack a partir do TibiaWiki (aberta em 2026-11-12; plano em `tasks/plan.md`)

- [x] **24.1** — Ferramenta de relatório (`tools/datapack/`, `f79d68e8b`): extrator luajit (1655 monstros, igual ao servidor), coletor da API com cache em `build/datapack-wiki`, parser `Loot2`, comparador com intervalo binomial, 15 testes Python. Primeira rodada: 1571 de 1655 comparados, 1087 com diferenças (chance 3712, quantidade 1366, faltando 1197, só no datapack 386, atributos/elementos ~1100); mitigation separada como questão sistemática (689 de 690, datapack ≈ 0,63 × wiki). Página de revisão publicada para o usuário
- [x] **24.2** — Revisão: usuário aceitou o primeiro lote (quantidades e itens faltando com nome único), o `--apply` idempotente e investigar mitigation antes de mexer
- [x] **24.3** — Merge: manifesto `tools/datapack/fixes.json` (acumula) + `wiki_check.py apply` sobre a versão do upstream; registrado no §5b e em `tools/merge/README.md` (`11cdbb8eb`, `997af9180`, `f80cc3e1c`)
- [x] **24.4a** — `maxCount` elevado ao observado: 514 entradas em 340 arquivos (só empilháveis, nunca baixa, sem bosses, sem saltos grandes) (`a70a1e4f4`); smoke sem aviso de loot
- [x] **24.4b** — Loot que faltava: 265 entradas em 129 arquivos (sem moeda, só estatística 12+, nome único, sem bosses) (`734c13139`); smoke sem aviso de loot
- [x] **24.5a** — Chances: 1014 em 188 arquivos (entrada única, estatística 12+, sem moeda, sem boss, até 5×) (`6555c5a0f`); ex.: Icecold Book com ultimate mana potion 100% → 21%
- [x] **24.5b** — Atributos: 77 XP, 22 armor, 1 HP, 117 elementos, 60 imunidades em 175 arquivos, só com raceId igual ao do wiki e até 2× (`13f72af07`)
- [x] **24.5c** — **Erro corrigido:** a regra de boss só olhava o bosstiary; 136 monstros `rewardBoss` fora dele levaram 104 fixes de loot em 28 arquivos. Ferramenta passa a usar `rewardBoss` (`f4d88150d`); arquivos restaurados ao estado de `997af9180` e manifesto reaplicado (`40ed1a2cd`)
- [x] **24.5d** — Bosses de corpo (bosstiary sem `rewardBoss`) entram nas regras normais: 58 fixes em 20 arquivos (`e8d7ae27b`, `1be498cc3`). Bosses de baú ficam fora: cada participante rola a tabela com fator de contribuição (2× solo), então a chance do datapack é base, não comparável ao wiki
- [x] **24.6** — Revalidação do manifesto inteiro a cada `plan`: identidade obrigatória para loot (race id no datapack), valores incertos do wiki ignorados, `from` em cada fix (o `apply` reporta conflito em vez de sobrescrever o upstream), revisão movida para `build/`. Saíram 122 fixes (60 arquivos restaurados), entraram 42 atributos de bosses de corpo; total 1963. Smoke com controle negativo; 986/986 + 90/90
- [x] **24.7a** — Saltos grandes com evidência independente: 265 chances (outra versão confirma, ou bloco único com ≥ 30 quedas e ≥ 1000 kills) e 17 atributos decididos caso a caso em `tools/datapack/reviewed.json` (`e77781e16`, `b8fb39be7`)
- [x] **24.7b** — Elementos e imunidades de bosses de baú (88 + 6); HP/XP/armor de boss ficam para revisão (scripts de quest), 3 stats de faraó desfeitos (`d299b887d`, `5ecbd7bf1`)
- [x] **24.7c** — Nomes ambíguos ou com sufixo do wiki resolvidos pelas flags `take`+`market` do `appearances.dat`: 52 itens gravados por id (`0c0a41509`, `b0f20a6db`)
- [x] **24.7d** — Estatística antiga confirmada pela raridade atual da infobox (faixas do `Template:Loot Item`): 258 chances + 173 itens (`c1b8fa322`, `465b10291`); 829 achados antigos já batiam com a raridade atual
- [x] **24.7e** — **Decisão do usuário: o wiki é a fonte da verdade** (o jogo original teve muitos rebalanceamentos). Atributos sem limite de proporção, bosses inclusos (scripts de quest conferidos: usam percentuais ou curas relativas), mitigation aplicada (683), chances com salto > 5× aplicadas; identidade e valores certos continuam obrigatórios (`d890a044d`, `dbdfff447`)
- [x] **24.7g** — Dinheiro: tipo `coins` reescreve as entradas de moeda com as do wiki quando os itens diferem ou o dinheiro por kill difere > 10% (`93ae4c041`, `7c0108786`)
- [x] **24.7h** — Revisão do advisor: dinheiro pelo `total` do wiki (não pelo meio da faixa), mínimo de 1000 kills, `coins` substitui `max_count` de moeda, `from` preservado como valor do upstream; teste do lado do upstream (base `997af9180` + `apply`) com 0 conflitos; mitigation conferida (`rateMonsterDefense` = 1,0). 127 monstros com moeda, manifesto 3680
- [x] **24.7i** — Decisão do usuário ("siga a wiki"): bosses com mudança extrema mantidos; loot só no datapack removido com portões (`remove_loot`): 78 remoções, 207 ficam fora com motivo (baú 141, sem race id 36, tabela atual lista 9, id sem nome 9, antiga sem tabela 8, moeda 3, nome sem resolução 1)
- [x] **24.7j** — **Erro corrigido:** 34 `add_loot` duplicavam item que o upstream já tinha com outra grafia/id (14 com id repetido, 16 com livro/mapa de outro id, 4 sem efeito). Comparador pareia nomes por id (`loot_aliases`), `plan`/`validate`/`apply` recusam a duplicata; 32 arquivos restaurados de `997af9180` e reaplicados; invariante sem id repetido novo; 21 chances novas dos pares (ex.: Bony Sea Devil rod 0,88% → 4,28%). Upstream-side 0 conflitos, smoke limpo, 57 + 22 testes. Manifesto 3745
- [x] **24.7l** — Redução de `maxCount` (decisão do usuário): quantidade acima de tudo que o wiki viu, com chance < e^−7 de não aparecer pela faixa do datapack, vira o máximo observado; só empilhável, entrada única, estatística 12+, linha do wiki coerente (14 linhas sem `amount:` com `total` maior ficaram de fora). 171 reduções; `apply` exato para redução. Upstream-side 0 conflitos, smoke limpo, 61 + 25 testes. Manifesto 3916
- [x] **24.7m** — Aumento de `maxCount` só com estatística 12+ (decisão do usuário: "igual à versão atual"): 233 aumentos de blocos antigos saíram, 177 arquivos restaurados de `997af9180` e reaplicados (233 linhas de volta ao upstream). Ficam 144 aumentos + 171 reduções. Upstream-side 0 conflitos, smoke limpo, 62 + 25 testes. Manifesto 3683
- [ ] **24.7f** — Fora do automático (contagem de 2026-11-13, depois da 24.7j): loot de baú de recompensa (2155; a chance do datapack é base por participante), sem identidade confirmada (417 + 291), estatística antiga sem confirmação (317) e dinheiro só com estatística antiga (216 monstros), nomes sem resolução (19), entradas compartilhadas (16), não empilhável (12), loot só no datapack mantido (207, motivos na 24.7i)
- [x] **24.7n** — Decisões do usuário sobre o que ficou fora (2026-11-13; recontagem: baú 2224 entradas, estatística antiga 1545 + dinheiro antigo 225, identidade 438 + 291, moeda 108, pequenos ~80): **baú de recompensa segue o wiki como chance base** (o fator do baú é ≈ 1 com ~4 jogadores de contribuição igual; solo é 2×); itens e quantidades seguem o wiki; entradas `unique` (só o maior contribuidor rola) ficam como no upstream; item que falta no baú fica para revisão (pode ser `unique`). Estatística antiga sem confirmação e identidade não confirmada: **manter upstream**. Grupos pequenos: resolver à mão pelo `items.xml`/appearances, com nota no `reviewed.json` (24.7o). Feito: 1056 fixes em 107 bosses de baú (375 chances: 227 sobem, 148 descem; 644 `maxCount`: 596/48; 37 remoções, todos os itens seguem obtíveis); 9 entradas `unique` mantidas, 418 itens faltando no baú para revisão; `from` da chance passa a ser a chance crua da entrada (os 48 drops garantidos com `chance = 13600000` de The Curse Spreads/Percht Queen davam conflito). `plan` estável, `apply` duas vezes sem mudança, lado do upstream 0 conflitos (os mesmos 2 arquivos só com ordem diferente), invariante de ids 0, smoke limpo, 63 + 26 testes. Manifesto 4739
- [x] **24.7o** — Grupos pequenos: ids das páginas de item do wiki (`wiki_item_ids.json`, só para nomes que o `items.xml` não tem; a página escolhe o id de uma adição ambígua), `chance_split` para item em 2+ entradas, adição `exact`, fix pareado com outro item derrubado (The Scourge of Oblivion restaurada). 18 splits, 10 adições, 3 remoções, 1 chance; ficam `die`, `wooden trash` e a coroa da Percht Queen. `plan` estável, `apply` duas vezes sem mudança, lado do upstream 0 conflitos (4 arquivos só com ordem), invariante 0, smoke limpo, 74 + 29 testes. Manifesto 4770
- [x] **24.7k** — Rótulo de versão do `Loot2` não é confiável: o bloco "8.6" aparece em criaturas implementadas depois (Candy Horror 13.40, Weretiger 13.20, Cult Believer 11.40). Proposta: versão efetiva = maior entre o rótulo e o `implemented` da infobox; afetaria 986 itens hoje em "estatística antiga" (227 monstros; 197 com implementação 12+). Decisão do usuário: não adotar; "15+ quando existir" (o bloco mais novo com ≥ 100 kills já manda; resto como antes)

## Adiado em 2026-11-13 (pedido do usuário: "salve essas pendências para depois")

- **Datapack (24.7f):** recontar os grupos depois da 24.7m (os 233 aumentos de `maxCount` desfeitos entram em "estatística antiga"); depois baú de recompensa (converter a chance do wiki para a chance base por participante), sem identidade, estatística antiga sem confirmação + dinheiro antigo, grupos pequenos
- **Phase 23:** itens 0 (sessão com client real no build de métricas: bloqueia produção), 2, 3, 5, 6 e 8 do §6d do `tasks/AGENTS.md`
- **Produção (com o usuário):** hotfix do store em Lua = `git diff 89799b1b2^ 89799b1b2 -- data/libs/gamestore/parsers.lua` (aplica no `parsers.lua` do upstream); `metricsPrometheusAddress = "127.0.0.1:9464"` ou firewall
- **Upstream:** último merge com base `666a60f6f` (2026-11-05); `git fetch` e conferir commits novos antes do próximo merge

### Achados do bot de ataque (`/home/nicollas/teste/proxy-bot`, lido sem executar) — próximas tasks da 23
- **Stresser**: 1024 goroutines numa conexão; modos `pathfinder` (usar item com moeda → posição distante fixa, força pathfinding), `outfit` (`0xD2` + `0xD3` em laço; resposta grande para pedido pequeno), `walk`/`leftright`, `follow` (seguir/cancelar alternado).
- **Leitor lento** ("server throttling" do bot): lê a saída do servidor devagar. `Connection::messageQueue` **não tem teto** e o timeout de escrita (30s) vale por escrita, então um gotejamento nunca estoura → fila e memória crescem. Corrigir: teto por conexão (bytes/mensagens) + desconexão + métrica.
- Limite atual `maxPacketsPerSecond` = 25 por média em janela de ~3s (permite rajadas de ~75); medir com o `packet_watch` antes de mudar.
- **Attacker** = automação de "moves" com cooldown (não é DoS).
- Catálogo completo de `/bot` (lido em 2026-11-12):
  - `shine`: 100× (`0xD2` abre a janela de outfit + `0xD3` troca cores) a cada 25ms;
  - `ddos`: 16 goroutines em laço infinito de `0xD2` + `0xD3`;
  - `outfit <lookType>`: `0xD2` + `0xD3` com lookType escolhido. **Posse validada**: o servidor só troca se `player->canWear(...)`. O custo é volume: `0xD2` monta a janela inteira (outfits/montarias) e `0xD3` não tem intervalo e é transmitido a todos os espectadores;
  - `move`: um `0x78` fixo (mover item de uma posição do mapa para um container); conferir as checagens de distância/alcance em `playerMoveItem`;
  - `fish`/`attack`/`heal`: automação;
  - `stress`: modos acima;
  - `debug`: só alterna o log do bot.
- Achado do código: no "usar item com" para destino longe, o `getPathTo` roda **antes** do `canDoAction`, então um A* por pacote (limitado por `maxSearchDist = 7`).
- Índices (levantados no `information_schema`, não no texto do `schema.sql`, porque as FKs criam índice implícito): tabelas por player estão indexadas; `market_history` sofre varredura completa no `updateStatistics`, chamado por `Game::loadItemsPrice` **periodicamente no dispatcher**; 7 índices redundantes (`player_items.player_id`, `player_spells.player_id`, `player_wheeldata` igual à PK, `account_viplist.account_id`, `account_vipgrouplist.account_id`/`vipgroup_id`, `house_lists.house_id_index`). Regra: índice novo só pelo passo idempotente da branch no boot (sem migration numerada, sem editar `schema.sql`); remover redundantes só depois de medir e com aprovação.
- Pendências da revisão: medir o `market_history` com centenas de milhares de linhas (com e sem índice); digests do `performance_schema` (também pega SQL do datapack Lua); custo do `packet_watch::Scope` no build com métricas; o que limita conexões por IP antes e depois do login.
- **Segurança**: `metricsPrometheusAddress` padrão `0.0.0.0:9464` expõe métricas (latências, começo do SQL) a qualquer um; em produção usar `127.0.0.1:9464` ou firewall liberando só o Prometheus.

