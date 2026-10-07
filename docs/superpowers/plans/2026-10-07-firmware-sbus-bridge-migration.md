# firmware-sbus-bridge Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate uart_sbus (renamed to sbus_bridge) from zephyr-devel into `firmware-sbus-bridge` with filtered history and GC-like scaffolding, without release/`app.yaml`.

**Architecture:** `uvx git-filter-repo` extracts SBUS paths from zephyr-devel into the empty app repo; a reshape commit moves the sample to product-app layout (`sources/`, app-owned `boards/` + `tests/` + docs) and adds west/Makefile/CI scaffolding pinned to firmware-sdk.

**Tech Stack:** Zephyr via firmware-sdk west workspace, C application, ztest/twister, GitHub `starcopter/firmware-sbus-bridge`.

## Global Constraints

- History: path-filtered extract only; reshape in follow-up commit(s) (approach 1).
- filter-repo: `uvx git-filter-repo` only — never `pip install git-filter-repo`.
- Rename sample/project/binary from `uart_sbus` to `sbus_bridge`; set `CONFIG_KERNEL_BIN_NAME="sbus_bridge"`. Do not rewrite historical superpowers specs/plans.
- Keep Kconfig symbol `CONFIG_UART_SBUS_SBUS2` (protocol flag).
- No `app.yaml`, no release Makefile target, no release GitHub workflows.
- App-owned board/tests/docs (not firmware-sdk).
- Board revision: `sbus_bridge@1.0` (`major.minor.patch`, default `"1.0"`).
- After verify: delete migrated trees from zephyr-devel (no stubs).
- Spec: `docs/superpowers/specs/2026-10-07-firmware-sbus-bridge-migration-design.md`.

---

### Task 1: Filter history into firmware-sbus-bridge

**Files:**
- Operate on: temp clone of `/home/lasse/projects/zephyr-devel`
- Replace history of: `/home/lasse/work/firmware/applications/firmware-sbus-bridge`

**Interfaces:**
- Consumes: zephyr-devel commits touching SBUS paths
- Produces: `firmware-sbus-bridge` git repo whose tree still uses old paths (`samples/uart_sbus/`, etc.)

- [ ] **Step 1: Confirm destination is empty**

Run:

```bash
cd /home/lasse/work/firmware/applications/firmware-sbus-bridge
git status
ls -la
```

Expected: unborn/empty `main`, only `.git/`.

- [ ] **Step 2: Create a temporary bare-usable clone**

```bash
rm -rf /tmp/zephyr-devel-sbus-filter
git clone --no-local /home/lasse/projects/zephyr-devel /tmp/zephyr-devel-sbus-filter
cd /tmp/zephyr-devel-sbus-filter
```

- [ ] **Step 3: Run path filter with uvx**

```bash
cd /tmp/zephyr-devel-sbus-filter
uvx git-filter-repo \
  --path samples/uart_sbus/ \
  --path boards/starcopter/sbus_bridge/ \
  --path tests/sbus_pipe/ \
  --path tests/sbus_bridge_hil/ \
  --path docs/superpowers/specs/2026-08-25-uart-sbus-converter-design.md \
  --path docs/superpowers/specs/2026-08-28-uart-sbus-frame-pipeline-design.md \
  --path docs/superpowers/specs/2026-09-30-sbus-bridge-board-design.md \
  --path docs/superpowers/specs/2026-10-05-sbus-bridge-hil-design.md \
  --path docs/superpowers/specs/2026-10-06-sbus-bridge-hil-noise-recovery-design.md \
  --path docs/superpowers/specs/2026-10-06-sbus-bridge-hil-status-leds-design.md \
  --path docs/superpowers/specs/2026-10-06-uart-sbus-field-robustness-design.md \
  --path docs/superpowers/plans/2026-08-25-uart-sbus-converter.md \
  --path docs/superpowers/plans/2026-08-28-uart-sbus-frame-pipeline.md \
  --path docs/superpowers/plans/2026-10-05-sbus-bridge-hil.md \
  --path docs/superpowers/plans/2026-10-06-sbus-bridge-hil-status-leds.md \
  --path .superpowers/sdd/progress-sbus-bridge-hil.md \
  --path docs/superpowers/specs/2026-10-07-firmware-sbus-bridge-migration-design.md \
  --path docs/superpowers/plans/2026-10-07-firmware-sbus-bridge-migration.md
```

