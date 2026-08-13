# ADC Runtime Vref API (Part 1) Implementation Plan

> **Superseded / completed.** Part 1 landed as upstream PR
> [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439), but **not**
> with this plan’s `vref_get`/`vref_set` + `adc_ref_internal_set()` shape.
> Review replaced that with enum-keyed `adc_ref_get()` and no setter.
> Do **not** execute this plan. Current Part 2 plan:
> `docs/superpowers/plans/2026-08-13-adc-stm32-vrefint-calibrate.md`.
> Spec: `docs/superpowers/specs/2026-08-13-adc-ref-get-stm32-vrefint-design.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional enum-keyed `vref_get` / `vref_set` to the Zephyr ADC driver API, with public INTERNAL shorthands `adc_ref_internal()` / `adc_ref_internal_set()`, plus emul + tests + docs — no vendor driver behavior change.

**Architecture:** Keep `adc_driver_api.ref_internal` as the static fallback. Add optional function pointers keyed by `enum adc_reference` (same shape as `adc_emul_ref_voltage_set`). Public v1 surface stays INTERNAL-only; non-internal DT helpers continue to use `spec->vref_mv`. Emulator implements the ops so host tests can exercise the contracts.

**Tech Stack:** Zephyr ADC subsystem (`include/zephyr/drivers/adc.h`), `adc_emul`, ztest on `native_sim`, west/`uv run`, Doxygen + RST docs.

**Spec:** `docs/superpowers/specs/2026-07-18-stm32-adc-vrefint-dynamic-ref-design.md` (API §§3–5, 7–9.1, 8.1).  
**Stacked follow-up:** `docs/superpowers/plans/2026-08-02-adc-stm32-vrefint-calibrate.md` (Part 2).

## Global Constraints

- **Work tree:** All upstream code/tests/docs/samples for this feature are edited in `deps/zephyr/` (west project, **not** a git submodule). Create and use a dedicated git branch there.
- **Branch (Part 1):** `adc-vref-runtime-api` from the current west-checked-out Zephyr revision (detached `HEAD` is normal after `west update`; create the branch explicitly).
- **Commits:** Do **not** use `git commit -s`, `--signoff`, or a `Signed-off-by` trailer. The human will rewrite history / add DCO for upstream later.
- **AI attribution:** Every commit in `deps/zephyr` that an AI agent helped author **must** include an `Assisted-by:` trailer per Zephyr’s [Usage disclosure and attribution](https://docs.zephyrproject.org/latest/contribute/guidelines.html#usage-disclosure-and-attribution):

  ```
  Assisted-by: [Agent Name]:[Model Version] [Tool1] [Tool2]
  ```

  - `[Agent Name]` — AI tool/framework (e.g. `Cursor`)
  - `[Model Version]` — specific model used for that commit (e.g. `grok-4.5`)
  - `[Tool1] [Tool2]` — optional specialized analysis tools only (e.g. `coccinelle`); do **not** list basic tools (git, gcc, make, editors, west, twister)
  - Place the trailer in the commit message body/footer (blank line after subject/body), **without** `Signed-off-by`
  - Example: `Assisted-by: Cursor:grok-4.5`
  - Use the **actual** agent and model for the session that produced the commit; if multiple agents/models contributed across commits, tag each commit accurately
- **Commit messages (Zephyr style):** Title `area: summary` (<72 chars), blank line, non-empty body explaining what/why. In the body/footer also include:
  - A short **verification** note (e.g. `Tested with: west twister -T tests/drivers/adc/adc_emul -p native_sim`)
  - `Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971` (RFC). Use `Fixes #113971` only if the PR is meant to close the RFC issue; prefer `Link:` until the feature lands.
  - `Assisted-by:` (see above). Do **not** add `Signed-off-by` in agent commits.
