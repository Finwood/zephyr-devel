# ADC Optional `ref_internal_get` (Part 1 reshape) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reshape the existing Part 1 branch from enum-keyed `vref_get`/`vref_set` + public `adc_ref_internal_set()` to an optional INTERNAL-only `ref_internal_get`, with emul/tests/docs updated and history folded into the three existing Part 1 commits (no force-push).

**Architecture:** Keep `adc_driver_api.ref_internal` as the static fallback. Add optional `ref_internal_get(dev)`. `adc_ref_internal()` prefers the getter when present. Drivers/emul mutate their own data; tests use `adc_emul_ref_voltage_set()`. No public setter, no syscall, no enum-keyed ops.

**Tech Stack:** Zephyr ADC subsystem (`include/zephyr/drivers/adc.h`), `adc_emul`, ztest on `native_sim`, west/`uv run`, Doxygen + RST docs.

**Spec:** `docs/superpowers/specs/2026-08-05-adc-ref-internal-get-design.md`  
**Supersedes plan:** `docs/superpowers/plans/2026-08-01-adc-vref-runtime-api.md` (enum-keyed + public set)  
**Stacked follow-up:** Update `docs/superpowers/plans/2026-08-02-adc-stm32-vrefint-calibrate.md` in Task 5 (API wiring only); STM32 code still later.

## Global Constraints

- **Work tree:** Upstream code/tests/docs/samples live in `deps/zephyr/` (west project). Workspace repo holds specs/plans only.
- **Zephyr branch:** `cursor/adc-vref-runtime-api-c2e3` (three Part 1 commits on top of `e201b84b04`).
- **History:** After code is correct, **fold** into those three commits via `git reset --soft` + re-commit (no `git rebase -i`). Do **not** `git push --force` / `--force-with-lease`. Leave remote reconciliation to the human.
- **Commits (Zephyr):** No `git commit -s` / `Signed-off-by`. Include verification note, `Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971`, and `Assisted-by: Cursor:grok-4.5` (or the actual model for the session).
- **Commits (workspace):** Conventional Commits for plan/doc updates in `zephyr-devel`.
- **v1 contract:** Optional `ref_internal_get` only; getter owns DT fallback when cache invalid (Part 2); Part 1 emul getter returns live `ref_int`.
- **Out of scope:** STM32 measure code; public setter; enum-keyed get/set; provider model; channel-id API.
- **Build/test:** `ZEPHYR_BASE=…/deps/zephyr`, `uv run west …`, host tests on `native_sim`.

---

## File map (Part 1 reshape)

| Path | Role |
|---|---|
| `deps/zephyr/include/zephyr/drivers/adc.h` | Replace `vref_*` + `adc_ref_internal_set` with `ref_internal_get` |
| `deps/zephyr/drivers/adc/adc_handlers.c` | Remove `adc_ref_internal_set` verify |
| `deps/zephyr/drivers/adc/adc_emul.c` | INTERNAL-only getter; keep `adc_emul_ref_voltage_set` as mutator |
| `deps/zephyr/include/zephyr/drivers/adc/adc_emul.h` | Doc: emul set updates what `adc_ref_internal` sees |
| `deps/zephyr/tests/drivers/adc/adc_emul/src/main.c` | Drive via `adc_emul_ref_voltage_set`; drop public-set tests |
| `deps/zephyr/doc/releases/release-notes-4.5.rst` | Behavior note, not new public symbol |
| `docs/superpowers/plans/2026-08-01-adc-vref-runtime-api.md` | Mark superseded (workspace) |
| `docs/superpowers/plans/2026-08-02-adc-stm32-vrefint-calibrate.md` | Align to `ref_internal_get`, drop setter (workspace) |

---

### Task 1: Rewrite emul tests for getter-only (red)

**Files:**
- Modify: `deps/zephyr/tests/drivers/adc/adc_emul/src/main.c`
- (API still has public set until Task 2 — tests that call `adc_emul_ref_voltage_set` + `adc_ref_internal` may already pass partially; public-set tests must be removed/replaced so Task 2 can delete the symbol.)

**Interfaces:**
- Consumes: existing `adc_emul_ref_voltage_set`, `adc_ref_internal`, `adc_raw_to_millivolts_dt`
- Produces: tests that do **not** reference `adc_ref_internal_set`

- [ ] **Step 1: Replace the three Part 1 public-set tests**

Delete `test_adc_ref_internal_set_and_get`, `test_adc_ref_internal_set_rejects_zero`, and `test_adc_raw_to_millivolts_dt_tracks_ref_internal_set`.