If the migration design/plan were committed only on a branch that is not in this clone yet, either commit them in zephyr-devel first or copy them in during Task 2. Prefer committing them in zephyr-devel before filtering so they ride along in history; if that is undesirable, add them only in the reshape commit.

Expected: filter completes; `ls` shows only the kept paths.

- [ ] **Step 4: Verify filtered log**

```bash
cd /tmp/zephyr-devel-sbus-filter
git log --oneline | head -30
git ls-files | head -80
```

Expected: SBUS-related commits only; no unrelated zephyr-devel trees.

- [ ] **Step 5: Replace firmware-sbus-bridge history**

```bash
DEST=/home/lasse/work/firmware/applications/firmware-sbus-bridge
rm -rf "$DEST/.git"
cp -a /tmp/zephyr-devel-sbus-filter/.git "$DEST/.git"
cd "$DEST"
git checkout -B develop
git status
git log --oneline | head -10
```

Expected: working tree matches filtered paths; branch `develop`.

- [ ] **Step 6: Commit checkpoint note (optional local tag)**

```bash
cd /home/lasse/work/firmware/applications/firmware-sbus-bridge
git tag pre-reshape
```

---

### Task 2: Reshape tree to product-app layout + sbus_bridge rename

**Files:**
- Move: `samples/uart_sbus/src/*` → `sources/`
- Move: `samples/uart_sbus/boards/*` → `boards/` (overlays next to `boards/starcopter/`)
- Move: `samples/uart_sbus/{PIPELINE.md,img}` → repo root / `img/`
- Remove: `samples/uart_sbus/` after absorbing root build files
- Create/replace: root `CMakeLists.txt`, `Kconfig`, `prj.conf`, `VERSION`
- Modify: `tests/sbus_pipe/CMakeLists.txt` paths
- Create: `zephyr/module.yml`
- Modify: `boards/starcopter/sbus_bridge/board.yml` (add revision `1.0`)
- Create: `boards/starcopter/sbus_bridge/sbus_bridge_1_0.overlay`

**Interfaces:**
- Consumes: filtered tree from Task 1
- Produces: buildable app at repo root named `sbus_bridge`; board target `sbus_bridge@1.0`

- [ ] **Step 1: Move sources and board overlays**

```bash
cd /home/lasse/work/firmware/applications/firmware-sbus-bridge
mkdir -p sources boards img
git mv samples/uart_sbus/src/main.c sources/
git mv samples/uart_sbus/src/sbus_pipe.c sources/
git mv samples/uart_sbus/src/sbus_pipe.h sources/
git mv samples/uart_sbus/boards/nucleo_g431kb.conf boards/
git mv samples/uart_sbus/boards/nucleo_g431kb.overlay boards/
git mv samples/uart_sbus/boards/sbus_bridge.conf boards/
git mv samples/uart_sbus/PIPELINE.md PIPELINE.md
git mv samples/uart_sbus/img/slots.svg img/
git mv samples/uart_sbus/img/wiring.svg img/
```

- [ ] **Step 2: Write root CMakeLists.txt**

Create `CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sbus_bridge)

target_sources(app PRIVATE
  sources/main.c
  sources/sbus_pipe.c
)
target_include_directories(app PRIVATE sources)
```

- [ ] **Step 3: Write root Kconfig**

Create `Kconfig`:

```kconfig
# SPDX-License-Identifier: Apache-2.0

mainmenu "UART to S.BUS bridge (sbus_bridge)"

source "Kconfig.zephyr"

config UART_SBUS_SBUS2
	bool "Accept S.BUS2 slot footers"
	default n
	help
	  Valid footer iff (byte & mask) == 0x00.
	  n: mask 0xFF (classic S.BUS, footer 0x00 only).
	  y: mask 0xCB (ignore bits 2,4,5: 0x00/0x04/0x14/0x24/0x34).
```

