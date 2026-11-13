# Move-aware upstream merges

This branch moved large parts of `game.cpp`, `player.cpp` and `protocolgame.cpp` into
services, components and split files. A plain `git merge` cannot follow those moves:
upstream hunks against moved functions either conflict with an empty region or apply to
a leftover delegate. These tools merge upstream changes **function by function** into the
place where each function lives today.

| Tool | Purpose |
|---|---|
| `cppchunks.py` | Splits clang-formatted C++ into top-level chunks (exact round-trip). |
| `split_merge.py` | Merges an upstream file that we split into several files (e.g. `protocolgame.cpp` → `protocolgame.cpp` + `protocolgame_send.cpp` + `protocolgame_helpers.hpp`). |
| `moved_merge.py` | Merges a god-object file whose functions moved into services/components (`game.cpp`, `player.cpp`). |
| `audit_moved.py` | Checks that lines upstream added to moved functions reached every full copy of the body. |
| `syntax_check.sh` | Compiles single `.cpp` files with `-fsyntax-only` using the unity build's flags, to catch includes hidden by unity batches. |
| `coverage_check.py` | Lists upstream functions of a file that have no copy in the branch files given. Run it after every code move and every merge; 0 missing (or only the listed namespace helpers) is required. |

## Workflow

1. Start from a clean tree and a green baseline build/test run.
2. `git merge --no-commit origin/main`. Since Phase 17 the merge base is a real upstream
   commit, so only new upstream commits are in play.
3. Resolve ordinary conflicts as usual. For moved-code files, reset to ours and merge by function:

   ```sh
   git checkout --ours -- src/server/network/protocol/protocolgame.cpp src/game/game.cpp src/creatures/players/player.cpp

   P=src/server/network/protocol
   python3 tools/merge/split_merge.py --src $P/protocolgame.cpp \
       --ours $P/protocolgame.cpp $P/protocolgame_send.cpp $P/protocolgame_send_cyclopedia.cpp \
              $P/protocolgame_send_economy.cpp $P/protocolgame_send_social.cpp $P/protocolgame_helpers.hpp \
       --helpers $P/protocolgame_helpers.hpp

   python3 tools/merge/moved_merge.py --src src/game/game.cpp \
       src/game/game.cpp src/game/*/*_service.cpp src/creatures/*/*_service.cpp src/items/item_service.cpp \
       src/game/scheduling/monster_post_think_queue.cpp src/io/highscore_repository_db.cpp

   python3 tools/merge/moved_merge.py --src src/creatures/players/player.cpp \
       src/creatures/players/player.cpp src/creatures/players/components/player_*_component.cpp \
       src/creatures/players/components/player_race_icon_overlays.cpp
   ```

   Coverage commands (run after moving code and after the merge; each must report 0 missing):

   ```sh
   python3 tools/merge/coverage_check.py --src src/game/game.cpp \
       --allow-unmatched teleportStack tryInsertTeleportStack eraseTeleportStack pointerTeleportKey \
         teleportGuardKey pruneTeleportGuardStates shouldBlockTeleportRate recordTeleportGuardBlock \
         teleportGuardReasonName logTeleportGuardBlock playerCanUseItem removeItemFromOwner deliverChange \
         processMoneyEntry addItemToCylinder addCoinStack addCoins removeInventoryMoneyToBank \
         withdrawMarketFunds checkCanInitCreateMarketOffer processHighscoreResults -- \
       src/game/game.cpp 'src/game/*/*_service.cpp' 'src/creatures/*/*_service.cpp' src/items/item_service.cpp \
       src/game/scheduling/monster_post_think_queue.cpp src/io/highscore_repository_db.cpp
   python3 tools/merge/coverage_check.py --src src/creatures/players/player.cpp \
       src/creatures/players/player.cpp 'src/creatures/players/components/player_*_component.cpp' \
       src/creatures/players/components/player_race_icon_overlays.cpp
   python3 tools/merge/coverage_check.py --src $P/protocolgame.cpp $P/protocolgame.cpp $P/protocolgame_send.cpp \
       $P/protocolgame_send_cyclopedia.cpp $P/protocolgame_send_economy.cpp $P/protocolgame_send_social.cpp \
       $P/protocolgame_helpers.hpp
   ```

   The allowed names are the namespace helpers and the split functions listed below; when
   upstream changes one of them, port it by hand.

   Since Phase 18.5 `protocolgame_send.cpp` is itself split into four files; without the three
   `_send_*` files, 88 upstream functions would find no local copy. Coverage was checked in
   Phase 22: every upstream function of `protocolgame.cpp` and `player.cpp` finds a copy.
   For `game.cpp`, 20 namespace helpers do not match by name (see "Namespace helpers" below).