Keep `test_adc_emul_ref_voltage_set_rejects_unsupported` as-is.

Append:

```c
ZTEST_USER(adc_emul, test_adc_ref_internal_tracks_emul_ref_voltage_set)
{
	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);
	uint16_t before = adc_ref_internal(adc_dev);
	int ret;

	zassert_true(before > 0, "expected non-zero DT internal ref");

	ret = adc_emul_ref_voltage_set(adc_dev, ADC_REF_INTERNAL, 2500);
	zassert_ok(ret, "adc_emul_ref_voltage_set failed: %d", ret);
	zassert_equal(adc_ref_internal(adc_dev), 2500,
		      "adc_ref_internal did not observe emul set");

	zassert_ok(adc_emul_ref_voltage_set(adc_dev, ADC_REF_INTERNAL, before));
}

ZTEST_USER(adc_emul, test_adc_raw_to_millivolts_dt_tracks_emul_ref_internal)
{
	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);
	const uint16_t runtime_ref_mv = 2500;
	const int32_t raw_value = BIT(ADC_RESOLUTION) / 2;
	const int32_t expected_mv = runtime_ref_mv / 2;
	int32_t output = raw_value;
	int ret;

	/* clang-format off */
	static const struct adc_dt_spec adc_internal_spec = {
		.dev = DEVICE_DT_GET(ADC_DEVICE_NODE),
		.channel_id = ADC_1ST_CHANNEL_ID,
		.channel_cfg_dt_node_exists = true,
		.channel_cfg = {
			.gain = ADC_GAIN_1,
			.reference = ADC_REF_INTERNAL,
			.acquisition_time = ADC_ACQUISITION_TIME,
			.channel_id = ADC_1ST_CHANNEL_ID,
		},
		.resolution = ADC_RESOLUTION,
	};
	/* clang-format on */

	channel_setup(adc_dev, ADC_REF_INTERNAL, ADC_GAIN_1, ADC_1ST_CHANNEL_ID);

	ret = adc_emul_ref_voltage_set(adc_dev, ADC_REF_INTERNAL, runtime_ref_mv);
	zassert_ok(ret, "adc_emul_ref_voltage_set failed: %d", ret);

	ret = adc_raw_to_millivolts_dt(&adc_internal_spec, &output);
	zassert_ok(ret, "adc_raw_to_millivolts_dt() failed with code %d", ret);
	zassert_within(expected_mv, output, MV_OUTPUT_EPS,
		       "conversion did not track runtime internal ref");

	zassert_ok(adc_emul_ref_voltage_set(adc_dev, ADC_REF_INTERNAL,
					    ADC_REF_INTERNAL_MV));
}
```

- [ ] **Step 2: Confirm no remaining `adc_ref_internal_set` references in the test file**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
rg -n 'adc_ref_internal_set' tests/drivers/adc/adc_emul/src/main.c
```

Expected: no matches.

- [ ] **Step 3: Do not commit yet** (fold happens in Task 4 after all code is green)

---

### Task 2: Thin common API in `adc.h` + remove syscall handler

**Files:**
- Modify: `deps/zephyr/include/zephyr/drivers/adc.h`
- Modify: `deps/zephyr/drivers/adc/adc_handlers.c`

**Interfaces:**
- Consumes: existing `struct adc_driver_api`, `adc_ref_internal`
- Produces:
  - `typedef uint16_t (*adc_api_ref_internal_get)(const struct device *dev);`
  - `adc_api_ref_internal_get ref_internal_get;` (optional, after `ref_internal`)
  - Updated `adc_ref_internal()`
  - Removed: `adc_api_vref_*`, `vref_get`, `vref_set`, `adc_ref_internal_set`, handler

- [ ] **Step 1: Replace typedefs and `struct adc_driver_api` members**

Remove `adc_api_vref_get` / `adc_api_vref_set` and `vref_get` / `vref_set`.

Add (same style as neighboring ops typedefs, before `__subsystem struct adc_driver_api`):

```c
/**
 * @brief Type definition of ADC API function for getting the internal
 *        reference voltage in millivolts.
 * See adc_ref_internal() for related public helper.
 */
typedef uint16_t (*adc_api_ref_internal_get)(const struct device *dev);
```

In `struct adc_driver_api`, keep `ref_internal`, then:

```c
	/**
	 * @driver_ops_optional Get the current internal reference voltage
	 * in millivolts.
	 * When NULL, adc_ref_internal() returns @c ref_internal.
	 * When implemented, return the live cache if valid, otherwise the
	 * instance DT / @c ref_internal fallback.
	 */
	adc_api_ref_internal_get ref_internal_get;