- [ ] **Step 4: Write root prj.conf with KERNEL_BIN_NAME**

Create `prj.conf` from the former sample, plus binary name:

```conf
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_CONSOLE=y
CONFIG_GPIO=y
CONFIG_WATCHDOG=y
CONFIG_RESET_ON_FATAL_ERROR=y
CONFIG_STACK_SENTINEL=y
CONFIG_FAULT_DUMP=1
CONFIG_ISR_STACK_SIZE=1024
CONFIG_MAIN_STACK_SIZE=1024
CONFIG_KERNEL_BIN_NAME="sbus_bridge"
```

- [ ] **Step 5: Write VERSION**

Create `VERSION`:

```
VERSION_MAJOR = 0
VERSION_MINOR = 1
PATCHLEVEL = 0
VERSION_TWEAK = 0
```

- [ ] **Step 6: Write zephyr/module.yml**

Create `zephyr/module.yml`:

```yaml
name: firmware-sbus-bridge
build:
  settings:
    board_root: .
tests:
  - tests
```

Do **not** register `cmake: .` / app `kconfig` as a Zephyr module library — this repo is the west application, not an SDK code module. `board_root` + `tests` are enough for board discovery and twister.

- [ ] **Step 7: Version board as 1.0**

Replace `boards/starcopter/sbus_bridge/board.yml` with:

```yaml
# SPDX-License-Identifier: MIT

board:
  name: sbus_bridge
  full_name: starcopter UART S.BUS Bridge
  vendor: starcopter
  revision:
    format: major.minor.patch
    default: "1.0"
    revisions:
      - name: "1.0"
  socs:
    - name: stm32c031xx
```

Create empty `boards/starcopter/sbus_bridge/sbus_bridge_1_0.overlay`:

```c
/*
 * SPDX-License-Identifier: MIT
 *
 * sbus_bridge hardware revision 1.0 (no DTS deltas vs base sbus_bridge.dts).
 */
```

Leave `sbus_bridge.yaml` identifier as `sbus_bridge` (twister discovers
`sbus_bridge@1.0` via board.yml default). Keep app fragment
`boards/sbus_bridge.conf` (applies across revisions).

- [ ] **Step 8: Fix sbus_pipe unit test paths**

Replace `tests/sbus_pipe/CMakeLists.txt` with:

```cmake
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sbus_pipe)

target_sources(app PRIVATE
  src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../sources/sbus_pipe.c
)
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../sources
)
```

- [ ] **Step 9: Remove obsolete sample tree**

```bash
git rm -rf samples/uart_sbus
# if samples/ empty:
rmdir samples 2>/dev/null || true
```

Absorb useful content from old `README.rst` into the new `README.md` in Task 3 (do not keep `.rst` as primary).

- [ ] **Step 10: Commit reshape**

```bash
git add -A
git commit -m "$(cat <<'EOF'
chore: reshape sbus_bridge into product-app layout

Move filtered zephyr-devel sample paths to sources/, app-owned boards,
and root build files; rename project/binary to sbus_bridge; version
board as sbus_bridge@1.0.
EOF
)"
```

---

### Task 3: Add firmware-gc-style scaffolding (no release)

**Files:**
- Create: `west.yml`, `Makefile`, `.envrc`, `.gitignore`, `.pre-commit-config.yaml`, `.clang-format`, `.markdownlint.yaml`, `README.md`, `CHANGELOG.md`
- Create: `.github/workflows/pr-compile.yaml`, `.github/workflows/push-pre-commit.yaml`
- Copy clang-format / markdownlint / pre-commit from `firmware-gc` (same revs)

**Interfaces:**
- Consumes: reshaped app from Task 2
- Produces: west-manifest app usable beside firmware-sdk

- [ ] **Step 1: Write west.yml**

Create `west.yml`:

