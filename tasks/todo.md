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
- [ ] **22.1** — `players_online` via `IPlayerRepository::updateOnlinePlayers`
- [ ] **22.2** — Highscores: `IHighscoreRepository` + `HighscoreRepositoryDB`; callback sem `[this]`
- [ ] **22.3** — Testes de integração de `gameWorldAuthentication`
- [ ] **22.4** — Caches de highscore com limite (hoje crescem sem fim, upstream também)
- [ ] **CHECKPOINT 22**
