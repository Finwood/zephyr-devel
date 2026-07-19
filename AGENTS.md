# AGENTS.md

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/) for all git commits in this
repo. Do **not** vendor the skill here; install it globally (user-level) if it is missing:

```bash
npx skills add https://smithery.ai/skills/dgalarza/conventional-commits -g -y
```

When creating or amending commits, follow the **conventional-commits** skill
(`~/.agents/skills/conventional-commits/SKILL.md`). Invoke it explicitly
(`/conventional-commits`) if it is not already in context.

## Cursor Cloud specific instructions

This repo is a **Zephyr T2 workspace** (west manifest repo + the `zephyr-devel` module). The
"application" is Zephyr firmware that is built and run on the **`native_sim`** simulator via
`west`. There is no web server or database. Standard setup steps live in `README.md`; only the
non-obvious, durable notes are captured here.

### What the update script already handles
On each session start the update script runs `uv sync` (Python tooling incl. `west`) and
`uv run west update --narrow --fetch-opt=--depth=1` (fetches Finwoos/zephyr main into `deps/`).
You do **not** need to re-run these unless `pyproject.toml` or `west.yml` changed.

### Already provisioned in the VM image (do not reinstall)
- `uv` is installed and symlinked into `/usr/local/bin`, so it is on `PATH` for any shell.
- Zephyr build system deps via apt: `ninja-build`, `device-tree-compiler`, `gperf`, `ccache`,
  `gcc-multilib`/`g++-multilib` (the 32-bit libs are required because `native_sim` defaults to a
  32-bit build).
- **Zephyr SDK 1.0.1** at `/opt/zephyr-sdk-1.0.1` (`/opt` is chowned to `ubuntu`).

### Required environment variables (gotcha)
`direnv` is **not** installed, so `.envrc` does **not** auto-load. The following are exported in
`~/.bashrc`, but if your shell does not source it, export them manually before building:

```bash
export ZEPHYR_BASE=/workspace/deps/zephyr
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
```

### Build / run / test / lint
All `west` commands must be prefixed with `uv run` (west lives in the uv venv).

```bash
# Build + run any app on the simulator (the produced binary is zephyr/zephyr.exe):
uv run west build -b native_sim -d /tmp/b <app_path>
/tmp/b/zephyr/zephyr.exe        # Ctrl-C / it self-stops; native_sim is interactive

# Tests use twister (the harness zcyphal's tests/ target):
uv run west twister -T <tests_dir> -p native_sim

# Lint (Python tooling): ruff and yamllint are available in the venv:
uv run ruff check .
uv run yamllint <files>
```
