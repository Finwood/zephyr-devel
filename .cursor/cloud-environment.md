# Cursor Cloud Environment (zephyr + zephyr-devel)

Config is committed at [`environment.json`](./environment.json)
and runs [`cloud-install.sh`](./cloud-install.sh).

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
2. Locates the sibling Finwood/`zephyr` checkout (must look like a real Zephyr
   tree: `VERSION` / `SDK_VERSION` / `west.yml` — not `zephyr-devel`'s module
   entrypoint at `./zephyr/`)
3. Symlinks `zephyr-devel/deps/zephyr` → that sibling
4. Seeds west `refs/heads/manifest-rev`, runs `uv sync` + `west update`
5. Restores the sibling zephyr SHA/branch if west detached it
6. Writes `ZEPHYR_*` exports into `~/.bashrc`
7. Installs Zephyr SDK under `/opt` (`x86_64-zephyr-elf` + `arm-zephyr-eabi`) when missing

### Failure mode (fixed): wrong `deps/zephyr` target

West places the `zephyr` project at `deps/zephyr` because `import: path-prefix: deps`
prefixes the importing project path. If install mistook `zephyr-devel/zephyr/`
(module stub with `CMakeLists.txt`) for the sibling checkout, it symlinked
`deps/zephyr` there; `west update` then failed with:

```text
error: The following untracked working tree files would be overwritten by checkout:
	CMakeLists.txt
FATAL ERROR: command exited with status 1: checkout --detach refs/heads/manifest-rev
```

`find_repo` now prefers `/agent/repos/zephyr` (and other sibling locations) and
requires Zephyr tree markers before accepting a `zephyr` candidate.

## Builds

After this lands on `zephyr-devel` **main**, trigger a Build **without** per-repo
ref overrides so it is promotable. Draft/test builds may point `zephyr-devel` at
a feature branch via `refs`.

Repo-file managed environments cannot use `environment_json` overrides on
`trigger-environment-build`; test config changes by building the feature branch.

Validated draft builds (feature-branch refs, not promotable):

- `bld-20260805-d95578d7-af58-442d-90fb-e436a46e0c69` — initial cloud-install
  (apt, uv, sibling symlink, west update, SDK 1.0.1)
- `bld-20260805-0d2f2399-e535-41c0-ac35-e5da4843a9e5` — `find_repo` sibling fix
  (`cursor/fix-cloud-install-find-repo-98bf`); wires `/agent/repos/zephyr`,
  install exit 0