```yaml
manifest:
  self:
    path: applications/firmware-sbus-bridge
  remotes:
    - name: starcopter
      url-base: https://github.com/starcopter
  projects:
    - name: firmware-sdk
      remote: starcopter
      revision: develop
      path: firmware-sdk
      import: true
```

- [ ] **Step 2: Write Makefile (no release)**

Base on `firmware-gc/Makefile` with these differences:

- `BOARD ?= sbus_bridge@1.0`
- Delete the `release` phony target and body
- Keep `check-sdk-pin`, `compile`, `flash`, `clean`, `help`, reports

Minimal compile section must invoke:

```makefile
BOARD ?= sbus_bridge@1.0
APP_NAME := $(notdir $(CURDIR))
PROJECT_ROOT := $(shell realpath ../..)
SDK_ROOT := $(PROJECT_ROOT)/firmware-sdk
UV := uv run --project $(SDK_ROOT)

compile: check-sdk-pin
	$(UV) west build -b ${BOARD} --build-dir ${BUILD_DIR} ${BUILD_FLAGS} .
```

`BUILD_DIR_FOLDER` already sanitizes `@` → `_` (from firmware-gc), so the
build dir is `build_sbus_bridge_1.0` or similar. Copy the full non-release
portions from firmware-gc; keep flash/compile/clean/help at minimum.

- [ ] **Step 3: Dotfiles**

- `.envrc`: same as firmware-gc (`source_env_if_exists ../../firmware-sdk/.envrc`)
- `.gitignore`: copy from firmware-gc (drop `sources/generated/` if unused, harmless to keep)
- `.pre-commit-config.yaml`, `.clang-format`, `.markdownlint.yaml`: copy from firmware-gc

- [ ] **Step 4: GitHub workflows (compile + pre-commit only)**

`.github/workflows/pr-compile.yaml`:

```yaml
name: Compile
on:
  pull_request:
  push:
    branches: [develop]
jobs:
  compile:
    uses: starcopter/firmware-sdk/.github/workflows/app-compile.yaml@develop
    secrets:
      ACTIONS_TOKEN: ${{ secrets.ACTIONS_TOKEN }}
    with:
      board: sbus_bridge@1.0
      app-path: applications/firmware-sbus-bridge
```

`.github/workflows/push-pre-commit.yaml`: copy from firmware-gc.

Do **not** add `dispatch-release-*.yaml`.

- [ ] **Step 5: README.md and CHANGELOG.md**

`README.md` structure (mirror firmware-gc brevity + sbus_bridge specifics):

```markdown
# firmware-sbus-bridge

Firmware for the Starcopter S.BUS bridge (`sbus_bridge`): UART 115200 8N1
cut-through to inverted S.BUS 100k 8E2.

Shared module: [starcopter/firmware-sdk](https://github.com/starcopter/firmware-sdk).

## Setup

Toolchain: [firmware-sdk README](https://github.com/starcopter/firmware-sdk#setup-before-cloning).

```bash
uvx west init -m git@github.com:starcopter/firmware-sbus-bridge.git firmware
cd firmware
uvx west update
make -C firmware-sdk prepare
cd applications/firmware-sbus-bridge
direnv allow .
```

## Build and flash

Default board `sbus_bridge@1.0`.

```bash
make compile
make flash
```

Unit tests:

```bash
uv run --project ../../firmware-sdk west twister -T tests/sbus_pipe -p native_sim
```

See `PIPELINE.md`, `boards/starcopter/sbus_bridge/README.md`, and
`tests/sbus_bridge_hil/README.md` for wiring and HIL.
```

`CHANGELOG.md`:

```markdown
# Changelog

## [Unreleased]

### Features

- Migrate sbus_bridge (formerly uart_sbus) from zephyr-devel into this repo.
```

Pull pinout/build details from old `samples/uart_sbus/README.rst` as needed into README or keep detailed content in board/HIL READMEs.

- [ ] **Step 6: Commit scaffolding**

