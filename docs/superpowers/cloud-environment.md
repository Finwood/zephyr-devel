# Cursor Cloud Environment (zephyr + zephyr-devel)

Config is committed at [`.cursor/environment.json`](../../.cursor/environment.json)
and runs [`.cursor/cloud-install.sh`](../../.cursor/cloud-install.sh).

The **repo group** (which repos are cloned as siblings) still lives in the
dashboard multi-repo environment:

https://cursor.com/dashboard/cloud-agents/environments/e/c8ff0cfd-904d-11f1-a7d1-d6b4613131ce

Both `github.com/finwood/zephyr` and `github.com/finwood/zephyr-devel` must be
selected there. `repositoryDependencies` is not used for cloning siblings.

## Repo-file config

`.cursor/environment.json` in **zephyr-devel** (not Finwood/zephyr) wins over
personal/team dashboard install scripts when present. Keep Start empty — no
long-lived services or ports.

## What install does

1. Ensures apt build deps (`ninja`, `dtc`, `gperf`, `ccache`, `gcc-multilib`, …) and `uv`
2. Symlinks `zephyr-devel/deps/zephyr` → sibling `zephyr` checkout
3. Seeds west `refs/heads/manifest-rev`, runs `uv sync` + `west update`
4. Restores the sibling zephyr SHA/branch if west detached it
5. Writes `ZEPHYR_*` exports into `~/.bashrc`
6. Installs Zephyr SDK under `/opt` (`x86_64-zephyr-elf` + `arm-zephyr-eabi`) when missing

## Builds

After this lands on `zephyr-devel` **main**, trigger a Build **without** per-repo
ref overrides so it is promotable. Draft/test builds may point `zephyr-devel` at
a feature branch via `refs`.

Repo-file managed environments cannot use `environment_json` overrides on
`trigger-environment-build`; test config changes by building the feature branch.

Validated draft build (feature-branch refs, not promotable):
`bld-20260805-d95578d7-af58-442d-90fb-e436a46e0c69` — install exit 0 (apt, uv,
sibling symlink, west update, SDK 1.0.1).