```

- [ ] **Step 2: Update `adc_ref_internal()` and delete `adc_ref_internal_set`**

```c
/**
 * @brief Get the internal reference voltage.
 *
 * Returns the voltage corresponding to @ref ADC_REF_INTERNAL,
 * measured in millivolts.
 *
 * When the driver provides @c ref_internal_get, that callback is used.
 * Otherwise, the static @c ref_internal field from the driver API is
 * returned. Drivers that implement @c ref_internal_get may update the
 * value over time (for example after hardware calibration); this does
 * not change the function signature.
 *
 * @param dev Pointer to the device structure for the driver instance.
 *
 * @return A positive value is the reference voltage value. Returns zero if
 * reference voltage information is not available.
 */
static inline uint16_t adc_ref_internal(const struct device *dev)
{
	const struct adc_driver_api *api = DEVICE_API_GET(adc, dev);

	if (api->ref_internal_get != NULL) {
		return api->ref_internal_get(dev);
	}

	return api->ref_internal;
}
```

Delete the entire `adc_ref_internal_set` documentation block, `__syscall` declaration, and `z_impl_adc_ref_internal_set`.

- [ ] **Step 3: Remove userspace handler**

In `drivers/adc/adc_handlers.c`, delete `z_vrfy_adc_ref_internal_set` and the `#include <zephyr/syscalls/adc_ref_internal_set_mrsh.c>` line.

- [ ] **Step 4: Sanity-check symbols**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
rg -n 'vref_get|vref_set|adc_ref_internal_set|adc_api_vref' include/zephyr/drivers/adc.h drivers/adc/adc_handlers.c
```

Expected: no matches (except possibly unrelated text). Confirm `ref_internal_get` is present.

- [ ] **Step 5: Do not commit yet**

---

### Task 3: Emul installs `ref_internal_get`; docs

**Files:**
- Modify: `deps/zephyr/drivers/adc/adc_emul.c`
- Modify: `deps/zephyr/include/zephyr/drivers/adc/adc_emul.h`
- Modify: `deps/zephyr/doc/releases/release-notes-4.5.rst`

**Interfaces:**
- Consumes: Task 2 `ref_internal_get`
- Produces: emul getter over `data->ref_int`; `adc_emul_ref_voltage_set` remains the mutator (may keep a private set helper for multi-ref storage)

- [ ] **Step 1: Replace emul driver ops wiring**

Change `adc_emul_vref_get` to INTERNAL-only:

```c
static uint16_t adc_emul_ref_internal_get(const struct device *dev)
{
	struct adc_emul_data *data = dev->data;

	return adc_emul_get_ref_voltage(data, ADC_REF_INTERNAL);
}
```

Keep the existing set-switch logic, but as a **static** helper used only by `adc_emul_ref_voltage_set` (rename `adc_emul_vref_set` → e.g. `adc_emul_ref_voltage_set_unlocked` pattern, or keep a static `adc_emul_ref_voltage_set_internal` that is **not** installed on `DEVICE_API`). Do **not** put any set pointer on `DEVICE_API(adc, …)`.

In `ADC_EMUL_INIT` / `DEVICE_API`:

```c
	.ref_internal = DT_INST_PROP(_num, ref_internal_mv),
	.ref_internal_get = adc_emul_ref_internal_get,
```

Remove `.vref_get` / `.vref_set`.

- [ ] **Step 2: Update `adc_emul.h` comments**

Replace mentions of `vref_get` / `vref_set` with: `adc_emul_ref_voltage_set()` updates the same INTERNAL storage observed by `adc_ref_internal()` when the emul provides `ref_internal_get`.

- [ ] **Step 3: Fix release notes API Changes entry**

Replace the ADC bullet under API Changes with something like:

```rst
* ADC

  * Optional :c:member:`adc_driver_api.ref_internal_get` callback so
    :c:func:`adc_ref_internal` can return a driver-owned runtime millivolt
    scale (static :c:member:`adc_driver_api.ref_internal` remains the
    fallback when the callback is NULL)
