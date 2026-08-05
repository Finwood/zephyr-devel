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

Cloud Environment setup is committed at [`.cursor/environment.json`](.cursor/environment.json)
(runs [`.cursor/cloud-install.sh`](.cursor/cloud-install.sh)). Details:
[`docs/superpowers/cloud-environment.md`](docs/superpowers/cloud-environment.md).

The **repo group** (sibling checkouts of `zephyr` + `zephyr-devel`) is still selected in the
dashboard environment; that part is not expressible in `environment.json`.

### Two-repo layout

Cloud Agents use a **multi-repo** environment with sibling checkouts, typically:

```text
<parent>/zephyr              # Finwood/zephyr (editable)
<parent>/zephyr-devel        # this repo (west manifest)
<parent>/zephyr-devel/deps/zephyr -> symlink to ../zephyr (or absolute sibling path)
```

Do **not** re-clone Zephyr into `deps/zephyr` when the sibling exists. The install script wires
the symlink and seeds west's `refs/heads/manifest-rev` so imports (`cmsis_6`, `hal_stm32`) work.
After `west update`, restore the sibling branch if west left it detached.

`deps/` is gitignored; the symlink is created by install, not committed.

### What the update / install script already handles

On environment build (and when re-run), `.cursor/cloud-install.sh`:

- ensures apt toolchain packages + `uv`
- wires `deps/zephyr` → sibling `zephyr`
- runs `uv sync` and `uv run west update --narrow --fetch-opt=--depth=1`
- installs Zephyr SDK under `/opt` when missing (`x86_64-zephyr-elf`, `arm-zephyr-eabi`)
- exports `ZEPHYR_*` in `~/.bashrc`

You do **not** need to re-run these unless `pyproject.toml` / `west.yml` changed or the SDK is
absent.

### Already provisioned after a successful Cloud Build

- `uv` on `PATH` (`~/.local/bin` and usually `/usr/local/bin`)
- apt: `ninja-build`, `device-tree-compiler`, `gperf`, `ccache`, `gcc-multilib` / `g++-multilib`
  (32-bit libs required because `native_sim` defaults to a 32-bit build)
- Zephyr SDK under `/opt/zephyr-sdk-*` (`ZEPHYR_SDK_INSTALL_DIR=/opt/`)

### Required environment variables (gotcha)

`direnv` is **not** installed, so `.envrc` does **not** auto-load. Install writes these to
`~/.bashrc`; if your shell does not source it, export them manually before building:

```bash
# Prefer the wired west path (symlink to sibling zephyr):
export ZEPHYR_BASE="<zephyr-devel>/deps/zephyr"
export ZEPHYR_SDK_INSTALL_DIR=/opt/
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
```

Common parents: `/agent/repos/zephyr-devel` or `/workspace/zephyr-devel`.

### Build / run / test / lint

All `west` commands must be prefixed with `uv run` (west lives in the uv venv). Run them from
`zephyr-devel` (the west workspace root).

```bash
# Build + run any app on the simulator (the produced binary is zephyr/zephyr.exe):
uv run west build -b native_sim -d /tmp/b <app_path>
/tmp/b/zephyr/zephyr.exe        # Ctrl-C / it self-stops; native_sim is interactive

# Tests use twister:
uv run west twister -T <tests_dir> -p native_sim

# Lint (Python tooling): ruff and yamllint are available in the venv:
uv run ruff check .
uv run yamllint <files>
```

If the Zephyr SDK is missing and you only need host `native_sim`, you can temporarily use:

```bash
export ZEPHYR_TOOLCHAIN_VARIANT=host
```
