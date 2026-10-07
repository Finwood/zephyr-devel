# firmware-sbus-bridge migration design

Date: 2026-10-07

## Goal

Move the UART→S.BUS converter out of `zephyr-devel` into its own product app repo
`firmware-sbus-bridge`, preserving filtered git history, matching the thin
`firmware-gc` scaffolding (without release/`app.yaml`), and keeping board, tests,
and docs app-owned.

## Decisions

| Topic | Choice |
|-------|--------|
| Layout | App-owned: board, tests, `docs/superpowers/` live in the app repo |
| History | `uvx git-filter-repo` path extract from zephyr-devel, then reshape commit(s) |
| zephyr-devel after | Delete migrated trees in a follow-up PR (no stub READMEs) |
| Sample rename | `uart_sbus` → `sbus_bridge` (project + binary name) |
| Binary name | `CONFIG_KERNEL_BIN_NAME="sbus_bridge"` |
| filter-repo install | Run via `uvx git-filter-repo` only (no pip install) |
| Release / `app.yaml` | Out of scope (follow-up) |
| Board revision | Version as `1.0` → build target `sbus_bridge@1.0` |
| Historical docs | Keep existing superpowers specs/plans filenames and in-doc names as-is |

## Source inventory (zephyr-devel)

- `samples/uart_sbus/` — application (reshape to product app named `sbus_bridge`)
- `boards/starcopter/sbus_bridge/` — product board
- `tests/sbus_pipe/` — native_sim unit tests (share `sbus_pipe.c`)
- `tests/sbus_bridge_hil/` — Nucleo HIL tester
- `docs/superpowers/specs/` — SBUS / uart-sbus design docs listed in the plan
- `docs/superpowers/plans/` — matching implementation plans
- `.superpowers/sdd/progress-sbus-bridge-hil.md` — HIL SDD progress (migrate)

## Target layout

```text
applications/firmware-sbus-bridge/
├── west.yml
├── CMakeLists.txt
├── Kconfig
├── prj.conf                 # includes CONFIG_KERNEL_BIN_NAME="sbus_bridge"
├── VERSION
├── Makefile                 # BOARD ?= sbus_bridge@1.0; no release target
├── PIPELINE.md
├── README.md
├── CHANGELOG.md
├── sources/                 # from samples/uart_sbus/src/
│   ├── main.c
│   ├── sbus_pipe.c
│   └── sbus_pipe.h
├── boards/
│   ├── starcopter/sbus_bridge/
│   │   ├── board.yml        # revision format major.minor.patch, default 1.0
│   │   ├── sbus_bridge_1_0.overlay  # empty marker overlay for rev 1.0
│   │   └── …
│   ├── nucleo_g431kb.conf
│   ├── nucleo_g431kb.overlay
│   └── sbus_bridge.conf     # app RTT fragment (all revisions)
├── tests/
│   ├── sbus_pipe/
│   └── sbus_bridge_hil/
├── docs/superpowers/{specs,plans}/   # migrate as-is; do not rename uart_* titles
├── .superpowers/sdd/progress-sbus-bridge-hil.md
├── zephyr/module.yml        # board_root: . ; tests: [tests]
├── .envrc
├── .gitignore
├── .pre-commit-config.yaml
├── .clang-format
├── .markdownlint.yaml
└── .github/workflows/
    ├── pr-compile.yaml      # board: sbus_bridge@1.0
    └── push-pre-commit.yaml
```

Explicitly **not** included: `app.yaml`, release Makefile target, release GitHub
workflows.

## Board revision `1.0`

During reshape, extend `boards/starcopter/sbus_bridge/board.yml`:

```yaml
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

Add an empty `sbus_bridge_1_0.overlay` (same pattern as `gc_A_2.overlay`).
Default / CI / Makefile board string: `sbus_bridge@1.0`. Building plain
`sbus_bridge` still resolves to `1.0` via `default`, but scripts and docs use
the explicit `@1.0` form.

## History pipeline

1. Work in a temporary clone of zephyr-devel.
2. `uvx git-filter-repo` keep only the paths above (exact list in the plan).
3. Replace the empty `firmware-sbus-bridge` git history with the filtered repo.
4. Reshape commit: move sample → app root / `sources/`, rename project/binary to
   `sbus_bridge`, version board as `sbus_bridge@1.0`, add GC-like scaffolding,
   fix test paths.
5. Create GitHub remote `starcopter/firmware-sbus-bridge`, default branch
   `develop`, push.
6. Verify build + `sbus_pipe` twister on `native_sim`.
7. zephyr-devel PR: delete the migrated paths.

## Naming

- CMake `project(sbus_bridge)`
- Binary: `CONFIG_KERNEL_BIN_NAME="sbus_bridge"`
- Board target: `sbus_bridge@1.0` (Makefile, CI, docs) — same stem as the app;
  board vs application are distinct Zephyr objects
- Product README / CHANGELOG / Kconfig mainmenu / GitHub description use
  `sbus_bridge`, not `uart_sbus` / `uart_bridge`
- Keep `CONFIG_UART_SBUS_SBUS2` Kconfig symbol (protocol option)
- **Exception:** migrated `docs/superpowers/specs/` and `docs/superpowers/plans/`
  keep original filenames and in-document names (`uart_sbus`, `uart-sbus`, etc.);
  do not rewrite those historical docs during migration

## Out of scope

- firmware-sdk board/test moves
- C→C++ or Cyphal
- Protocol/behavior changes
- `app.yaml` and release/OTA pipeline
- Rewriting historical superpowers specs/plans