- **New files:** Add SPDX/REUSE headers at the top (native comment syntax), matching neighboring files — typically `SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors` and `SPDX-License-Identifier: Apache-2.0`. In the commit that introduces new original files, include `Origin: Original` in the commit message footer ([Identifying Contribution Origin](https://docs.zephyrproject.org/latest/contribute/guidelines.html#identifying-contribution-origin)).
- **Compliance:** Before handoff, run `./scripts/ci/check_compliance.py -c <commit-range>` from `deps/zephyr` (see [Running CI Tests Locally](https://docs.zephyrproject.org/latest/contribute/guidelines.html#running-ci-tests-locally)). Fix reported issues.
- **Upstream PR (human):** Agent does not open PRs unless asked. When the human prepares upstream PRs: rewrite/add `Signed-off-by` (DCO; legal name + real email matching Git author), keep `Assisted-by:`, reference the RFC in the PR body, watch CI. See [Contribution Guidelines](https://docs.zephyrproject.org/latest/contribute/guidelines.html).
- **Formal tests & samples:** Commit them **into** `deps/zephyr` (this plan). Do not put the design-spec test suite in the workspace manifest repo.
- **Interactive hacking only:** Scratch builds, ad-hoc apps, and debugging may use the workspace (`zephyr-devel` samples, `/tmp/b`, etc.). Do not treat those as the deliverable test suite.
- **v1 contract:** Only `ADC_REF_INTERNAL` must work where ops are implemented; unsupported enum → get `0`, set `-ENOTSUP`; `vref_mv == 0` on set → `-EINVAL`; getter owns DT fallback when present.
- **Out of scope:** STM32 measurement/cache/Kconfig; public `adc_vref_get/set`; routing non-internal DT conversions through `vref_get`; channel-id API.
- **Build/test host:** From workspace root with Zephyr env set (`ZEPHYR_BASE` → `deps/zephyr`). Prefer `uv run west …`. Host tests: `native_sim`.

---

## File map (Part 1)

| Path | Role |
|---|---|
| `deps/zephyr/include/zephyr/drivers/adc.h` | Driver ops + `adc_ref_internal` / `adc_ref_internal_set` |
| `deps/zephyr/drivers/adc/adc_handlers.c` | Userspace verify for `adc_ref_internal_set` |
| `deps/zephyr/drivers/adc/adc_emul.c` | Implement `vref_get` / `vref_set`; optionally wrap emul helper |
| `deps/zephyr/include/zephyr/drivers/adc/adc_emul.h` | Doc relationship to new API |
| `deps/zephyr/tests/drivers/adc/adc_emul/src/main.c` (+ yaml if needed) | Contract tests |
| `deps/zephyr/doc/hardware/peripherals/adc.rst` | Overview blurb |
| `deps/zephyr/doc/releases/release-notes-4.5.rst` (or current unreleased notes) | API Changes entry |
| `deps/zephyr/samples/drivers/adc/adc_dt/` | Minimal printk / optional set demo that tolerates `-ENOTSUP` |

---

### Task 1: Branch setup in `deps/zephyr`

**Files:** none (git only)

**Interfaces:**
- Consumes: current west Zephyr checkout
- Produces: branch `adc-vref-runtime-api`

- [ ] **Step 1: Create the Part 1 branch**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git status
git switch -c adc-vref-runtime-api
```

Expected: on branch `adc-vref-runtime-api`.

- [ ] **Step 2: Confirm commit policy**

When committing later, use plain `git commit -m "$(cat <<'EOF' … EOF)"` only — never `-s` / `--signoff`. Include body verification note, `Link:` to the RFC, and `Assisted-by:` (see Global Constraints).

- [ ] **Step 3: Commit is N/A** (branch creation only)

---

### Task 2: Common API in `adc.h` (+ syscall handler)

**Files:**
- Modify: `deps/zephyr/include/zephyr/drivers/adc.h` (`struct adc_driver_api`, `adc_ref_internal`, add `adc_ref_internal_set`)
- Modify: `deps/zephyr/drivers/adc/adc_handlers.c` (syscall verify)
- Test: `deps/zephyr/tests/drivers/adc/adc_emul/src/main.c` (cases land red until Task 3)

**Interfaces:**
- Consumes: existing `enum adc_reference`, `ref_internal`, `adc_raw_to_x_dt_chan`
- Produces:
  - `typedef uint16_t (*adc_api_vref_get)(…);` / `typedef int (*adc_api_vref_set)(…);`
  - `struct adc_driver_api` members `adc_api_vref_get vref_get;` / `adc_api_vref_set vref_set;`
  - `uint16_t adc_ref_internal(const struct device *dev);` (updated)
  - `int adc_ref_internal_set(const struct device *dev, uint16_t vref_mv);` (`__syscall`)

- [ ] **Step 1: Write ztest cases in adc_emul (will fail / not link until Task 3)**

Append tests (names illustrative):

```c
ZTEST_USER(adc_emul, test_adc_ref_internal_set_and_get)
{
	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);
	int ret;
	uint16_t before = adc_ref_internal(adc_dev);

	zassert_true(before > 0, "expected non-zero DT internal ref");

	ret = adc_ref_internal_set(adc_dev, 2500);
	zassert_ok(ret, "adc_ref_internal_set failed: %d", ret);
	zassert_equal(adc_ref_internal(adc_dev), 2500,
		      "getter did not observe set value");

	zassert_ok(adc_ref_internal_set(adc_dev, before));
}

ZTEST_USER(adc_emul, test_adc_ref_internal_set_rejects_zero)
{
	const struct device *adc_dev = DEVICE_DT_GET(ADC_DEVICE_NODE);

	zassert_equal(adc_ref_internal_set(adc_dev, 0), -EINVAL);
}
```

Also add a test that `adc_raw_to_millivolts_dt()` with an `ADC_REF_INTERNAL` channel tracks a distinctive value after `adc_ref_internal_set` (use existing emul channel setup patterns in this file).

Do **not** change `adc_raw_to_x_dt_chan` for non-internal refs.

- [ ] **Step 2: Extend `struct adc_driver_api` and wrappers in `adc.h`**

Follow existing ADC backend conventions: declare ops as named typedefs next to
`adc_api_channel_setup` / `adc_api_read` / … (before `__subsystem struct
adc_driver_api`), then use those typedefs as struct members. Mark the new ops
optional with `@driver_ops_optional` (same style as `get_decoder`). Place new
members **after** `ref_internal` so initializers that omit them zero-fill.

```c
/**
 * @brief Type definition of ADC API function for getting a reference
 *        voltage in millivolts.
 * See adc_ref_internal() for related public helper.
 */
typedef uint16_t (*adc_api_vref_get)(const struct device *dev,
				     enum adc_reference reference);

/**
 * @brief Type definition of ADC API function for setting a reference
 *        voltage in millivolts.
 * See adc_ref_internal_set() for argument descriptions.
 */
typedef int (*adc_api_vref_set)(const struct device *dev,
				enum adc_reference reference,
				uint16_t vref_mv);
```

In `struct adc_driver_api`:

```c
	/**
	 * @driver_ops_mandatory Internal reference voltage, in millivolts.
	 *
	 * Set to 0 if internal reference is not supported.
	 * Used as the fallback when @c vref_get is NULL (or, for drivers that
	 * implement @c vref_get, as the DT fallback the getter itself returns
	 * when no live cache is valid).
	 */
	uint16_t ref_internal;
	/**
	 * @driver_ops_optional Get the millivolt scale for a reference.
	 * When NULL, adc_ref_internal() returns @c ref_internal.
	 * Unsupported @p reference: return 0.
	 */
	adc_api_vref_get vref_get;
	/**
	 * @driver_ops_optional Set the millivolt scale for a reference.
	 * Does not reconfigure the hardware mux. When NULL,
	 * adc_ref_internal_set() returns -ENOTSUP.
	 */
	adc_api_vref_set vref_set;
```

Public wrappers:

```c
static inline uint16_t adc_ref_internal(const struct device *dev)
{
	const struct adc_driver_api *api = DEVICE_API_GET(adc, dev);

	if (api->vref_get != NULL) {
		return api->vref_get(dev, ADC_REF_INTERNAL);
	}

	return api->ref_internal;
}

__syscall int adc_ref_internal_set(const struct device *dev, uint16_t vref_mv);

static inline int z_impl_adc_ref_internal_set(const struct device *dev,
					      uint16_t vref_mv)
{
	const struct adc_driver_api *api = DEVICE_API_GET(adc, dev);

	if (api->vref_set == NULL) {
		return -ENOTSUP;
	}

	return api->vref_set(dev, ADC_REF_INTERNAL, vref_mv);
}
```

Document thoroughly on the public helpers and typedefs (optional; NULL = legacy; set does not change mux; zero → driver `-EINVAL`; unsupported reference → `-ENOTSUP`).

- [ ] **Step 3: Add userspace handler in `adc_handlers.c`**

Optional op: do **not** require a non-NULL `vref_set` via `K_SYSCALL_DRIVER_ADC(dev, vref_set)`. Verify the object is an ADC device (mirror neighboring handlers’ object check), then call `z_impl_adc_ref_internal_set`:

```c
static inline int z_vrfy_adc_ref_internal_set(const struct device *dev,
					      uint16_t vref_mv)
{
	K_OOPS(K_SYSCALL_OBJ(dev, K_OBJ_DRIVER_ADC));

	return z_impl_adc_ref_internal_set(dev, vref_mv);
}
#include <zephyr/syscalls/adc_ref_internal_set_mrsh.c>
```

Adjust the object-type macro if the tree’s ADC handlers use a different helper — copy the pattern from `z_vrfy_adc_channel_setup`’s device check style when in doubt.

- [ ] **Step 4: Commit (no signoff; with Assisted-by)**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git add include/zephyr/drivers/adc.h drivers/adc/adc_handlers.c \
  tests/drivers/adc/adc_emul/src/main.c
git commit -m "$(cat <<'EOF'
drivers: adc: add optional vref_get/set and ref_internal_set

Allow drivers to expose a runtime millivolt scale keyed by
enum adc_reference, with INTERNAL shorthands for applications.

Tested with: build regenerates syscalls; tests land green after Task 3 emul wiring

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.5
EOF
)"
```

---

### Task 3: Implement ops on `adc_emul`

**Files:**
- Modify: `deps/zephyr/drivers/adc/adc_emul.c`
- Modify: `deps/zephyr/include/zephyr/drivers/adc/adc_emul.h`
- Test: `deps/zephyr/tests/drivers/adc/adc_emul/`

**Interfaces:**
- Consumes: Task 2 API
- Produces: emul `vref_get` / `vref_set` on `DEVICE_API(adc, …)`; shared storage with `adc_emul_ref_voltage_set`

- [ ] **Step 1: Implement `adc_emul_vref_get` / `adc_emul_vref_set`**

Reuse existing `ref_int` / `ref_vdd` / `ref_ext0` / `ref_ext1` storage. Preferred structure:

1. Implement set switch once: `vref_mv == 0` → `-EINVAL`; unsupported enum → `-ENOTSUP`; else store under mutex.
2. Get: reuse / factor `adc_emul_get_ref_voltage` (unsupported → `0`; derive `VDD_1_2` etc. from `ref_vdd` as today).
3. Make `adc_emul_ref_voltage_set()` call the same set helper (update emul tests if historical `-EINVAL` for bad enum becomes `-ENOTSUP`).

```c
static uint16_t adc_emul_vref_get(const struct device *dev,
				  enum adc_reference reference)
{
	struct adc_emul_data *data = dev->data;

	return adc_emul_get_ref_voltage(data, reference);
}

static int adc_emul_vref_set(const struct device *dev,
			     enum adc_reference reference,
			     uint16_t vref_mv)
{
	struct adc_emul_data *data = dev->data;
	int err = 0;

	if (vref_mv == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&data->cfg_mtx, K_FOREVER);

	switch (reference) {
	case ADC_REF_VDD_1:
		data->ref_vdd = vref_mv;
		break;
	case ADC_REF_INTERNAL:
		data->ref_int = vref_mv;
		break;
	case ADC_REF_EXTERNAL0:
		data->ref_ext0 = vref_mv;
		break;
	case ADC_REF_EXTERNAL1:
		data->ref_ext1 = vref_mv;
		break;
	default:
		err = -ENOTSUP;
		break;
	}

	k_mutex_unlock(&data->cfg_mtx);

	return err;
}
```

Then thin-wrap `adc_emul_ref_voltage_set` to call `adc_emul_vref_set` (or shared static) so one path owns storage.

- [ ] **Step 2: Install pointers in `ADC_EMUL_INIT`**

```c
static DEVICE_API(adc, adc_emul_api_##_num) = {
	.channel_setup = adc_emul_channel_setup,
	.read = adc_emul_read,
	.ref_internal = DT_INST_PROP(_num, ref_internal_mv),
	.vref_get = adc_emul_vref_get,
	.vref_set = adc_emul_vref_set,
	IF_ENABLED(CONFIG_ADC_ASYNC, (.read_async = adc_emul_read_async,))
};
```

- [ ] **Step 3: Document in `adc_emul.h`**

State that `adc_emul_ref_voltage_set()` updates the same storage observed by `adc_ref_internal()` / `vref_get`.

- [ ] **Step 4: Run emul tests**

```bash
cd /home/lasse/projects/zephyr-devel
export ZEPHYR_BASE=/home/lasse/projects/zephyr-devel/deps/zephyr
uv run west twister -T deps/zephyr/tests/drivers/adc/adc_emul -p native_sim
```

Expected: all `drivers.adc.emul` tests PASS (including new cases).

- [ ] **Step 5: Commit (no signoff; with Assisted-by)**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git add drivers/adc/adc_emul.c include/zephyr/drivers/adc/adc_emul.h \
  tests/drivers/adc/adc_emul/
git commit -m "$(cat <<'EOF'
drivers: adc_emul: implement vref_get/set for runtime ref API

Wire emulator reference storage through the new optional ADC ops so
host tests can exercise adc_ref_internal_set().

Tested with: west twister -T tests/drivers/adc/adc_emul -p native_sim

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.5
EOF
)"
```

---

### Task 4: Docs + sample touch

**Files:**
- Modify: `deps/zephyr/doc/hardware/peripherals/adc.rst`
- Modify: `deps/zephyr/doc/releases/release-notes-4.5.rst` (or current unreleased notes file)
- Modify: `deps/zephyr/samples/drivers/adc/adc_dt/src/main.c` (+ README if needed)
- **No** migration-guide entry in Part 1 (no default vendor mV behavior change)

**Interfaces:**
- Consumes: public API from Task 2
- Produces: documented API + sample that still runs when set returns `-ENOTSUP`

- [ ] **Step 1: ADC overview blurb**

In `adc.rst` Overview: static vs optional dynamic INTERNAL millivolt scale; point to `adc_ref_internal()` / `adc_ref_internal_set()`; clarify `zephyr,vref-mv` remains for non-internal channel refs and is not routed through `vref_get` in this change.

- [ ] **Step 2: Release notes API Changes**

List `adc_ref_internal_set()` and optional `vref_get` / `vref_set` / updated `adc_ref_internal()`.

- [ ] **Step 3: Minimal `adc_dt` sample update**

```c
uint16_t vref = adc_ref_internal(spec->dev);

printk("ADC ref_internal: %u mV\n", vref);
```

Do not require set to succeed on hardware boards (`-ENOTSUP` is fine). Optional demo set may be `#ifdef` / Kconfig default `n`.

- [ ] **Step 4: Build sample for a board listed in `sample.yaml`**

```bash
cd /home/lasse/projects/zephyr-devel
export ZEPHYR_BASE=/home/lasse/projects/zephyr-devel/deps/zephyr
uv run west build -b <board_from_sample_yaml> -d /tmp/b_adc_dt \
  deps/zephyr/samples/drivers/adc/adc_dt
```

Expected: build succeeds.

- [ ] **Step 5: Commit (no signoff; with Assisted-by)**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git add doc/hardware/peripherals/adc.rst doc/releases/release-notes-4.5.rst \
  samples/drivers/adc/adc_dt/
git commit -m "$(cat <<'EOF'
docs: adc: document runtime internal reference get/set

Describe optional vref ops and adc_ref_internal_set; show ref_internal
in the adc_dt sample.

Tested with: west build of adc_dt for a board in sample.yaml; docs build if touched

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.5
EOF
)"
```

---

### Task 5: Part 1 verification gate

- [ ] **Step 1: Re-run emul twister**

```bash
cd /home/lasse/projects/zephyr-devel
export ZEPHYR_BASE=/home/lasse/projects/zephyr-devel/deps/zephyr
uv run west twister -T deps/zephyr/tests/drivers/adc/adc_emul -p native_sim
```

Expected: PASS.

- [ ] **Step 2: Run check_compliance.py on the Part 1 range**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
./scripts/ci/check_compliance.py -c manifest-rev..adc-vref-runtime-api
```

Expected: no unexpected failures; fix any real issues before handoff. (If `manifest-rev` is awkward, use the merge-base with upstream/`main` for this branch.)

- [ ] **Step 3: Confirm commit trailers on Part 1 commits**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git log --format=%B adc-vref-runtime-api --not manifest-rev | rg -i 'signed-off-by' || true
git log --format=%B adc-vref-runtime-api --not manifest-rev | rg -c '^Assisted-by:'
git log --format=%B adc-vref-runtime-api --not manifest-rev | rg -c '^Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971'
```

Expected: no `Signed-off-by`; every AI-authored commit has `Assisted-by:` and the RFC `Link:`.

- [ ] **Step 4: Hand off (human DCO / PR)**

Summarize branch name + commits. Do **not** open the upstream PR unless asked. Part 2 stacks on `adc-vref-runtime-api`.

Remind the human: before pushing upstream, add their `Signed-off-by:` (DCO; legal name + email matching Git author), keep `Assisted-by:` and RFC `Link:`, and when opening the PR include a body that summarizes the change and links [#113971](https://github.com/zephyrproject-rtos/zephyr/issues/113971). Watch CI after submission.

---

## Spec coverage (Part 1)

| Spec item | Task |
|---|---|
| Optional `vref_get` / `vref_set` | Task 2–3 |
| Public INTERNAL shorthands | Task 2 |
| Unsupported enum / zero / getter fallback (emul) | Task 3 + tests |
| DT helpers unchanged for non-internal | Task 2 (no helper rewrite) |
| Emul + unit tests | Task 2–3 |
| Docs + sample | Task 4 |
| STM32 / migration guide | Part 2 |
