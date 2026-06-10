# Implementation Plan: Merge main into nicollassantos/solid

## Overview

Merge 4 upstream commits from `main` into `nicollassantos/solid`, preserving all SOLID/Hexagonal
refactoring and increasing test coverage where main introduced new behavior.

## Upstream Changes (main)

| Commit | What | Files |
|--------|------|-------|
| `6725cfa` | fix(container): page index bounds + isNearDepotBox() const + shouldCloseContainer fix | player.cpp, player.hpp, batch_update_functions.cpp |
| `a8e2ceac` | fix(teleport): anti-recursive teleport guard (+219 lines to InternalGame namespace) | game.cpp |
| `9e795c57` | feat: release workflow + MyAAC client 1501→1511 | CI files, docker-compose.yml |
| `1df549ec` | chore: release metadata 3.5.0→3.6.0 | config.lua.dist, docker-compose.yml, core.hpp |

## Architecture Decisions

- **Container fix must land in `PlayerStashComponent`**, not in `player.cpp`, since we delegated
  `sendBatchUpdateContainer` and `isNearDepotBox` there. Porting the fix to the component keeps
  the SOLID boundary intact and avoids re-inlining logic that belongs to the component.
- **Teleport fix lives in `game.cpp` InternalGame namespace** — it is an anti-recursion guard on
  the existing `Game` layer, not a component candidate. Accept the addition as-is.
- **CI/config files** — accept main's version wholesale (no conflict with our work).

## Conflict Forecast

| File | Expected conflict? | Strategy |
|------|--------------------|----------|
| `docker/docker-compose.yml` | None (different sections) | Auto-merge or accept both |
| `player.hpp` | Minor (`const` on `isNearDepotBox`) | Accept main's `const`, keep our signature |
| `player.cpp` | Yes (component constructors vs inline fixes) | Keep delegate + apply const + apply shouldCloseContainer fix |
| `game.cpp` | Yes (we removed 8k lines; main added 219) | Abort game.cpp merge, manually cherry-pick teleport block |
| `src/lua/functions/core/game/batch_update_functions.cpp` | None | Accept main's version |
| CI/version files | None | Accept main's version |

---

## Phase 1: Pre-merge snapshot

### Task 1.1 — Verify tests green before merge
**Acceptance criteria:**
- [ ] `canary_ut` builds and all 576 tests pass

**Verification:**
```bash
cd build/linux-release-enabled-tests && ninja canary_ut 2>&1 | tail -5
./tests/unit/canary_ut --gtest_brief=1 2>&1 | tail -3
```

**Dependencies:** None  
**Scope:** XS — read-only check

---

## Phase 2: Merge and conflict resolution

### Task 2.1 — Merge main
**Acceptance criteria:**
- [ ] `git merge main` started (may stop with conflicts — that is expected)
- [ ] Conflict list captured

**Verification:**
```bash
git merge main
git status | grep "^UU\|both modified"
```

**Dependencies:** Task 1.1  
**Scope:** XS

---

### Task 2.2 — Resolve `player.hpp`
Make `isNearDepotBox()` `const` to match main.

**Acceptance criteria:**
- [ ] Declaration reads `bool isNearDepotBox() const;`

**Files:**
- `src/creatures/players/player.hpp`

**Dependencies:** Task 2.1  
**Scope:** XS

---

### Task 2.3 — Resolve `player.cpp`
Keep our delegate pattern; apply the two functional changes from main:
1. `isNearDepotBox()` → `isNearDepotBox() const` (the wrapper delegates to stash component)
2. `shouldCloseContainer`: change `return false;` → `return !isNearDepotBox();`

**Acceptance criteria:**
- [ ] `Player::isNearDepotBox() const` compiles (delegates to `m_stashComponent.isNearDepotBox()`)
- [ ] `shouldCloseContainer` returns `!isNearDepotBox()` for the depot chest case (not `false`)
- [ ] No inline re-implementation — delegate pattern preserved

**Files:**
- `src/creatures/players/player.cpp`

**Dependencies:** Task 2.2  
**Scope:** S

---

### Task 2.4 — Port `sendBatchUpdateContainer` page fix to `PlayerStashComponent`
Main's fix adds bounds-check on `containerInfo.index` before calling `sendContainer`.
Apply exact same logic to `PlayerStashComponent::sendBatchUpdateContainer`.

**Acceptance criteria:**
- [ ] Loop uses `auto &[cid, containerInfo]` (not `const auto &`)
- [ ] Before calling `client->sendContainer`, index is clamped to valid page boundary
- [ ] Zero-size container case handled (`firstIndex = 0`)