4. Review the tool output:
   - `merged_conflict*`: resolve the `<<<<<<< ours` / `>>>>>>> theirs` markers. In services and
     components, qualify upstream's implicit members (`game_.`, `m_player.`).
   - `merged_clean_moved`: also review. A clean text merge can still introduce unqualified
     `getID()`, `getName()` and similar calls. The compiler catches most of these.
   - `deleted_upstream_port_by_hand` and `excluded_insert`: signature changes. Update the
     declaration, the delegate and the implementation together.
   - `new_namespace_member_place_by_hand`: new anonymous-namespace helpers. Move the whole
     upstream namespace block into the right file.
   - `*_has_duplicates`: the function body exists more than once. Make sure the live copy got the change.
   - Move new upstream functions into services/components when they push `game.cpp` past
     4k lines or `player.cpp` past 7k lines.
5. Before committing, run `audit_moved.py` for `game.cpp` and `player.cpp` and expect zero misses.
6. Build, then run `syntax_check.sh` on the touched `.cpp` files and the unit/integration suites.
7. Update `vcproj/canary.vcxproj` for any added or removed source/header (see `AGENTS.md`).

## Monster datapack (Phase 24)

The fork's monster corrections live in `tools/datapack/fixes.json`. On a conflict in
`data-otservbr-global/monster/**`, take upstream's file and re-apply the manifest:

```sh
git checkout --theirs -- data-otservbr-global/monster/<path>.lua
python3 -I tools/datapack/wiki_check.py apply   # idempotent; never lowers a maxCount
```

Each fix records the value it replaces (`from`). `apply` changes a value only while the file
still holds that `from`; when upstream changed it to something else it prints
`changed upstream (...)`, exits 1 and leaves the file alone, so the conflict is decided by hand.
A `remove_loot` fix deletes an item's one-line entries only while they equal its `from`; an item
upstream already dropped is `already`, and an entry upstream spread over several lines is reported.
An `add_loot` whose item upstream now lists under another spelling or id is reported as
`listed as another entry (...)` instead of adding it twice. Then run the smoke boot (no loot
warnings, no Lua errors).

To prove the manifest still re-applies from upstream's side, apply it to a copy of the monster files
at a base commit and compare with the branch (expect no conflicts; at most a different order of loot
lines, which does not matter because every entry rolls on its own):

```sh
T=$(mktemp -d) && git archive <base> data-otservbr-global/monster | tar -x -C "$T"
python3 -I tools/datapack/wiki_check.py apply --root "$T"
diff -rq "$T/data-otservbr-global/monster" data-otservbr-global/monster
```

## Gotchas from Phase 17

- After a merge, check the `CMakeCache.txt` of `build/<preset>` for `find_*` results that point
  at an old `vcpkg_installed` tree. If the vcpkg baseline changed, recreate the preset
  directory. On Linux, export `VCPKG_ROOT`; the preset depends on it.
- When `vcpkg.json` moves the baseline, check out `VCPKG_ROOT` at that commit and re-run
  `bootstrap-vcpkg.sh`, matching `.github/scripts/bootstrap_vcpkg.py`.

## Receiver substitutions in components

Branch-owned player components call some `Game` methods through narrow ports instead of
`g_game()`. The ports mirror `Game`'s signatures, so only the receiver token differs. When an
upstream hunk touches one of these lines, keep the branch receiver and take upstream's
arguments. When upstream adds a new call to one of these methods in a moved function, use the
port receiver.

| Upstream (`player.cpp`) | Branch (`components/player_*_component.cpp`) | Port |
|---|---|---|
| `g_game().changeSpeed(` | `g_creatureFeedback().changeSpeed(` | `CreatureFeedback` |
| `g_game().changePlayerSpeed(` | `g_creatureFeedback().changePlayerSpeed(` | `CreatureFeedback` |
| `g_game().addCreatureHealth(` | `g_creatureFeedback().addCreatureHealth(` | `CreatureFeedback` |
| `g_game().addPlayerMana(` | `g_creatureFeedback().addPlayerMana(` | `CreatureFeedback` |
| `g_game().internalCreatureChangeOutfit(` | `g_creatureFeedback().internalCreatureChangeOutfit(` | `CreatureFeedback` |
| `g_game().reloadCreature(` | `g_creatureFeedback().reloadCreature(` | `CreatureFeedback` |
| `g_game().addMagicEffect(pos, effect)` | `g_creatureFeedback().addMagicEffect(pos, effect)` | `CreatureFeedback` |
| `g_game().sendSingleSoundEffect(` | `g_creatureFeedback().sendSingleSoundEffect(` | `CreatureFeedback` |
| `g_game().getPlayerByID(` | `g_worldRegistry().getPlayerByID(` | `WorldRegistry` |
| `g_game().getCreatureByID(` | `g_worldRegistry().getCreatureByID(` | `WorldRegistry` |
| `g_game().getPlayers(` | `g_worldRegistry().getPlayers(` | `WorldRegistry` |
| `g_game().addPlayer(` | `g_worldRegistry().addPlayer(` | `WorldRegistry` |
| `g_game().removePlayer(` | `g_worldRegistry().removePlayer(` | `WorldRegistry` |
| `g_game().removeCreature(` | `g_worldRegistry().removeCreature(` | `WorldRegistry` |
| `g_game().checkPlayersRecord(` | `g_worldRegistry().checkPlayersRecord(` | `WorldRegistry` |
| `g_game().getWorldType(` | `g_worldRegistry().getWorldType(` | `WorldRegistry` |