```bash
git add -A
git commit -m "$(cat <<'EOF'
chore: add product-app scaffolding for sbus_bridge

West manifest, Makefile, CI compile/pre-commit, and README aligned with
firmware-gc; release and app.yaml deferred.
EOF
)"
```

---

### Task 4: Wire workspace, build, and test

**Files:**
- Possibly: `/home/lasse/work/firmware/.west/config` (local only; do not commit)

**Interfaces:**
- Consumes: Tasks 2–3 tree + existing firmware-sdk checkout
- Produces: verified `sbus_bridge` binary and passing `sbus_pipe` tests

- [ ] **Step 1: Point west at this app (local verify)**

Either temporarily set manifest path:

```bash
cd /home/lasse/work/firmware
# backup: cp .west/config /tmp/west-config.bak
cat > .west/config <<'EOF'
[manifest]
path = applications/firmware-sbus-bridge
file = west.yml
[zephyr]
base = zephyr
EOF
uv run --project firmware-sdk west update
```

Or build with existing SDK tree without re-init if `ZEPHYR_BASE` already valid and board_root is discovered via the app’s `zephyr/module.yml` on the manifest repo after `west update`.

If `west update` with this manifest fails because GitHub repo does not exist yet, keep using the already-populated `/home/lasse/work/firmware/{zephyr,firmware-sdk}` and build with:

```bash
cd /home/lasse/work/firmware/applications/firmware-sbus-bridge
export ZEPHYR_BASE=/home/lasse/work/firmware/zephyr
uv run --project ../../firmware-sdk west build -b sbus_bridge@1.0 -d build_sbus_bridge_1_0 .
```

Ensure the app directory is on Zephyr’s module path so `board_root` applies. If boards are not found, pass:

```bash
uv run --project ../../firmware-sdk west build -b sbus_bridge@1.0 -d build_sbus_bridge_1_0 -- \
  -DBOARD_ROOT=/home/lasse/work/firmware/applications/firmware-sbus-bridge .
```

Prefer fixing `zephyr/module.yml` discovery over leaving a permanent `-DBOARD_ROOT` hack. West includes the manifest repository as a Zephyr module when `zephyr/module.yml` exists.

- [ ] **Step 2: Compile for sbus_bridge@1.0**

```bash
cd /home/lasse/work/firmware/applications/firmware-sbus-bridge
make compile
```

Expected: success; artifact under `build_sbus_bridge_1.0/` (or Makefile-sanitized
equiv) named `sbus_bridge.elf` / `sbus_bridge.bin` (via `CONFIG_KERNEL_BIN_NAME`).
CMake should report board revision `1.0`.

Verify:

```bash
find build_sbus_bridge* \( -name 'sbus_bridge.elf' -o -name 'sbus_bridge.bin' \) | head
```

- [ ] **Step 3: Run unit tests**

```bash
cd /home/lasse/work/firmware
uv run --project firmware-sdk west twister \
  -T applications/firmware-sbus-bridge/tests/sbus_pipe \
  -p native_sim
```

Expected: all `sbus_pipe` tests PASS.

- [ ] **Step 4: Restore west config if changed**

```bash
cp /tmp/west-config.bak /home/lasse/work/firmware/.west/config
```

---

### Task 5: Publish GitHub repo and push

**Files:**
- Remote only

- [ ] **Step 1: Create GitHub repository**

```bash
gh repo create starcopter/firmware-sbus-bridge \
  --private \
  --description "Starcopter S.BUS bridge firmware (sbus_bridge)" \
  --source=/home/lasse/work/firmware/applications/firmware-sbus-bridge \
  --remote=origin \
  --push=false
```

Adjust visibility if the org default for product apps is public (match `firmware-gc`).

- [ ] **Step 2: Push develop**

```bash
cd /home/lasse/work/firmware/applications/firmware-sbus-bridge
git push -u origin develop
```

- [ ] **Step 3: Set default branch to develop**

```bash
gh repo edit starcopter/firmware-sbus-bridge --default-branch develop
```

---

### Task 6: Remove migrated trees from zephyr-devel

