# Implementation Plan: Merge main into nicollassantos/solid (Phase 16)

## Overview

Merge 10 upstream commits from `main` into `nicollassantos/solid`, preserving all
SOLID/Hexagonal refactoring (655/655 tests). The last merge was Phase 14 (commit
`39bf7692c`). This plan covers the 10 new commits added to `main` after that point.

---

## Upstream Changes

| # | Commit | Summary | Conflict risk |
|---|--------|---------|---------------|
| 1 | `5aeca4f6d` | chore: update vcpkg baseline to 2026.06.24 | None — only `vcpkg.json` |
| 2 | `b3d9188cb` | fix(workflows): auto-close stale PRs | None — CI only |
| 3 | `3b9b6f0c6` | fix(ci): validate git refs in lua api check | None — CI only |
| 4 | `bce1f6308` | ci(release): retag via git push | None — CI only |
| 5 | `bd6217f37` | fix(forge): prevent same-item double-removal | **MEDIUM** — `getForgeItemFromId` moved to `player_forge_component.cpp` on our branch |
| 6 | `5ee38ce90` | feat: 15.13 NPC module protocol + NPC Lua scripts | **MEDIUM** — `AddPlayerStats` in `protocolgame_send.cpp`; `game.cpp` line numbers differ; 1044 NPC Lua files |
| 7 | `68181dcdc` | chore: update release metadata to 3.6.1 | LOW — version bump in `core.hpp`, docker/CI files |
| 8 | `d415619c8` | chore: normalize YAML document headers | None — docs/YAML only |
| 9 | `82b4778a5` | feat(protocol): add runtime multiprotocol client profiles | **HIGH** — 3607 insertions; 87 hunks in `protocolgame.cpp`; our branch split that file into `protocolgame.cpp` (3555 lines) + `protocolgame_send.cpp`; many new files to add |
| 10 | `60f9c62af` | fix(release): preserve main client metadata | LOW — `core.hpp` CLIENT_VERSION 1511→1513, docker/CI |

Final state on `origin/main`: `SERVER_RELEASE_VERSION = "3.6.1"`, `CLIENT_VERSION = 1513`.

---

## Architecture Decisions

- **Forge fix applies to `PlayerForgeComponent`** — `getForgeItemFromId`, `forgeFuseItems`, `forgeTransferItemTier` live in `src/creatures/players/components/player_forge_component.cpp`. The fix signature change + call sites must be applied there. The delegate stubs in `player.cpp` and the declaration in `player.hpp` also need updating.
- **`AddPlayerStats` lives in `protocolgame_send.cpp`** — the NPC protocol commit's level-percent-as-uint16 change (for clientVersion >= 1513) must be applied to `protocolgame_send.cpp:5686`, not `protocolgame.cpp`.
- **Multiprotocol new files accepted wholesale** — `protocol_profile.cpp/hpp`, `transport_codec.cpp/hpp`, `protocol_session_hint.cpp/hpp`, `protocol_port_utils.hpp` are new abstractions with no conflicts.
- **Multiprotocol changes to existing files** — hunks in `protocolgame.cpp` that touch `parse*` and `login/connect/onRecvFirstMessage` stay in our `protocolgame.cpp`; hunks touching `send*`, `Add*`, `AddCreature`, `AddItem` etc. go to `protocolgame_send.cpp`.
- **`game.cpp::start()`** — the multiprotocol port-selection logic goes into our `game.cpp` (still present, line ~739).
- **`game.cpp::playerNpcGreet`** — SPEECHBUBBLE_SAILOR else-if applies at our line 2992.

---

## Dependency Graph

```
Task 16.1 (verify green)
    │
    ├── Task 16.2 (no-conflict wholesale) ─── no deps
    ├── Task 16.3 (forge fix) ─────────────── no deps
    ├── Task 16.4 (NPC protocol C++) ────────── no deps
    ├── Task 16.5 (release metadata) ────────── no deps
    ├── Task 16.6 (multiprotocol new files) ─── no deps
    │
    └── Task 16.7 (multiprotocol existing files)
            │
            └── Task 16.8 (build + tests)
                    │
                    └── Task 16.9 (forge integration tests)
                            │
                            └── Task 16.10 (commit merge)
```

Tasks 16.2–16.6 are independent and can be done in any order after 16.1.

---

## Phase 16: Pre-merge check

### Task 16.1 — Verify 655/655 tests green before merge