`g_configManager()` in components became `IConfigManager::getInstance()` (Phase 19.2).
Upstream-owned component files (`git ls-tree origin/main src/creatures/players/components`)
keep `g_game()`.

## Upstream files deleted on the branch

Upstream still has these files; the branch moved their code into adapters and deleted them.
A merge reports them as modify/delete conflicts. Port the upstream changes, then
`git rm` the upstream copy again.

| Upstream | Branch |
|---|---|
| `src/io/iomarket.{cpp,hpp}` (`IOMarket::*`) | `src/io/market_repository_db.cpp` (`MarketRepositoryDB::*`, SQL and statistics) |
| `IOMarket::processExpiredOffers`, `IOMarket::checkExpiredOffers` | `src/game/market/market_expiry.cpp` (`market_expiry::*`) plus `MarketRepositoryDB::loadExpiredOffers` |
| `IOMarket::getTierFromDatabaseTable` | anonymous-namespace `getTierFromDatabaseTable(value, maxTier)` in `market_repository_db.cpp` |
| `src/io/ioguild.{cpp,hpp}` (`IOGuild::*`) | `src/io/guild_repository_db.cpp` (`GuildRepositoryDB::*`) |
| VIP block of `src/io/iologindata.cpp` (`IOLoginData::*VIP*`) | `src/creatures/players/vip_repository_db.cpp` (`VipRepositoryDB::*`) |
| `IOLoginData::getNameByGuid`, `getGuidByName`, `getGuidByNameEx`, `formatPlayerName`, `increaseBankBalance` | `src/creatures/players/player_repository_db.cpp` (`PlayerRepositoryDB::*`) |

`moved_merge.py` reads `--src` from the base and upstream revisions, so it works with a
deleted local file. Keys are matched without the class name (`IOMarket::x` ↔
`MarketRepositoryDB::x`):

```sh
python3 tools/merge/moved_merge.py --src src/io/iomarket.cpp src/io/market_repository_db.cpp src/game/market/market_expiry.cpp
python3 tools/merge/moved_merge.py --src src/io/ioguild.cpp src/io/guild_repository_db.cpp
python3 tools/merge/moved_merge.py --src src/io/iologindata.cpp src/io/iologindata.cpp src/creatures/players/vip_repository_db.cpp src/creatures/players/player_repository_db.cpp
git rm src/io/iomarket.cpp src/io/iomarket.hpp src/io/ioguild.cpp src/io/ioguild.hpp
```

Then:

- The adapters swapped the receivers: `Database::getInstance()`/`g_database()` → `db_` and
  `g_configManager()` → `config_`. In the tier helper, `getTierFromDatabaseTable(x)` became
  `getTierFromDatabaseTable(x, maxTier)`. Conflicts on those lines keep the adapter's receiver
  and take upstream's logic.
- `processExpiredOffers` and `getTierFromDatabaseTable` do not match automatically, because
  they are namespaced and have new signatures. Diff them by hand with
  `git diff <base> origin/main -- src/io/iomarket.cpp`. The expiry body reads an
  `ExpiredMarketOffer` instead of a `DBResult` row.
- New upstream `IOMarket`/`IOGuild` functions land in the home file as `IOMarket::foo`. Turn
  them into adapter members and declare them in the port; check `inserted_home`.
- The `iologindata` run reports `gameWorldAuthentication`, `loadPlayerById` and `savePlayer`
  as `*_has_duplicates`. The live bodies are in `iologindata.cpp`; the copies in
  `player_repository_db.cpp` are one-line delegates. The tool picks the copy most similar to
  the base, which is the live body, so leave the delegates alone.
- New upstream VIP and name/guid/balance functions land in `iologindata.cpp` (home). Move them
  to `VipRepositoryDB` or `PlayerRepositoryDB` when they are plain SQL; leave player
  load/save there.