```

- [ ] **Step 4: Run emul twister**

```bash
cd /home/lasse/projects/zephyr-devel
export ZEPHYR_BASE=/home/lasse/projects/zephyr-devel/deps/zephyr
uv run west twister -T deps/zephyr/tests/drivers/adc/adc_emul -p native_sim
```

Expected: all `drivers.adc.emul` tests PASS (including the two new tests from Task 1).

- [ ] **Step 5: Do not commit yet** (Task 4 folds history)

---

### Task 4: Fold into the three existing Part 1 commits (no force-push)

**Files:** none new — git only in `deps/zephyr`

**Interfaces:**
- Consumes: green working tree from Tasks 1–3
- Produces: same three commit slots, rewritten subjects/bodies for getter-only; remote unchanged

Part 1 base (parent of first Part 1 commit): `e201b84b04`  
Current tip (before fold): `bc8762adfe` (or whatever `HEAD` is after Tasks 1–3 if still on that tip with dirty tree).

- [ ] **Step 1: Confirm clean logical tree and status**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git status
git log --oneline e201b84b04..HEAD
rg -n 'adc_ref_internal_set|vref_get|vref_set' include/zephyr/drivers/adc.h drivers/adc/ tests/drivers/adc/adc_emul/ doc/releases/release-notes-4.5.rst || true
rg -n 'ref_internal_get' include/zephyr/drivers/adc.h drivers/adc/adc_emul.c
```

Expected: working tree has all reshape edits (staged or unstaged); no public-set / enum `vref_*` leftovers; `ref_internal_get` present.

- [ ] **Step 2: Soft-reset to Part 1 base (keeps all file contents)**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git reset --soft e201b84b04
git status
```

Expected: all Part 1 + reshape changes staged; `HEAD` at `e201b84b04`.

- [ ] **Step 3: Unstage, then recreate commit 1 (API + handlers)**

```bash
git reset HEAD
git add include/zephyr/drivers/adc.h drivers/adc/adc_handlers.c
git commit -m "$(cat <<'EOF'
drivers: adc: add optional ref_internal_get

Allow drivers to expose a runtime INTERNAL millivolt scale through
adc_ref_internal() without a public setter. Static ref_internal remains
the fallback when the optional getter is NULL.

Tested with: syscall regen on build; emul tests green after follow-up commits

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.5
EOF
)"
```

- [ ] **Step 4: Recreate commit 2 (emul + tests)**

```bash
git add drivers/adc/adc_emul.c include/zephyr/drivers/adc/adc_emul.h \
  tests/drivers/adc/adc_emul/src/main.c
git commit -m "$(cat <<'EOF'
drivers: adc_emul: implement ref_internal_get for runtime INTERNAL ref

Wire emulator INTERNAL reference storage through the optional getter so
host tests can exercise dynamic adc_ref_internal() via
adc_emul_ref_voltage_set().

Tested with: west twister -T tests/drivers/adc/adc_emul -p native_sim

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.5
EOF
)"
```

- [ ] **Step 5: Recreate commit 3 (docs)**

```bash
git add doc/releases/release-notes-4.5.rst
# include any other doc/sample files touched in the reshape
git status
git commit -m "$(cat <<'EOF'
docs: adc: document optional ref_internal_get behavior

Note that adc_ref_internal() may return a driver-owned runtime scale when
ref_internal_get is implemented.

