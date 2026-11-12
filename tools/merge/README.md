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

## Workflow

1. Start from a clean tree and a green baseline build/test run.
2. `git merge --no-commit origin/main`. Since Phase 17 the merge base is a real upstream
   commit, so only new upstream commits are in play.
3. Resolve ordinary conflicts as usual. For moved-code files, reset to ours and merge by function:

   ```sh
   git checkout --ours -- src/server/network/protocol/protocolgame.cpp src/game/game.cpp src/creatures/players/player.cpp

   P=src/server/network/protocol
   python3 tools/merge/split_merge.py --src $P/protocolgame.cpp \
       --ours $P/protocolgame.cpp $P/protocolgame_send.cpp $P/protocolgame_helpers.hpp \
       --helpers $P/protocolgame_helpers.hpp

   python3 tools/merge/moved_merge.py --src src/game/game.cpp \
       src/game/game.cpp src/game/*/*_service.cpp src/creatures/*/*_service.cpp src/items/item_service.cpp

   python3 tools/merge/moved_merge.py --src src/creatures/players/player.cpp \
       src/creatures/players/player.cpp src/creatures/players/components/player_*_component.cpp
   ```

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

`moved_merge.py` reads `--src` from the base and upstream revisions, so it works with a
deleted local file. Keys are matched without the class name (`IOMarket::x` ↔
`MarketRepositoryDB::x`):

```sh
python3 tools/merge/moved_merge.py --src src/io/iomarket.cpp src/io/market_repository_db.cpp src/game/market/market_expiry.cpp
python3 tools/merge/moved_merge.py --src src/io/ioguild.cpp src/io/guild_repository_db.cpp
python3 tools/merge/moved_merge.py --src src/io/iologindata.cpp src/io/iologindata.cpp src/creatures/players/vip_repository_db.cpp
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
- New upstream VIP functions land in `iologindata.cpp` (home). Move them to `VipRepositoryDB`.