**Acceptance criteria:**
- [ ] `canary_ut` builds cleanly
- [ ] All 655 tests pass (0 failures)

**Verification:**
```bash
cd build/linux-release-enabled-tests
ninja canary_ut 2>&1 | tail -5
./tests/unit/canary_ut --gtest_brief=1 2>&1 | tail -3
```

**Dependencies:** None
**Scope:** XS

---

## Phase 16: No-conflict wholesale commits

### Task 16.2 — Accept CI / workflows / vcpkg / docs (5aeca4f6d, b3d9188cb, 3b9b6f0c6, bce1f6308, d415619c8)

Checkout these files directly from `origin/main`:
- `.github/workflows/` — all workflow files
- `vcpkg.json` — baseline update

**Acceptance criteria:**
- [ ] No conflict markers in any file
- [ ] `vcpkg.json` baseline matches `origin/main`

**Dependencies:** Task 16.1
**Scope:** XS

---

## Phase 16: Surgical fixes

### Task 16.3 — Apply forge fix (bd6217f37)

Fix: `getForgeItemFromId` needs an optional `exclude` param so the same item
instance cannot be selected twice in fuse/transfer flows.

**Files to modify on our branch (NOT main's locations):**

**`src/creatures/players/components/player_forge_component.cpp`** (line 53):
```cpp
// BEFORE:
std::shared_ptr<Item> PlayerForgeComponent::getForgeItemFromId(uint16_t itemId, uint8_t tier) const {
    for (const auto &item : m_player.getAllInventoryItems(true)) {
        if (!item) {

// AFTER:
std::shared_ptr<Item> PlayerForgeComponent::getForgeItemFromId(uint16_t itemId, uint8_t tier, const std::shared_ptr<Item> &exclude /*= nullptr*/) const {
    for (const auto &item : m_player.getAllInventoryItems(true)) {
        if (!item || item == exclude) {
```

**`src/creatures/players/components/player_forge_component.cpp`** (call sites):
- `forgeFuseItems` line ~190: `getForgeItemFromId(secondItemId, tier)` → `getForgeItemFromId(secondItemId, tier, firstForgingItem)`
- `forgeTransferItemTier` line ~486: `getForgeItemFromId(receiveItemId, 0)` → `getForgeItemFromId(receiveItemId, 0, donorItem)`

**`src/creatures/players/player.cpp`** (delegate, line 3092):
```cpp
// BEFORE:
std::shared_ptr<Item> Player::getForgeItemFromId(uint16_t itemId, uint8_t tier) const {
// AFTER:
std::shared_ptr<Item> Player::getForgeItemFromId(uint16_t itemId, uint8_t tier, const std::shared_ptr<Item> &exclude /*= nullptr*/) const {
    // Update call to m_forgeComponent.getForgeItemFromId(itemId, tier, exclude)
```

**`src/creatures/players/player.hpp`** (declaration, line ~1619):
```cpp
// BEFORE:
std::shared_ptr<Item> getForgeItemFromId(uint16_t itemId, uint8_t tier) const;
// AFTER:
std::shared_ptr<Item> getForgeItemFromId(uint16_t itemId, uint8_t tier, const std::shared_ptr<Item> &exclude = nullptr) const;
```

Also check `src/creatures/players/components/player_forge_component.hpp` for the declaration and update it.

**Acceptance criteria:**
- [ ] `getForgeItemFromId` signature has `exclude` param in all 4 locations
- [ ] Call sites in `forgeFuseItems` and `forgeTransferItemTier` pass the first item as `exclude`
- [ ] Build passes; 655+ tests green

**Dependencies:** Task 16.1
**Scope:** S

---

### Task 16.4 — Apply NPC protocol changes (5ee38ce90)

**C++ changes (surgical):**

**`src/creatures/creatures_definitions.hpp`** — add enum values after `SPEECHBUBBLE_QUESTTRADER = 4`:
```cpp
SPEECHBUBBLE_SAILOR = 5,
// Banker falls back to trade icon for client compatibility.
SPEECHBUBBLE_BANKER = 2,
```

**`src/lua/functions/core/game/lua_enums.cpp`** — add after `registerEnum(L, SPEECHBUBBLE_QUESTTRADER)`:
```cpp
registerEnum(L, SPEECHBUBBLE_SAILOR);
registerEnum(L, SPEECHBUBBLE_BANKER);
```

**`src/game/game.cpp`** (line ~2992, playerNpcGreet):
```cpp
// BEFORE:
if (npc->getSpeechBubble() == SPEECHBUBBLE_TRADE) {
    internalCreatureSay(player, TALKTYPE_PRIVATE_PN, "trade", false, &npcsSpectators);
} else {
    internalCreatureSay(player, TALKTYPE_PRIVATE_PN, "sail", false, &npcsSpectators);
}

// AFTER:
const auto speechBubble = npc->getSpeechBubble();
if (speechBubble == SPEECHBUBBLE_TRADE) {
    internalCreatureSay(player, TALKTYPE_PRIVATE_PN, "trade", false, &npcsSpectators);
} else if (speechBubble == SPEECHBUBBLE_SAILOR) {
    internalCreatureSay(player, TALKTYPE_PRIVATE_PN, "sail", false, &npcsSpectators);
}
```

**`src/server/network/protocol/protocolgame_send.cpp`** (line ~5686, AddPlayerStats):
```cpp
// BEFORE:
msg.addByte(std::min<uint8_t>(player->getLevelPercent(), 100));

// AFTER:
if (!oldProtocol) {
    if (clientVersion >= 1513) {
        const auto levelPercent = std::min<uint16_t>(static_cast<uint16_t>(player->getLevelPercent()) * 100, 10000);
        msg.add<uint16_t>(levelPercent);
    } else {
        msg.addByte(std::min<uint8_t>(player->getLevelPercent(), 100));
    }
} else {
    msg.addByte(std::min<uint8_t>(player->getLevelPercent(), 100));
}
```

**`src/core.hpp`** — bump CLIENT_VERSION: `1511` → `1513`

**Lua files (wholesale):**
- `data/npclib/npc_system/modules.lua`
- `data/npclib/npc_system/custom_modules.lua`
- `data/npclib/npc_system/npc_handler.lua`
- `data/npclib/load.lua`
- `data/modules/scripts/npc/npc_dialog.lua`
- `data/scripts/lib/register_npc_type.lua`
- All ~1000 `data-otservbr-global/npc/*.lua` files — checkout from origin/main

**Acceptance criteria:**
- [ ] `SPEECHBUBBLE_SAILOR` and `SPEECHBUBBLE_BANKER` declared in `creatures_definitions.hpp`
- [ ] Both registered in `lua_enums.cpp`
- [ ] `playerNpcGreet` uses `else if` for SAILOR (no longer greets "sail" for all non-traders)
- [ ] `AddPlayerStats` sends uint16 (×100) for clientVersion ≥ 1513
- [ ] `CLIENT_VERSION = 1513` in `core.hpp`
- [ ] Build passes; 655+ tests green

**Dependencies:** Task 16.1
**Scope:** M

---

### Task 16.5 — Apply release metadata (68181dcdc, 60f9c62af)

**`src/core.hpp`** — if not already done in 16.4:
- `SERVER_RELEASE_VERSION = "3.6.1"`
- `CLIENT_VERSION = 1513`

**Docker/CI files (checkout from origin/main):**
- `docker/.env.dist`
- `docker/DOCKER.md`
- `docker/docker-compose.yml`
- `docker/quickstart/myaac/bootstrap.php`
- `config.lua.dist` — check for version references

**Acceptance criteria:**
- [ ] `core.hpp` matches `origin/main` (SERVER_RELEASE_VERSION="3.6.1", CLIENT_VERSION=1513)
- [ ] No conflict markers in docker/config files

**Dependencies:** Task 16.1
**Scope:** XS

---

## Phase 16: Multiprotocol (82b4778a5)

### Task 16.6 — Accept multiprotocol new files wholesale

Copy directly from `origin/main` (no conflicts — files don't exist yet):

```
src/server/network/protocol/protocol_profile.hpp
src/server/network/protocol/protocol_profile.cpp
src/server/network/protocol/protocol_session_hint.hpp
src/server/network/protocol/protocol_session_hint.cpp
src/server/network/protocol/transport_codec.hpp
src/server/network/protocol/transport_codec.cpp
src/server/network/protocol/protocol_port_utils.hpp
docs/systems/multiprotocol.md
tests/unit/server/network/protocol/multiprotocol_test.cpp
```

Add to `CMakeLists.txt` files (find where `protocolgame.cpp` is listed and add new sources alongside):
```
src/server/CMakeLists.txt
tests/unit/server/CMakeLists.txt
```

**Acceptance criteria:**
- [ ] All 9 files present in tree
- [ ] `cmake --build` picks them up (CMakeLists updated)
- [ ] `ProtocolProfileRegistry`, `TransportCodec`, `ProtocolSessionHintStore` symbols resolve

**Dependencies:** Task 16.1
**Scope:** M

---

### Task 16.7 — Apply multiprotocol changes to existing files

Apply hunks from `82b4778a5` to files that already exist on our branch.

**`src/server/network/connection/connection.hpp`** — add `InitialConnectionBehavior` and updated `Connection` members (checkout from origin/main or apply diff)

**`src/server/network/connection/connection.cpp`** — apply multiprotocol connection startup changes

**`src/server/network/protocol/protocol.hpp`** — add new method declarations

**`src/server/network/protocol/protocol.cpp`** — apply protocol changes

**`src/server/network/protocol/protocollogin.hpp`** — add declarations

**`src/server/network/protocol/protocollogin.cpp`** — apply login-split changes (account login vs game login layouts)

**`src/server/network/message/networkmessage.hpp`** — 9 lines changed

**`src/server/network/message/outputmessage.hpp`** — 12 lines added

**`src/pch.hpp`** — 3 includes added

**`src/config/config_enums.hpp`** — 2 new config enum values (`OLD_PROTOCOL` etc.)

**`src/config/configmanager.cpp`** — 2 new config entries

**`src/canary_server.cpp`** — add `getClientProtocolPortSummary`, `warnLegacy860WorldIpConfiguration`, startup logging changes

**`src/game/game.cpp`** (line ~739, `Game::start()`):
```cpp
// BEFORE:
manager->add<ProtocolGame>(static_cast<uint16_t>(g_configManager().getNumber(GAME_PORT)));

// AFTER:
const auto modernGamePort = protocol_port_utils::getModernGamePort();
manager->add<ProtocolGame>(modernGamePort);
if (g_configManager().getBoolean(OLD_PROTOCOL)) {
    const auto legacy1100GamePort = protocol_port_utils::getLegacy1100GamePort();
    if (legacy1100GamePort != modernGamePort) {
        manager->add<ProtocolGame>(legacy1100GamePort);
    }
    const auto legacy860GamePort = protocol_port_utils::getLegacy860GamePort();
    if (legacy860GamePort != modernGamePort && legacy860GamePort != legacy1100GamePort) {
        manager->add<ProtocolGame>(legacy860GamePort);
    }
}
```
Add include: `"server/network/protocol/protocol_port_utils.hpp"`

**`src/game/game.cpp`** — `#include "server/network/protocol/protocol_port_utils.hpp"` alongside existing protocollogin.hpp include

**`src/creatures/players/player.cpp`** (onCreatureAppear login log):
```cpp
// BEFORE:
auto version = client->oldProtocol ? getProtocolVersion() : CLIENT_VERSION;
g_logger().info("{} has logged in. (Protocol: {})", name, version);

// AFTER:
auto version = client && client->oldProtocol ? getProtocolVersion() : CLIENT_VERSION;
const auto* protocolProfile = client ? client->getProtocolProfile() : nullptr;
if (protocolProfile && (protocolProfile->assetSignatures.dat != 0 || ...)) {
    g_logger().info("{} has logged in. (Protocol: {}, Profile: {}, Assets: ...)", ...);
} else if (protocolProfile) {
    g_logger().info("{} has logged in. (Protocol: {}, Profile: {})", ...);
} else {
    g_logger().info("{} has logged in. (Protocol: {})", name, version);
}
```

**`src/server/network/protocol/protocolgame.cpp`** — parse/login/connect hunks:

Key hunks to apply (by function on our branch):
- `ProtocolGame::ProtocolGame` constructor (profile init from session hint / port)
- `ProtocolGame::onRecvFirstMessage` — multiprotocol profile resolution from port or hint
- `ProtocolGame::sendLoginChallenge` — profile-aware challenge layout
- `ProtocolGame::login` / `connect` — profile-aware login flow
- `ProtocolGame::writeToOutputBuffer` — transport codec integration
- Parse function guards (parseHotkeyEquip, parseOpenPrivateChannel etc.) — oldProtocol or profile checks
- Namespace helper: add `getPortPinnedProfile()` (lines 76-96 in new main)
- Add includes: `connection.hpp`, `protocol_port_utils.hpp`, `transport_codec.hpp`

**`src/server/network/protocol/protocolgame_send.cpp`** — send/Add hunks (from main's `protocolgame.cpp` which still had these):

Key send hunks:
- `AddItem` — profile/version guards for item encoding
- `sendAddCreature` / `AddCreature` — profile guards
- `sendCreatureSay`, `sendToChannel`, `sendPrivateMessage` — oldProtocol or profile-aware
- `sendMarketDetail` — minor changes
- `sendVIP`, `sendVIPGroups`, `sendUpdatedVIPStatus` — profile guards
- `sendTextMessage`, `sendChannel`, `sendIcons`, etc. — profile/version guards
- `AddCreature`, `AddPlayerStats` — check if already handled in Task 16.4
- `sendPremiumTrigger` — 49 lines added for profile-aware logic
- `sendMagicEffect`, `sendDistanceShoot` — profile checks

**Approach:** For each hunk in the 87-hunk diff, identify whether the function exists in `protocolgame.cpp` or `protocolgame_send.cpp` on our branch using `grep -n "functionName"`. Apply to the correct file.

**Acceptance criteria:**
- [ ] `ProtocolGame` constructor resolves profile from session hint or port
- [ ] `Game::start()` registers legacy ports when `OLD_PROTOCOL` is enabled
- [ ] `player.cpp::onCreatureAppear` login log includes profile info
- [ ] `protocolgame.cpp` and `protocolgame_send.cpp` compile without undefined symbols from new profile types
- [ ] No function appears in both files with conflicting changes

**Files:**
- `src/server/network/connection/connection.cpp`
- `src/server/network/connection/connection.hpp`
- `src/server/network/protocol/protocol.cpp`
- `src/server/network/protocol/protocol.hpp`
- `src/server/network/protocol/protocolgame.cpp`
- `src/server/network/protocol/protocolgame_send.cpp`
- `src/server/network/protocol/protocolgame.hpp`
- `src/server/network/protocol/protocollogin.cpp`
- `src/server/network/protocol/protocollogin.hpp`
- `src/server/network/message/networkmessage.hpp`
- `src/server/network/message/outputmessage.hpp`
- `src/pch.hpp`
- `src/config/config_enums.hpp`
- `src/config/configmanager.cpp`
- `src/canary_server.cpp`
- `src/game/game.cpp`
- `src/creatures/players/player.cpp`

**Dependencies:** Tasks 16.4, 16.6
**Scope:** L

---

## Phase 16: Build + test + commit

### Task 16.8 — Build and run full test suite

```bash
cd build/linux-release-enabled-tests
ninja canary_ut 2>&1 | tail -20
./tests/unit/canary_ut --gtest_brief=1 2>&1 | tail -5
```

**Acceptance criteria:**
- [ ] Build with no errors (warnings OK)
- [ ] ≥655 tests pass (0 failures)
- [ ] `multiprotocol_test.cpp` tests pass (new suite from 16.6)

**Dependencies:** Tasks 16.2–16.7
**Scope:** XS

---

### Task 16.9 — Port forge integration tests (bd6217f37)

The upstream commit adds `tests/integration/game/forge_it.cpp` (128 lines) with tests for
fuse/transfer with identical items. Accept from `origin/main` and verify they compile.
Check that the integration test suite CMakeLists includes the file.

**Acceptance criteria:**
- [ ] `tests/integration/game/forge_it.cpp` present in tree
- [ ] CMakeLists includes it
- [ ] Tests compile (may not run without MySQL — that's expected, same as Phase 0)

**Files:**
- `tests/integration/game/forge_it.cpp`
- `tests/integration/game/CMakeLists.txt` (if not already including it)

**Dependencies:** Task 16.8
**Scope:** S

---

### Task 16.10 — Commit merge

Single merge commit:

```
merge(main): integrate upstream fixes into nicollassantos/solid

- chore: update vcpkg baseline to 2026.06.24 (5aeca4f6d)
- fix(workflows): auto-close stale PRs (b3d9188cb)
- fix(ci): validate git refs in lua api check (3b9b6f0c6)
- ci(release): retag via git push in workflow (bce1f6308)
- fix(forge): prevent same-item double-removal in fuse/transfer flows (bd6217f37)
  Ported to PlayerForgeComponent::getForgeItemFromId (exclude param)
- feat: 15.13 NPC module protocol + NPC Lua scripts (5ee38ce90)
  Ported AddPlayerStats uint16 level-percent to protocolgame_send.cpp
  Applied SPEECHBUBBLE_SAILOR/BANKER enum values and playerNpcGreet else-if
- chore: update release metadata to 3.6.1 (68181dcdc)
- chore: normalize YAML document headers (d415619c8)
- feat(protocol): add runtime multiprotocol client profiles (82b4778a5)
  New files added; hunks split between protocolgame.cpp and protocolgame_send.cpp
- fix(release): preserve main client metadata (60f9c62af)
```

Update `tasks/todo.md` Phase 16 entries to `[x]`.

**Dependencies:** Task 16.9
**Scope:** XS

---

## Checkpoint: Phase 16 Complete

- [ ] ≥655 tests pass (multiprotocol_test suite adds more)
- [ ] `SERVER_RELEASE_VERSION = "3.6.1"`, `CLIENT_VERSION = 1513`
- [ ] Merge commit created
- [ ] `tasks/todo.md` updated with Phase 16 ✓

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| 87 hunks in main's `protocolgame.cpp` — many may touch functions in `protocolgame_send.cpp` on our branch | High | grep each function name before applying; route to correct file |
| `sendLoginChallenge` / `onRecvFirstMessage` restructured for multiprotocol in ways incompatible with our thin adapter | High | Read both files carefully; preserve our command-dispatch approach while adding profile-selection |
| `config.lua.dist` may have new config keys (`OLD_PROTOCOL_PORT`, legacy port configs) that interact with `config_enums.hpp` | Med | Accept `config.lua.dist` from main; verify `config_enums.hpp` has matching entries |
| `clientVersion` field used in send hunks — may not be defined in our `protocolgame_send.cpp` scope | Med | Check if `clientVersion` is a member of `ProtocolGame`; trace its definition |
| Integration tests in `forge_it.cpp` may need MySQL (same as other integration tests) | Low | Follow existing pattern — expected to be skipped without DB |

---

# Phase 17: Merge main upstream (107 commits, 60f9c62af..666a60f6f)

## Findings before merging

- `d2c13d162` (Phase 16) committed only part of the ported tree; ~1050 files stayed in the
  working tree. They were committed first as `fix(merge): complete phase 16 upstream port`.
- All previous "merges" were single-parent ports, so `git merge-base HEAD origin/main` was still
  `1df549ec5` (v3.6.0). Classifying every file touched by `1df549ec5..60f9c62af` showed
  1076 ported verbatim, 29 never ported (docs, workflows, AGENTS.md, quests.lua,
  icons_functions.lua, vcxproj) and 18 adapted; `protocolgame.cpp` was missing ~220 lines of the
  multiprotocol port (8.60 tile description, special text messages, 11.00 imbuement window).

## Approach

1. Real two-parent `git merge origin/main` (base `1df549ec5`), so the next merge only sees new
   upstream commits.
2. Ported and never-ported files resolve to `origin/main` by construction (ours == mid or ours == base).
3. Adapted files: take `origin/main` and re-apply only the branch customisations
   (friend classes, forward declarations, DI ports).
4. Files whose functions moved (game.cpp → services, player.cpp → components,
   protocolgame.cpp → protocolgame_send.cpp / protocolgame_helpers.hpp): function-level 3-way
   merge. Each upstream function is matched by name+parameter types to its current location
   (best similarity to the base body) and merged there with `git merge-file`; new upstream
   functions are inserted after their upstream predecessor; signature changes are handled by hand.
5. Hunks that merged cleanly into services/components still need `game_.` / `m_player.`
   qualification — rely on the compiler plus a grep for unqualified calls in added lines.
6. Re-apply the architecture to new upstream code until the checkpoints hold again
   (game.cpp < 4k, player.cpp < 7k).

## Environment notes

- The new `cmake/SharedBuildCache.cmake` rejects a `VCPKG_INSTALLED_DIR` outside the build tree:
  configure with `-UVCPKG_INSTALLED_DIR -U_VCPKG_INSTALLED_DIR` once.
- The vcpkg baseline moved to `9eec002df`; the local `VCPKG_ROOT` must be checked out at that
  commit and re-bootstrapped (`./bootstrap-vcpkg.sh -disableMetrics`), matching
  `.github/scripts/bootstrap_vcpkg.py`.
