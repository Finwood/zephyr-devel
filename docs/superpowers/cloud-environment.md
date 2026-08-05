# Cursor Cloud Environment (zephyr + zephyr-devel)

Dashboard environment (multi-repo): both `github.com/finwood/zephyr` and
`github.com/finwood/zephyr-devel` must be selected. Config is **dashboard-managed**
(no committed `.cursor/environment.json`).

Environment page:
https://cursor.com/dashboard/cloud-agents/environments/e/c8ff0cfd-904d-11f1-a7d1-d6b4613131ce

## Install script

Canonical install lives at [`.cursor/cloud-install.sh`](../../.cursor/cloud-install.sh).

Paste this into the environment **Install** field:

```bash
set -euo pipefail
SCRIPT=""
for c in \
  "$PWD/.cursor/cloud-install.sh" \
  "$PWD/zephyr-devel/.cursor/cloud-install.sh" \
  "$PWD/repos/zephyr-devel/.cursor/cloud-install.sh" \
  "/agent/repos/zephyr-devel/.cursor/cloud-install.sh" \
  "/workspace/zephyr-devel/.cursor/cloud-install.sh"
do
  if [ -f "$c" ]; then SCRIPT="$c"; break; fi
done
if [ -z "$SCRIPT" ]; then
  echo "cloud-install.sh not found; is zephyr-devel checked out?" >&2
  exit 1
fi
bash "$SCRIPT"
```

Leave **Start** empty (no long-lived services). No ports/terminals required.

## What install does

1. Ensures apt build deps (`ninja`, `dtc`, `gperf`, `ccache`, `gcc-multilib`, …) and `uv`
2. Symlinks `zephyr-devel/deps/zephyr` → sibling `zephyr` checkout
3. Seeds west `refs/heads/manifest-rev`, runs `uv sync` + `west update`
4. Restores the sibling zephyr SHA/branch if west detached it
5. Writes `ZEPHYR_*` exports into `~/.bashrc`
6. Installs Zephyr SDK under `/opt` (`x86_64-zephyr-elf` + `arm-zephyr-eabi`) when missing

## Builds

After the install script is on `zephyr-devel` **main**, trigger a Build **without**
per-repo ref overrides so it is promotable. Draft/test builds may use a feature
branch ref for `zephyr-devel` only.
