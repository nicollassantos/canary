# Implementation Plan: Merge main into nicollassantos/solid (Phase 14)

## Overview

Merge 6 upstream commits from `main` into `nicollassantos/solid`, preserving all
SOLID/Hexagonal refactoring (576/576 tests) and porting each fix to the correct
architectural layer. The last merge was `5599e2675` (v3.6.0). This plan covers the
6 new commits added to main after that point.

---

## Upstream Changes

| Commit | Summary | Files | Conflict risk |
|--------|---------|-------|---------------|
| `36b7fd192` | fix: remove first-step auto-walk special case | `creature.cpp/hpp`, `game.cpp` | **HIGH** — game.cpp is 4k vs 12.8k on main; movement_service.cpp has the call sites |
| `0c02da916` | perf(database): reduce query allocation | `database.cpp`, `iologindata.cpp`, `iologindata_save_player.cpp` | MEDIUM — iologindata.cpp trimmed from 1925→450 lines |
| `e6d575bed` | fix(market): sort active offers by price | `iomarket.cpp` | LOW — iomarket.cpp still present (410 lines) |
| `edb8d3725` | fix: plural Mazarius NPC keywords | `mazarius.lua` | None |
| `f19dd8b28` | docs: reorganize project docs | docs only | None |
| `39bf7692c` | ci(release): materialize tags before publishing | CI only | None |

---

## Architecture Decisions

- **`WalkStartPolicy` lives in `Creature`** — the enum is part of creature movement
  semantics, not game orchestration. No SOLID boundary needs to change.
- **`startAutoWalk` call sites in `MovementService`** — `playerAutoWalk` and
  `forcePlayerAutoWalk` in `movement_service.cpp` must receive the same policy
  treatment as the main-branch `game.cpp` calls they replaced.
- **`game.cpp` playerMove / forcePlayerMove** — these two remain in `game.cpp` (not
  extracted yet). Apply `ImmediateWhenReady` there directly.
- **`iomarket.cpp` market sort** — the fix touches the `IOMarket` static layer which
  still exists; apply it there. Our `IMarketRepository` wraps this, so no adapter
  change needed.
- **`iologindata.cpp` DB perf** — our branch has this file trimmed to ~450 lines;
  most of the save/load hunks live in `iologindata_save_player.cpp`. Apply hunks
  that match existing code; skip hunks for code we already extracted.

---

## Phase 14: Pre-merge check

### Task 14.1 — Verify tests green before merge

**Acceptance criteria:**
- [ ] `canary_ut` builds and all 576 tests pass

**Verification:**
```bash
cd build/linux-release-enabled-tests
ninja canary_ut 2>&1 | tail -5
./tests/unit/canary_ut --gtest_brief=1 2>&1 | tail -3
```

**Dependencies:** None  
**Scope:** XS

---

## Phase 14: Manual cherry-pick of upstream fixes

### Task 14.2 — Apply first-step fix (36b7fd192)

This commit changes creature auto-walk scheduling so every step obeys thinking
constraints. Three sub-tasks:

**14.2a — `creature.hpp`**  
Add `WalkStartPolicy` enum (before `getEventStepTicks`); update signatures:
- `getEventStepTicks(WalkStartPolicy)` replacing `getEventStepTicks(bool)`
- `startAutoWalk(..., WalkStartPolicy startPolicy = WalkStartPolicy::RespectDelay)`
- `addEventWalk(WalkStartPolicy startPolicy = WalkStartPolicy::RespectDelay)`

**14.2b — `creature.cpp`**  
Apply all 3 hunks verbatim:
1. `startAutoWalk` signature + `addEventWalk(startPolicy)` call
2. `addEventWalk` body: move `eventWalk != 0` guard inside `safeCall`; use `startPolicy`
3. `getEventStepTicks`: flip logic from `onlyDelay` bool to `WalkStartPolicy`

**14.2c — `game.cpp`** (our orchestrator layer)  
Two call sites at current lines 1621 and 1635:
- `playerMove`: add `Creature::WalkStartPolicy::ImmediateWhenReady` as 3rd arg
- `forcePlayerMove`: same

**14.2d — `movement_service.cpp`**  
Apply same policy pattern from main's `playerAutoWalk` / `forcePlayerAutoWalk`:
```cpp
// playerAutoWalk (line 320)
const auto startPolicy = listDir.size() == 1
    ? Creature::WalkStartPolicy::ImmediateWhenReady
    : Creature::WalkStartPolicy::RespectDelay;
player->startAutoWalk(listDir, false, startPolicy);

// forcePlayerAutoWalk (line 335)
const auto startPolicy = listDir.size() == 1
    ? Creature::WalkStartPolicy::ImmediateWhenReady
    : Creature::WalkStartPolicy::RespectDelay;
player->startAutoWalk(listDir, true, startPolicy);
```

**Acceptance criteria:**
- [ ] `WalkStartPolicy` enum declared in `creature.hpp`
- [ ] `addEventWalk(bool)` signature gone; replaced with `addEventWalk(WalkStartPolicy)`
- [ ] `game.cpp` playerMove/forcePlayerMove use `ImmediateWhenReady`
- [ ] `movement_service.cpp` uses policy for single vs multi-step
- [ ] Build compiles (no `bool` → `WalkStartPolicy` implicit conversion warnings)

**Files:**
- `src/creatures/creature.hpp`
- `src/creatures/creature.cpp`
- `src/game/game.cpp`
- `src/game/movement/movement_service.cpp`