Tested with: west twister -T tests/drivers/adc/adc_emul -p native_sim

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.5
EOF
)"
```

- [ ] **Step 6: Verify — no force-push**

```bash
git log --oneline e201b84b04..HEAD
git status
# Do NOT run: git push --force / --force-with-lease
```

Expected: exactly three commits; working tree clean; branch may diverge from `Finwood/cursor/adc-vref-runtime-api-c2e3` (ahead/behind after rewrite). Leave remote as-is.

- [ ] **Step 7: Re-run twister once on the folded tip**

```bash
cd /home/lasse/projects/zephyr-devel
export ZEPHYR_BASE=/home/lasse/projects/zephyr-devel/deps/zephyr
uv run west twister -T deps/zephyr/tests/drivers/adc/adc_emul -p native_sim
```

Expected: PASS.

---

### Task 5: Align workspace plans with the new spec

**Files:**
- Modify: `docs/superpowers/plans/2026-08-01-adc-vref-runtime-api.md`
- Modify: `docs/superpowers/plans/2026-08-02-adc-stm32-vrefint-calibrate.md`
- (This plan file is already the Part 1 reshape source of truth.)

**Interfaces:**
- Consumes: `docs/superpowers/specs/2026-08-05-adc-ref-internal-get-design.md`
- Produces: superseded banner on 2026-08-01; Part 2 plan uses `ref_internal_get` only

- [ ] **Step 1: Supersede the old Part 1 plan**

At the top of `docs/superpowers/plans/2026-08-01-adc-vref-runtime-api.md`, insert:

```markdown
> **Superseded** by `docs/superpowers/plans/2026-08-05-adc-ref-internal-get.md`
> and spec `docs/superpowers/specs/2026-08-05-adc-ref-internal-get-design.md`
> (getter-only; no public `adc_ref_internal_set` / enum-keyed `vref_*`).
> Keep this file for historical context only — do not execute.
```

- [ ] **Step 2: Patch Part 2 plan API references**

In `docs/superpowers/plans/2026-08-02-adc-stm32-vrefint-calibrate.md`:

- Goal/Architecture: depend on `ref_internal_get` (not `vref_get`/`vref_set`); remove `adc_ref_internal_set`.
- Spec pointer: also cite `2026-08-05-adc-ref-internal-get-design.md`; Part 1 branch still the stack base.
- Kconfig help: remove “Applications may override it with `adc_ref_internal_set()`.”
- Replace `adc_stm32_vref_get(dev, enum)` / `adc_stm32_vref_set` stubs with:

```c
static uint16_t adc_stm32_ref_internal_get(const struct device *dev)
{
	const struct adc_stm32_cfg *cfg = dev->config;

	k_mutex_lock(&stm32_adc_vref.lock, K_FOREVER);
	uint16_t out = stm32_adc_vref.valid ? stm32_adc_vref.mv : cfg->vref_mv;
	k_mutex_unlock(&stm32_adc_vref.lock);

	return out;
}
```

- Install `.ref_internal_get = adc_stm32_ref_internal_get` (no set op).
- Out of scope / v1 contract lines: drop public set; only getter.
- Depends-on line: Part 1 plan `2026-08-05-adc-ref-internal-get.md`.

Do **not** implement STM32 C in this task — docs only.

- [ ] **Step 3: Commit workspace docs**

```bash
cd /home/lasse/projects/zephyr-devel
git add docs/superpowers/plans/2026-08-01-adc-vref-runtime-api.md \
  docs/superpowers/plans/2026-08-02-adc-stm32-vrefint-calibrate.md \
  docs/superpowers/plans/2026-08-05-adc-ref-internal-get.md
git commit -m "$(cat <<'EOF'
docs(plans): reshape ADC vref Part 1 to ref_internal_get

Supersede the enum-keyed public-set plan; align Part 2 plan with the
getter-only RFC direction.

EOF
)"
```

---

### Task 6: Verification gate

- [ ] **Step 1: Emul twister** (if not just run in Task 4)

```bash
cd /home/lasse/projects/zephyr-devel
export ZEPHYR_BASE=/home/lasse/projects/zephyr-devel/deps/zephyr
uv run west twister -T deps/zephyr/tests/drivers/adc/adc_emul -p native_sim
```

Expected: PASS.

- [ ] **Step 2: Compliance on folded range**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
./scripts/ci/check_compliance.py -c e201b84b04..HEAD
```

Expected: no unexpected failures; fix real issues before handoff.

- [ ] **Step 3: Trailer / symbol audit**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git log --format=%B e201b84b04..HEAD | rg -i 'signed-off-by' || true
git log --format=%B e201b84b04..HEAD | rg -c '^Assisted-by:'
git log --format=%B e201b84b04..HEAD | rg -c '^Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971'
rg -n 'adc_ref_internal_set|adc_api_vref|\.vref_get|\.vref_set' \
  include/zephyr/drivers/adc.h drivers/adc/adc_emul.c drivers/adc/adc_handlers.c \
  tests/drivers/adc/adc_emul/src/main.c doc/releases/release-notes-4.5.rst || true
```

Expected: no `Signed-off-by`; each AI commit has `Assisted-by:` and RFC `Link:`; no set/enum-op leftovers.

- [ ] **Step 4: Handoff**

Summarize local three-commit tip vs remote (diverged; **do not force-push**). Part 2 stacks after human reconciles remote/PR.

---

## Spec coverage

| Spec item | Task |
|---|---|
| Optional `ref_internal_get` + updated `adc_ref_internal()` | Task 2 |
| No public setter / syscall | Task 2 |
| Emul getter + `adc_emul_ref_voltage_set` mutator | Task 3 |
| Tests via emul set | Task 1 + 3 |
| Docs / release notes behavior | Task 3 |
| Fold into existing commits; no force-push | Task 4 |
| Part 2 plan alignment (no STM32 code) | Task 5 |
| Verification | Task 6 |