**Files:**
- Delete from `/home/lasse/projects/zephyr-devel`:
  - `samples/uart_sbus/`
  - `boards/starcopter/sbus_bridge/`
  - `tests/sbus_pipe/`
  - `tests/sbus_bridge_hil/`
  - listed SBUS specs/plans (same filter list)
  - `.superpowers/sdd/progress-sbus-bridge-hil.md`
- Keep: this migration design + plan under zephyr-devel docs until the PR lands (optional: add a one-line pointer in the migration plan that the code now lives in firmware-sbus-bridge)

**Interfaces:**
- Consumes: verified Task 4–5
- Produces: zephyr-devel PR with deletions only

- [ ] **Step 1: Branch and delete**

```bash
cd /home/lasse/projects/zephyr-devel
git checkout -b cursor/remove-uart-sbus-after-migration
git rm -rf samples/uart_sbus \
  boards/starcopter/sbus_bridge \
  tests/sbus_pipe \
  tests/sbus_bridge_hil \
  .superpowers/sdd/progress-sbus-bridge-hil.md
git rm -f \
  docs/superpowers/specs/2026-08-25-uart-sbus-converter-design.md \
  docs/superpowers/specs/2026-08-28-uart-sbus-frame-pipeline-design.md \
  docs/superpowers/specs/2026-09-30-sbus-bridge-board-design.md \
  docs/superpowers/specs/2026-10-05-sbus-bridge-hil-design.md \
  docs/superpowers/specs/2026-10-06-sbus-bridge-hil-noise-recovery-design.md \
  docs/superpowers/specs/2026-10-06-sbus-bridge-hil-status-leds-design.md \
  docs/superpowers/specs/2026-10-06-uart-sbus-field-robustness-design.md \
  docs/superpowers/plans/2026-08-25-uart-sbus-converter.md \
  docs/superpowers/plans/2026-08-28-uart-sbus-frame-pipeline.md \
  docs/superpowers/plans/2026-10-05-sbus-bridge-hil.md \
  docs/superpowers/plans/2026-10-06-sbus-bridge-hil-status-leds.md
```

Leave `docs/superpowers/specs/2026-10-07-firmware-sbus-bridge-migration-design.md` and
`docs/superpowers/plans/2026-10-07-firmware-sbus-bridge-migration.md` in zephyr-devel
as the record of the move (or move them only into the new repo — prefer **copy already
in new repo via filter/reshape**; delete from zephyr-devel only if duplicated).

- [ ] **Step 2: Commit and open PR**

```bash
git commit -m "$(cat <<'EOF'
chore(sbus): remove uart_sbus after firmware-sbus-bridge migration

Code, board, tests, and historical specs/plans now live in
starcopter/firmware-sbus-bridge as sbus_bridge.
EOF
)"
git push -u origin HEAD
gh pr create --title "chore(sbus): remove uart_sbus after migration" --body "$(cat <<'EOF'
## Summary
- Remove uart_sbus sample, sbus_bridge board, sbus tests, and related superpowers docs from zephyr-devel after migration to starcopter/firmware-sbus-bridge (sbus_bridge).

## Test plan
- [ ] Confirm firmware-sbus-bridge builds for sbus_bridge@1.0
- [ ] Confirm tests/sbus_pipe passes on native_sim in the new repo
- [ ] Grep zephyr-devel for leftover uart_sbus / sbus_bridge references
EOF
)"
```

---

## Self-review

1. **Spec coverage:** App-owned layout, filter-repo via uvx, sbus_bridge rename + KERNEL_BIN_NAME, board `sbus_bridge@1.0`, no app.yaml/release, zephyr-devel deletion — all have tasks. Historical superpowers docs unchanged.
2. **Placeholders:** None intentional; BOARD_ROOT fallback documented only as verify contingency.
3. **Naming:** `sbus_bridge` project/binary (`CONFIG_KERNEL_BIN_NAME`); board `sbus_bridge@1.0`; `CONFIG_UART_SBUS_SBUS2` retained; do not rewrite historical superpowers specs/plans (`uart_sbus` / `uart-sbus` filenames and prose stay).
