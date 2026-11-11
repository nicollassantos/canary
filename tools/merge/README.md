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