**Scope:** M | **Dependencies:** Task 14.1

---

### Task 14.3 — Apply DB perf fix (0c02da916)

Apply hunks from `iologindata.cpp` that touch code still present in our file
(VIP entries, VIP groups, bank balance). Skip hunks for save/load functions
already extracted to `iologindata_save_player.cpp` or adapters.

Apply all of `iologindata_save_player.cpp` changes (query formatting).  
Apply `database.cpp` changes (DBInsert allocation reduction).

**Acceptance criteria:**
- [ ] No `Database::getInstance()` added by this patch (we removed those)
- [ ] VIP query formatting improved
- [ ] Build passes

**Files:**
- `src/database/database.cpp`
- `src/io/iologindata.cpp`
- `src/io/functions/iologindata_save_player.cpp`

**Scope:** S | **Dependencies:** Task 14.1

---

### Task 14.4 — Apply market sort fix (e6d575bed)

Two line additions to `src/io/iomarket.cpp`:
1. `getActiveOffers`: add `ORDER BY price <priceOrder>, created ASC`
2. `getOwnOffers`: add `ORDER BY created ASC`

**Acceptance criteria:**
- [ ] Active buy offers sorted DESC by price
- [ ] Active sell offers sorted ASC by price
- [ ] Own offers sorted by created ASC

**Files:**
- `src/io/iomarket.cpp`

**Scope:** XS | **Dependencies:** Task 14.1

---

### Task 14.5 — Accept remaining files wholesale

- `data-otservbr-global/npc/mazarius.lua` — NPC plural keyword fix (no conflict)
- `docs/**` — accept main's new docs structure
- `.github/workflows/**` — accept CI changes
- `docker/docker-compose.yml`, `docker/quickstart/myaac/Dockerfile` — accept

**Acceptance criteria:**
- [ ] No unresolved conflict markers in any file

**Scope:** XS | **Dependencies:** Task 14.1

---

### Checkpoint: All patches applied
- [ ] Build compiles
- [ ] No conflict markers in any file

---

## Phase 14: Build and test

### Task 14.6 — Build + run full test suite

**Acceptance criteria:**
- [ ] `ninja canary_ut` builds without errors
- [ ] ≥576 tests pass (0 failures)

**Verification:**
```bash
cd build/linux-release-enabled-tests
ninja canary_ut 2>&1 | tail -10
./tests/unit/canary_ut --gtest_brief=1 2>&1 | tail -5
```

**Dependencies:** Tasks 14.2–14.5  
**Scope:** XS

---

### Task 14.7 — Add regression tests for WalkStartPolicy (TDD)

Write in `tests/unit/creatures/` or `tests/unit/game/`:

**14.7a** — `WalkStartPolicy::RespectDelay` → `getEventStepTicks` returns full step duration  
**14.7b** — `WalkStartPolicy::ImmediateWhenReady` + no walk delay → returns 1 (instant)  
**14.7c** — `WalkStartPolicy::ImmediateWhenReady` + active walk delay → returns delay (respects it anyway)

**Acceptance criteria:**
- [ ] ≥3 tests written and GREEN
- [ ] Total test count increases from 576

**Files:**
- `tests/unit/creatures/walk_start_policy_test.cpp`
- Update `tests/unit/creatures/CMakeLists.txt`

**Dependencies:** Task 14.6  
**Scope:** S

---

### Task 14.8 — Commit merge

Single merge commit with all upstream changes incorporated:

```
merge(main): integrate upstream fixes into nicollassantos/solid

- fix: remove first-step auto-walk special case (36b7fd192)
  Ported WalkStartPolicy to creature.hpp/cpp, game.cpp, movement_service.cpp
- perf(database): reduce query construction allocations (0c02da916)
- fix(market): sort active offers by price (e6d575bed)
- fix: Mazarius NPC plural keywords (edb8d3725)
- docs: reorganize project docs (f19dd8b28)
- ci(release): materialize tags (39bf7692c)
```

Update `tasks/todo.md` Phase 14 entries.

**Dependencies:** Task 14.7  
**Scope:** XS

---

## Checkpoint: Phase 14 Complete
- [ ] ≥579 tests pass
- [ ] Merge commit created
- [ ] `tasks/todo.md` updated with Phase 14 ✓

---

## Phase 15: Next Coverage Targets (Post-Merge)

These are the highest-value next steps after the merge lands:

| Target | Size | Tests possible | Notes |
|--------|------|----------------|-------|
| `MovementService` (1900+ lines) | M | 10+ | `internalMoveCreature`, `internalTeleport` — testable with TeleportGuard already extracted |
| `TradeService` | M | 8+ | Trade request/accept/cancel guards |
| `creature.cpp` auto-walk behavior | M | 5+ | Use WalkStartPolicy now properly typed |
| `HighscoreService` | S | 6+ | Pure logic, injectable |
| `OutfitService` | S | 5+ | Outfit change guards |
| `IOLoginData` remaining | S | 8+ | Still 450 lines; VIP/bank now tested |

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| `creature.hpp` bool→enum breaks other callers of `addEventWalk(bool)` | High | Search all callers before changing signature; default param covers most |
| `iologindata.cpp` DB perf hunks don't apply cleanly (our file is 450 vs 1925 lines) | Med | Apply only matching hunks; skip extracted code |
| `WalkStartPolicy` enum conflicts with existing code patterns | Low | Enum class scoped, no name collision |