**Files:**
- `src/creatures/players/components/player_stash_component.cpp`

**Dependencies:** Task 2.1  
**Scope:** S

---

### Task 2.5 — Make `PlayerStashComponent::isNearDepotBox()` `const`
Required because `Player::isNearDepotBox() const` delegates to it.

**Acceptance criteria:**
- [ ] Header declares `bool isNearDepotBox() const;`
- [ ] Implementation is `bool PlayerStashComponent::isNearDepotBox() const {`
- [ ] No internal mutable state prevents this

**Files:**
- `src/creatures/players/components/player_stash_component.hpp`
- `src/creatures/players/components/player_stash_component.cpp`

**Dependencies:** Task 2.3  
**Scope:** XS

---

### Task 2.6 — Apply teleport fix to `game.cpp`
The merge will likely conflict here because our `game.cpp` is 3988 lines (was 12269).
Strategy: resolve by accepting our file as base and manually inserting the teleport block.

The teleport block goes into `namespace InternalGame {}` (our file: line 75).
The new code adds: constants, `teleportStack()`, `tryInsertTeleportStack`, `eraseTeleportStack`,
`TeleportStackCleaner`, `TeleportGuardReason` enum, and supporting functions (total ~111 lines of
pure additions before `sendBlockEffect`).

**Acceptance criteria:**
- [ ] `InternalGame::teleportStack()` exists
- [ ] `TeleportStackCleaner` struct exists
- [ ] `TeleportGuardReason` enum exists
- [ ] `sendBlockEffect` remains unchanged at its current position

**Files:**
- `src/game/game.cpp`

**Dependencies:** Task 2.1  
**Scope:** M

---

### Task 2.7 — Resolve remaining files
Accept main's version for:
- `src/lua/functions/core/game/batch_update_functions.cpp` (minor bounds fix)
- `docker/docker-compose.yml` (map URL v3.6.0, MyAAC 1511; keep our login-server network_mode:host)
- `src/core.hpp`, `config.lua.dist` (version bumps)
- All `.github/workflows/` files
- `docker/.env.dist`, `docker/DOCKER.md`, `docker/data/start.sh`
- `docker/quickstart/myaac/bootstrap.php`

**Acceptance criteria:**
- [ ] No unresolved conflict markers (`<<<<<<<`) in any file
- [ ] `git status` shows all files staged

**Dependencies:** Task 2.1  
**Scope:** S

---

### Checkpoint: Merge complete
- [ ] `git status` shows no conflicts
- [ ] All modified files staged
- [ ] Ready to build

---

## Phase 3: Build and test

### Task 3.1 — CMake reconfigure + build
**Acceptance criteria:**
- [ ] `cmake` reconfigure succeeds
- [ ] `ninja canary_ut` builds without errors

**Verification:**
```bash
cd build/linux-release-enabled-tests
cmake ../.. -DENABLE_TESTS=ON 2>&1 | tail -5
ninja canary_ut 2>&1 | tail -10
```

**Dependencies:** Phase 2 complete  
**Scope:** XS

---

### Task 3.2 — Run full test suite
**Acceptance criteria:**
- [ ] ≥576 tests pass (0 failures)

**Verification:**
```bash
./tests/unit/canary_ut --gtest_brief=1 2>&1 | tail -5
```

**Dependencies:** Task 3.1  
**Scope:** XS

---

### Task 3.3 — Add regression tests for main's fixes (TDD)
Write tests covering the two bug fixes from main that now live in our components:

**3.3a** — `PlayerStashComponent::sendBatchUpdateContainer` page-clamping (2–3 tests)
- Empty container → firstIndex stays 0
- Container smaller than current index → index clamped to valid page

**3.3b** — `shouldCloseContainer` returns true when away from depot
- Mock/stub `isNearDepotBox()` returning false → shouldCloseContainer returns true for depot

**Acceptance criteria:**
- [ ] Tests written in `tests/unit/players/components/`
- [ ] Tests are RED first, then GREEN after confirming behavior
- [ ] Total test count increases

**Dependencies:** Task 3.2  
**Scope:** M

---

### Checkpoint: Phase 3 complete
- [ ] All tests pass (≥579 expected after new tests)
- [ ] Merge commit created
- [ ] `tasks/todo.md` updated with Phase 14

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| `game.cpp` merge conflict too large | High | Manually apply teleport block to our file |
| `const` propagation breaks stash component | Med | Task 2.5 handles it explicitly |
| Teleport fix references symbols we moved/renamed | Med | Read the teleport block carefully before inserting |
| New regression from container page fix | Low | Task 3.3 adds regression tests |