## Namespace helpers of `game.cpp` that never match by name

The chunker keys namespace members separately, and some were renamed. When upstream changes
them, port the change by hand (`git diff <base> origin/main -- src/game/game.cpp`).

| Upstream `game.cpp` | Branch |
|---|---|
| `InternalGame` teleport guard: `teleportStack`, `tryInsertTeleportStack`, `eraseTeleportStack`, `pointerTeleportKey`, `teleportGuardKey`, `pruneTeleportGuardStates`, `shouldBlockTeleportRate`, `recordTeleportGuardBlock`, `teleportGuardReasonName`, `logTeleportGuardBlock` | `src/game/movement/teleport_guard.cpp` (`TeleportGuard::tryEnterStack`, `pointerKey`, `guardKey`, `pruneStates`, `shouldBlockRate`, `recordBlock`, ...) |
| `InternalGame::playerCanUseItemOnHouseTile` | `src/game/game_helpers.hpp` (used by the interaction and outfit services) |
| `InternalGame::playerCanUseItemWithOnHouseTile` | removed: dead code upstream too |
| money helpers `removeItemFromOwner`, `deliverChange`, `processMoneyEntry`, `addItemToCylinder`, `addCoinStack`, `addCoins` | anonymous namespace of `src/items/item_service.cpp` |
| market helpers `removeInventoryMoneyToBank`, `withdrawMarketFunds`, `checkCanInitCreateMarketOffer` | anonymous namespace of `src/game/market/market_service.cpp` |

## Call sites of deleted or wrapped IO statics

Upstream code outside `src/io` still calls the IO statics. On the branch every such call goes
through a port, so a new upstream call must be rewritten when it is merged:

| Upstream call | Branch call |
|---|---|
| `IOLoginData::loadPlayerById(`, `loadPlayerByName(`, `savePlayer(`, `getGuidByName(`, `getNameByGuid(`, `getGuidByNameEx(`, `formatPlayerName(`, `increaseBankBalance(`, `gameWorldAuthentication(` | `g_playerRepository().<same>(` |
| `IOLoginData::getVIPEntries(` and the other VIP functions | `g_vipRepository().<same>(` |
| `IOMarket::<x>(` | `g_marketRepository().<x>(` (`checkExpiredOffers` → `market_expiry::checkExpiredOffers()`) |
| `IOGuild::<x>(` | `g_guildRepository().<x>(` |

A stray upstream call still compiles for `IOLoginData` (load/save remain there) and silently
bypasses the port. After every merge this must print nothing:

```sh
git grep -nE 'IOLoginData::|IOMarket::|IOGuild::' -- src ':!src/io/iologindata*' ':!src/io/functions/*' ':!*_repository_db.cpp'
```

## Upstream functions split between a service and an adapter

`coverage_check.py` still finds these by name (the service keeps it), but part of the body now
lives in an adapter. When upstream changes one of them, diff it by hand and send each line to
the half that owns it.

| Upstream function | Service half (game logic) | Adapter half (SQL) |
|---|---|---|
| `Game::updatePlayersOnline` (`game.cpp`) | `CyclopediaService::updatePlayersOnline` builds the online GUID list | `PlayerRepositoryDB::updateOnlinePlayers` (count/clear, insert, prune, one transaction) |
| `Game::playerHighscores` (`game.cpp`) | `HighscoreService::playerHighscores` picks the request and keeps the async-task flag | `HighscoreRepositoryDB::loadEntries`/`loadOurRank` run the query |
| `Game::loadItemsPrice` (`game.cpp`) | `Game::loadItemsPrice` chains two async loads; `Game::rebuildItemsPrice` holds the old averaging/max loops | `MarketRepositoryDB::loadStatistics` / `loadActiveOffers` (queries and parsing shared with `updateStatistics` / `getActiveOffers`) |
| `Game::processHighscoreResults(const DBResult_ptr &, ...)` (`game.cpp`) | `HighscoreService::processHighscoreResults(Game &, std::optional<HighscorePage>, ...)` (static; result cache and sending) | `HighscoreRepositoryDB::readHighscorePage` (row parsing) |

The highscore query helpers `generateHighscoreQuery`, `generateVocationConditionHighscore`,
`cacheQueryHighscore` and `generateHighscoreOrGetCachedQueryFor{Entries,OurRank}` moved whole to
`src/io/highscore_repository_db.cpp` with the upstream names and signatures, and `moved_merge.py`
matches them there. The query cache moved too: `QueryHighscoreCacheEntry` and `queryCache` left
`game.hpp`, and the private `Game` delegates were dropped. When upstream edits those lines in
`game.hpp`, drop them again.

