# ADC Runtime Vref API (Part 1) — Finwood/zephyr patches

This directory carries the Part 1 implementation for
[Finwood/zephyr](https://github.com/Finwood/zephyr) because the cloud agent
token can push to `Finwood/zephyr-devel` but **not** to `Finwood/zephyr`
(`Permission denied to cursor[bot]`).

## Branch

- Intended Zephyr branch name: `adc-vref-runtime-api`
- Base revision (west `main` at implementation time): `e201b84b04e4fab1844658e71da0b7e340f1cc82`

## Apply onto Finwood/zephyr

```bash
cd deps/zephyr   # or a fresh Finwood/zephyr clone
git fetch Finwood main
git switch -c adc-vref-runtime-api e201b84b04e4fab1844658e71da0b7e340f1cc82
# If the base SHA is not in a shallow clone:
#   git fetch --unshallow   # or deepen as needed
git am /path/to/zephyr-devel/docs/superpowers/patches/adc-vref-runtime-api/000*.patch
git push -u Finwood adc-vref-runtime-api
```

Alternatively, if you have the live worktree from this agent session:

```bash
cd /path/to/deps/zephyr
git push -u Finwood adc-vref-runtime-api
```

## Commits (4)

1. `drivers: adc: add optional vref_get/set and ref_internal_set`
2. `drivers: adc_emul: implement vref_get/set for runtime ref API`
3. `docs: adc: document runtime internal reference get/set`
4. `style: adc: clang-format and keep-sorted release notes fix`

All AI commits include `Assisted-by:` and `Link:` to RFC
https://github.com/zephyrproject-rtos/zephyr/issues/113971. They intentionally
omit `Signed-off-by` — add DCO signoff before opening the upstream PR.

## Verification already run

- `west twister -T tests/drivers/adc/adc_emul -p native_sim` — PASS (15/15)
- `west build -b nucleo_l073rz samples/drivers/adc/adc_dt` — PASS
- `./scripts/ci/check_compliance.py -c e201b84b0..HEAD` — KeepSorted / ClangFormat / Checkpatch PASS;
  Gitlint UC2 fails without Signed-off-by (expected until human DCO)

## Plan

`docs/superpowers/plans/2026-08-01-adc-vref-runtime-api.md`
