# STM32 ADC VREFINT Calibrate (Part 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On top of Part 1's landed `adc_ref_get()` API, make `adc_stm32` measure VREF+ from VREFINT, cache it SoC-wide, and expose it through `adc_ref_get()` / `adc_ref_internal()`.

**Architecture:** Kconfig-gated feature reuses `st,stm32-vref` DT (channel, cal, shift) inside `adc_stm32` (no sensor-driver calls). Shared mutex-protected rail cache; all STM32 ADC instances install the same `ref_get` op; only the VREFINT-owning ADC measures at init and on `sequence.calibrate`. Measure failure keeps DT fallback; ADC init still succeeds. No setter.

**Tech Stack:** `drivers/adc/adc_stm32.c`, STM32 LL ADC VREFINT path, existing `stm32_vref` sensor as formula/reference, twister on STM32 `platform_allow` boards, west/`uv run`.

**Spec:** `docs/superpowers/specs/2026-08-13-adc-ref-get-stm32-vrefint-design.md`  
**Supersedes plan:** `docs/superpowers/plans/2026-08-02-adc-stm32-vrefint-calibrate.md` (wired to `vref_get`/`vref_set`)  
**Depends on:** Part 1 API from upstream [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439) (`adc_ref_get` / `adc_api_ref_get`).

## Global Constraints

- **Work tree:** Edit upstream code/tests/docs/samples only in `deps/zephyr/` (west-managed).
- **Branch (Part 2):** `adc-stm32-vrefint-calibrate`, created from a Zephyr revision that already has `adc_ref_get` in `include/zephyr/drivers/adc.h`. After [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439) merges, use Finwood/`upstream` `main`. Until then, stack on `cursor/adc-vref-runtime-api-c2e3`.
- **Commits:** Do **not** use `git commit -s`, `--signoff`, or `Signed-off-by`. Human prepares DCO/upstream history later.
- **AI attribution:** Every commit in `deps/zephyr` that an AI agent helped author **must** include an `Assisted-by:` trailer per Zephyr’s [Usage disclosure and attribution](https://docs.zephyrproject.org/latest/contribute/guidelines.html#usage-disclosure-and-attribution):

  ```
  Assisted-by: [Agent Name]:[Model Version] [Tool1] [Tool2]
  ```

  - `[Agent Name]` — AI tool/framework (e.g. `Cursor`)
  - `[Model Version]` — specific model used for that commit (e.g. `grok-4.6`)
  - `[Tool1] [Tool2]` — optional specialized analysis tools only; do **not** list basic tools (git, gcc, make, editors, west, twister)
  - Place the trailer in the commit message body/footer (blank line after subject/body), **without** `Signed-off-by`
  - Example: `Assisted-by: Cursor:grok-4.6`
  - Use the **actual** agent and model for the session that produced the commit
- **Commit messages (Zephyr style):** Title `area: summary` (<72 chars), blank line, non-empty body explaining what/why. In the body/footer also include:
  - A short **verification** note (e.g. `Tested with: west build -b nucleo_l476rg samples/drivers/adc/adc_dt`)
  - `Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971` (RFC). Prefer `Link:` until the feature lands.
  - `Assisted-by:` (see above). Do **not** add `Signed-off-by` in agent commits.
- **New files:** Add SPDX/REUSE headers at the top (native comment syntax), matching neighboring files — typically `SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors` and `SPDX-License-Identifier: Apache-2.0`. In the commit that introduces new original files, include `Origin: Original` in the commit message footer.
- **Compliance:** Before handoff, run `./scripts/ci/check_compliance.py -c <commit-range>` from `deps/zephyr`. Fix reported issues.
- **Upstream PR (human):** Agent does not open PRs unless asked. When the human prepares upstream PRs: rewrite/add `Signed-off-by` (DCO; legal name + real email matching Git author), keep `Assisted-by:`, reference the RFC in the PR body, watch CI.
- **Formal tests & samples:** Live in `deps/zephyr`. Workspace repo is for interactive hacking/debugging only.
- **v1 STM32 contract:** `ref_get` supports only `ADC_REF_INTERNAL` (`0` + mV, or `-ENODATA` if mV is 0). Other enums → `-ENOTSUP`. Cache invalid → that instance’s DT `vref-mv`. No public or driver-api setter.
- **Out of scope:** `adc_ref_internal_set()` / `vref_set`; channel-id API; provider/`reference-supplies`; removing `st,stm32-vref` sensor; auto re-measure on PM resume; implementing non-INTERNAL `ref_get` on STM32.
- **Build:** `ZEPHYR_BASE=…/deps/zephyr`, `uv run west …`. Prefer `nucleo_l476rg` (has `&vref { status = "okay"; }`) for integration builds.

---

## File map (Part 2)

| Path | Role |
|---|---|
| `deps/zephyr/drivers/adc/Kconfig.stm32` | `ADC_STM32_VREFINT_CALIBRATE` (+ optional nvmem helper symbol) |
| `deps/zephyr/drivers/adc/adc_stm32.c` | Cache, measure, `ref_get`, init/calibrate hooks, per-instance `vref-mv`, stream live vref |
| `deps/zephyr/dts/bindings/adc/st,stm32-adc.yaml` | Clarify `vref-mv` as fallback |
| `deps/zephyr/dts/bindings/sensor/st,stm32-vref.yaml` | Note ADC may consume node for cal |
| `deps/zephyr/tests/drivers/adc/adc_stm32_vref/` | STM32 integration cases from spec §6.2 |
| `deps/zephyr/samples/drivers/adc/adc_dt/src/main.c` | Print `adc_ref_internal()` |
| `deps/zephyr/samples/sensor/soc_voltage/README.rst` | Short cross-link |
| `deps/zephyr/doc/releases/migration-guide-4.5.rst` | Measured mV + per-instance `vref-mv` |
| `deps/zephyr/doc/releases/release-notes-4.5.rst` | Driver/feature note + migration pointer |

**Reference implementation (read-only):** `deps/zephyr/drivers/sensor/st/stm32_vref/stm32_vref.c` for enable-path, cal read, and numerator/raw formula.

---

### Task 1: Stack branch on Part 1 API

**Files:** none (git only)

**Interfaces:**
- Consumes: Zephyr revision with `adc_ref_get` / `adc_api_ref_get` in `include/zephyr/drivers/adc.h`
- Produces: branch `adc-stm32-vrefint-calibrate`

- [ ] **Step 1: Verify Part 1 is present**

```bash
cd /agent/repos/zephyr   # or $ZEPHYR_BASE / deps/zephyr
git log -1 --oneline
rg -n "adc_api_ref_get|adc_ref_get" include/zephyr/drivers/adc.h | head
```

Expected: `typedef int (*adc_api_ref_get)` and `static inline int adc_ref_get(` exist. If missing, check out `cursor/adc-vref-runtime-api-c2e3` or wait for [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439) to merge, then retry.

- [ ] **Step 2: Create Part 2 branch**

```bash
git switch -c adc-stm32-vrefint-calibrate
```

- [ ] **Step 3: Commit N/A**

---

### Task 2: Kconfig + shared cache + `ref_get` + per-instance API

**Files:**
- Modify: `deps/zephyr/drivers/adc/Kconfig.stm32`
- Modify: `deps/zephyr/drivers/adc/adc_stm32.c`
- Test: compile later (Task 3 / Task 6)

**Interfaces:**
- Consumes: Part 1 `adc_api_ref_get ref_get`
- Produces: `CONFIG_ADC_STM32_VREFINT_CALIBRATE`; `stm32_adc_vref` cache; INTERNAL-only `adc_stm32_ref_get`; per-instance `.ref_internal`

- [ ] **Step 1: Add Kconfig at the end of `drivers/adc/Kconfig.stm32` (inside the existing `if ADC_STM32 || ADC_STM32WB0` is **wrong** — put it under `ADC_STM32` only, after the `ADC_STM32` block / inside a new `if ADC_STM32`)**

Place after the `ADC_STM32` `help` block (before `ADC_STM32WB0`), or in a dedicated `if ADC_STM32` / `endif` pair so WB0 is not included:

```
if ADC_STM32

config ADC_STM32_VREFINT_CALIBRATE
	bool "Measure VREF+ from VREFINT at init/calibrate"
	depends on DT_HAS_ST_STM32_VREF_ENABLED
	default y
	help
	  Measure the ADC reference rail (VREF+/VDDA) using the VREFINT
	  channel and factory calibration described by the st,stm32-vref
	  node. The value is cached SoC-wide and returned by
	  adc_ref_internal() / adc_ref_get() for ADC_REF_INTERNAL on all
	  STM32 ADC instances. Re-measure when sequence.calibrate is set
	  on the VREFINT-owning ADC.

config ADC_STM32_VREFINT_CALIB_VIA_NVMEM
	bool
	default y
	depends on ADC_STM32_VREFINT_CALIBRATE && OTP && NVMEM
	help
	  Read VREFINT_CAL via the NVMEM API instead of a raw OTP pointer.
	  Independent of CONFIG_STM32_VREF (the sensor).

endif # ADC_STM32
```

- [ ] **Step 2: Add `vref_mv` to `struct adc_stm32_cfg` and shared cache + `ref_get`**

In `struct adc_stm32_cfg` (after the existing bitfields is fine; `uint16_t` should not sit in the bitfield block):

```c
	uint16_t vref_mv;
```

Near the top of the file, after includes / `STM32_ADC_VREF_MV`, add (behind the Kconfig):

```c
#ifdef CONFIG_ADC_STM32_VREFINT_CALIBRATE

#define STM32_VREF_NODE		DT_INST(0, st_stm32_vref)
#define STM32_VREF_ADC_NODE	DT_IO_CHANNELS_CTLR(STM32_VREF_NODE)
#define STM32_VREFINT_CHANNEL	DT_IO_CHANNELS_INPUT(STM32_VREF_NODE)
#define STM32_VREFINT_MEAS_RES	12U

K_MUTEX_DEFINE(stm32_adc_vref_lock);

static struct {
	uint16_t mv;
	bool valid;
} stm32_adc_vref;

static int adc_stm32_ref_get(const struct device *dev, enum adc_reference ref,
			     uint16_t *vref_mv)
{
	const struct adc_stm32_cfg *cfg = dev->config;

	if (vref_mv == NULL) {
		return -EINVAL;
	}
	if (ref != ADC_REF_INTERNAL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&stm32_adc_vref_lock, K_FOREVER);
	*vref_mv = stm32_adc_vref.valid ? stm32_adc_vref.mv : cfg->vref_mv;
	k_mutex_unlock(&stm32_adc_vref_lock);

	return (*vref_mv == 0U) ? -ENODATA : 0;
}

static bool adc_stm32_is_vrefint_owner(const struct device *dev)
{
	return dev == DEVICE_DT_GET(STM32_VREF_ADC_NODE);
}

#endif /* CONFIG_ADC_STM32_VREFINT_CALIBRATE */
```

Need `#include <zephyr/sys/util.h>` only if not already present. `enum adc_reference` comes from `adc.h` (already included).

- [ ] **Step 3: Per-instance `DEVICE_API` + store `vref_mv` in cfg**

Today a single `api_stm32_driver_api` uses `STM32_ADC_VREF_MV` (`DT_INST_PROP(0, vref_mv)`). Replace that shared API with a per-instance API inside `ADC_STM32_INIT`.

In the `adc_stm32_cfg_##index` initializer, add:

```c
		.vref_mv = DT_INST_PROP(index, vref_mv),
```

Replace the file-scope `static DEVICE_API(adc, api_stm32_driver_api) = { ... }` and the `DEVICE_DT_INST_DEFINE(..., &api_stm32_driver_api)` with:

```c
#define ADC_STM32_DRIVER_API(index)						\
	static DEVICE_API(adc, adc_stm32_api_##index) = {			\
		.channel_setup = adc_stm32_channel_setup,			\
		.read = adc_stm32_read_sync,					\
		IF_ENABLED(CONFIG_ADC_ASYNC,					\
			   (.read_async = adc_stm32_read_async,))		\
		.ref_internal = DT_INST_PROP(index, vref_mv),			\
		IF_ENABLED(CONFIG_ADC_STM32_VREFINT_CALIBRATE,			\
			   (.ref_get = adc_stm32_ref_get,))			\
		IF_ENABLED(CONFIG_ADC_STREAM,					\
			   (.submit = adc_stm32_submit_stream,			\
			    .get_decoder = adc_stm32_get_decoder,))		\
	}
```

Invoke `ADC_STM32_DRIVER_API(index);` inside `ADC_STM32_INIT` before `DEVICE_DT_INST_DEFINE`, and pass `&adc_stm32_api_##index` instead of `&api_stm32_driver_api`.

Keep `STM32_ADC_VREF_MV` until Task 3 stream change, or replace stream use in this commit if it is a one-liner — prefer Task 3 so this commit is API-wiring only.

**Breaking change — must be documented (Task 4):** multi-ADC boards with divergent per-instance `vref-mv` previously saw every ADC report instance 0’s value.

- [ ] **Step 4: Commit (no signoff; with Assisted-by)**

```bash
cd /agent/repos/zephyr
git add drivers/adc/Kconfig.stm32 drivers/adc/adc_stm32.c
git commit -m "$(cat <<'EOF'
drivers: adc_stm32: add vref cache and optional ref_get

Introduce Kconfig-gated shared VREF+ cache wired to adc_ref_get,
with per-instance DT vref-mv as fallback.

Tested with: west build -b nucleo_l476rg samples/drivers/adc/adc_dt

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.6
EOF
)"
```

---

### Task 3: VREFINT measurement + init/calibrate hooks + stream

**Files:**
- Modify: `deps/zephyr/drivers/adc/adc_stm32.c`
- Read-only: `deps/zephyr/drivers/sensor/st/stm32_vref/stm32_vref.c`

**Interfaces:**
- Consumes: Task 2 cache; `st,stm32-vref` DT; existing `adc_stm32_enable` / `adc_stm32_start_conversion` / `set_sequencer` / `set_resolution` / `adc_stm32_sampling_time_setup`
- Produces: measure helper; seed on owner init; refresh on owner `sequence.calibrate`; live stream `vref_mv`

- [ ] **Step 1: Cal constants + path enable (match sensor)**

Reuse the sensor’s OTP vs nvmem pattern, keyed by `CONFIG_ADC_STM32_VREFINT_CALIB_VIA_NVMEM` (not `CONFIG_STM32_VREF_*`). Copy the `VREFINT_CAL_INIT` macros from `stm32_vref.c`, substituting `DT_INST_*(0, …)` with `STM32_VREF_NODE` / `DT_INST(0, st_stm32_vref)` as needed.

Also copy enable/disable of `LL_ADC_PATH_INTERNAL_VREFINT` (and `LL_ADC_DELAY_VREFINT_STAB_US` sleep). Include `<zephyr/nvmem.h>` and `<zephyr/cache.h>` only inside the calibrate `#ifdef` if the sensor does.

Numerator (compute once at first measure or as a static):

```c
/* mv = (cal_mv * (vrefint_cal >> shift)) / raw */
shift = DT_PROP(STM32_VREF_NODE, vrefint_cal_resolution) - STM32_VREFINT_MEAS_RES;
cal_mv = DT_PROP(STM32_VREF_NODE, vrefint_cal_mv);
```

`BUILD_ASSERT(DT_PROP(STM32_VREF_NODE, vrefint_cal_resolution) >= STM32_VREFINT_MEAS_RES)`.

- [ ] **Step 2: Implement `adc_stm32_vrefint_measure()` without nested `adc_context`**

Do **not** call `adc_channel_setup()` or `adc_read()`. Both take `adc_context`; init starts with the context locked, and `start_read()` already holds it during `sequence.calibrate`.

Sketch (adapt LL wait-for-EOC to the same flags this file already uses for non-DMA reads; poll with a timeout, do not wait forever):

```c
#ifdef CONFIG_ADC_STM32_VREFINT_CALIBRATE
static int adc_stm32_vrefint_measure(const struct device *dev)
{
	const struct adc_stm32_cfg *cfg = dev->config;
	ADC_TypeDef *adc = cfg->base;
	struct adc_channel_cfg ch = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_MAX,
		.channel_id = STM32_VREFINT_CHANNEL,
		.differential = 0,
	};
	uint16_t raw = 0;
	uint16_t vrefint_cal = 0;
	uint16_t mv;
	int err;

	/* Read factory cal (nvmem or OTP pointer; match stm32_vref.c). */
	/* On failure: LOG_ERR; return err; do not mark cache valid. */

	err = adc_stm32_sampling_time_setup(dev, ch.channel_id, ch.acquisition_time);
	if (err) {
		return err;
	}

	/* Temporarily treat this instance as a 1-channel VREFINT sequence.
	 * Caller must re-run set_resolution+set_sequencer for a user sequence.
	 */
	struct adc_stm32_data *data = dev->data;
	uint32_t saved_channels = data->channels;
	uint8_t saved_count = data->channel_count;
	uint8_t saved_res = data->resolution;

	data->channels = BIT(STM32_VREFINT_CHANNEL);
	data->channel_count = 1;
	data->resolution = STM32_VREFINT_MEAS_RES;

	err = set_resolution(dev, &(const struct adc_sequence){
		.resolution = STM32_VREFINT_MEAS_RES,
	});
	if (err) {
		goto restore;
	}
	err = set_sequencer(dev);
	if (err) {
		goto restore;
	}

	/* Enable VREFINT path + stab delay (copy from stm32_vref.c). */

	err = adc_stm32_enable(adc);
	if (err) {
		goto disable_path;
	}

	adc_stm32_start_conversion(dev);

	/* Poll EOC/EOS the same way this driver does for the series
	 * (LL_ADC_IsActiveFlag_EOC / EOCS / EOS). Timeout → err = -EIO.
	 * Then: raw = LL_ADC_REG_ReadConversionData32(adc);
	 */

	if (raw == 0U) {
		err = -ENODATA;
		goto disable_path;
	}

	{
		uint8_t shift = DT_PROP(STM32_VREF_NODE, vrefint_cal_resolution)
				- STM32_VREFINT_MEAS_RES;
		int32_t numerator = DT_PROP(STM32_VREF_NODE, vrefint_cal_mv)
				    * (vrefint_cal >> shift);

		mv = (uint16_t)(numerator / raw);
	}

	k_mutex_lock(&stm32_adc_vref_lock, K_FOREVER);
	stm32_adc_vref.mv = mv;
	stm32_adc_vref.valid = true;
	k_mutex_unlock(&stm32_adc_vref_lock);

disable_path:
	/* Disable VREFINT path unless CONFIG_STM32_VREF_INJECTED-style
	 * permanent enable is required for this series/config.
	 */
restore:
	data->channels = saved_channels;
	data->channel_count = saved_count;
	data->resolution = saved_res;
	return err;
}
#endif /* CONFIG_ADC_STM32_VREFINT_CALIBRATE */
```

If `set_resolution` / `set_sequencer` are `static` further down the file, move the measure helper below them, or add static prototypes.

Fill in cal-read and EOC-poll from neighboring functions in the same file plus `stm32_vref.c`. Do not invent a second ADC enable path.

- [ ] **Step 3: Call measure from owner init**

In `adc_stm32_init`, after `#if defined(HAS_CALIBRATION) adc_stm32_calibrate(dev, true); … #endif` and **before** the final `adc_stm32_disable` / `adc_context_unlock_unconditionally`:

```c
#ifdef CONFIG_ADC_STM32_VREFINT_CALIBRATE
	if (adc_stm32_is_vrefint_owner(dev)) {
		int merr = adc_stm32_vrefint_measure(dev);

		if (merr) {
			LOG_WRN("VREFINT measure failed (%d); using DT vref-mv", merr);
		}
	}
#endif /* CONFIG_ADC_STM32_VREFINT_CALIBRATE */
```

Failure must **not** fail ADC init.

- [ ] **Step 4: Call measure when `sequence.calibrate` on owner**

In `start_read()`, immediately after the existing `if (sequence->calibrate) { adc_stm32_calibrate(...); }` block:

```c
#ifdef CONFIG_ADC_STM32_VREFINT_CALIBRATE
	if (sequence->calibrate && adc_stm32_is_vrefint_owner(dev)) {
		int merr = adc_stm32_vrefint_measure(dev);

		if (merr) {
			LOG_WRN("VREFINT measure failed (%d)", merr);
		}
		/* Measure may have overwritten sequencer/resolution. */
		err = set_resolution(dev, sequence);
		if (err < 0) {
			return err;
		}
		err = set_sequencer(dev);
		if (err < 0) {
			return err;
		}
	}
#endif /* CONFIG_ADC_STM32_VREFINT_CALIBRATE */
```

Do not fail the user sequence solely because VREFINT refresh failed (HW cal already succeeded). Re-apply sequencer/resolution even on measure failure.

Non-owner: no extra work.

- [ ] **Step 5: Stream / RTIO live vref**

Replace:

```c
			hdr->vref_mv = STM32_ADC_VREF_MV;
```

with:

```c
			hdr->vref_mv = adc_ref_internal(dev);
```

(`adc.h` is already included.) If `STM32_ADC_VREF_MV` has no remaining uses, delete the `#define`.

- [ ] **Step 6: No PM-resume auto re-measure** (explicit non-goal — do not add code in `adc_stm32_pm_action`)

- [ ] **Step 7: Commit (no signoff; with Assisted-by)**

```bash
cd /agent/repos/zephyr
git add drivers/adc/adc_stm32.c
git commit -m "$(cat <<'EOF'
drivers: adc_stm32: seed VREF+ cache from VREFINT calibration

Measure the shared rail at init and on calibrate for the VREFINT-owning
ADC so adc_ref_internal() returns a hardware-based scale.

Tested with: west build -b nucleo_l476rg samples/drivers/adc/adc_dt

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.6
EOF
)"
```

---

### Task 4: Bindings, migration guide, samples

**Files:**
- Modify: `deps/zephyr/dts/bindings/adc/st,stm32-adc.yaml`
- Modify: `deps/zephyr/dts/bindings/sensor/st,stm32-vref.yaml`
- Modify: `deps/zephyr/doc/releases/migration-guide-4.5.rst`
- Modify: `deps/zephyr/doc/releases/release-notes-4.5.rst`
- Modify: `deps/zephyr/samples/drivers/adc/adc_dt/src/main.c`
- Modify: `deps/zephyr/samples/sensor/soc_voltage/README.rst`

**Interfaces:**
- Consumes: Task 2–3 behavior
- Produces: documented fallback semantics + migration warning + sample printk

- [ ] **Step 1: Binding text**

`st,stm32-adc.yaml` `vref-mv` description — replace the current one-liner with:

```yaml
  vref-mv:
    type: int
    default: 3300
    description: |
      Board nominal ADC reference voltage in millivolts (VREF+/VDDA).

      Used as the static adc_ref_internal() value when
      CONFIG_ADC_STM32_VREFINT_CALIBRATE is disabled, and as the fallback
      when VREFINT measurement has not yet produced a cached value.
      When that Kconfig is enabled, adc_ref_internal() may return a
      measured scale instead of this property.
```

`st,stm32-vref.yaml` — append to the compatible `description:`:

```yaml
description: |
  STM32 VREF+.

  The st,stm32-vref sensor driver can report VREF+ via the sensor API.
  The STM32 ADC driver may also consume this node (channel, factory cal)
  when CONFIG_ADC_STM32_VREFINT_CALIBRATE is enabled, to seed
  adc_ref_internal(). The sensor remains independently usable.
```

- [ ] **Step 2: Migration guide**

In `doc/releases/migration-guide-4.5.rst`, under the existing `ADC` `===` section, **inside** the `zephyr-keep-sorted` block, add a bullet that sorts with the other ADC entries (after the girqs bullet, before Audio Codec is a different heading — stay in ADC):

```rst
* STM32 ADC (:dtcompatible:`st,stm32-adc`): when
  :kconfig:option:`CONFIG_ADC_STM32_VREFINT_CALIBRATE` is enabled (default
  whenever an okay :dtcompatible:`st,stm32-vref` node exists),
  :c:func:`adc_ref_internal` and INTERNAL :c:func:`adc_raw_to_millivolts_dt`
  results may no longer match DT ``vref-mv`` / 3300 exactly. Disable the
  Kconfig to keep the previous static DT-only scale.

  ``adc_ref_internal()`` no longer reports instance 0's ``vref-mv`` for every
  STM32 ADC instance. Each instance uses its own ``vref-mv`` (optional,
  default 3300). This only affects multi-ADC DTs that set divergent
  ``vref-mv`` values and relied on the old shared ``DEVICE_API``.
```

Adjust wording if keep-sorted requires a specific first letter; keep the STM32 ADC bullet next to other ADC bullets.

In `doc/releases/release-notes-4.5.rst`, add a Drivers/ADC note pointing at the migration guide (Part 1 already has an API Changes entry for `adc_ref_get`).

- [ ] **Step 3: Sample / soc_voltage README**

In `samples/drivers/adc/adc_dt/src/main.c`, after a successful `adc_channel_setup_dt`, print the controller scale once per distinct `spec->dev` (or simply each iteration — acceptable):

```c
			printk("- %s, channel %d, ref_internal %u mV: ",
			       adc_channels[i].dev->name,
			       adc_channels[i].channel_id,
			       adc_ref_internal(adc_channels[i].dev));
```

Replace the existing `printk("- %s, channel %d: ", …)` line so the sample still runs when `adc_ref_internal()` returns 0.

In `samples/sensor/soc_voltage/README.rst`, after Overview, add:

```rst
On STM32 boards with an okay ``st,stm32-vref`` node and
``CONFIG_ADC_STM32_VREFINT_CALIBRATE=y``, the voltage reported here should
agree with :c:func:`adc_ref_internal` on the VREFINT-owning ADC. Prefer the
ADC API for ``adc_raw_to_millivolts_dt()``; use this sensor sample for
explicit voltage channels (VREF+/VBAT).
```

- [ ] **Step 4: Commit (no signoff; with Assisted-by)**

```bash
cd /agent/repos/zephyr
git add dts/bindings/adc/st,stm32-adc.yaml \
  dts/bindings/sensor/st,stm32-vref.yaml \
  doc/releases/migration-guide-4.5.rst \
  doc/releases/release-notes-4.5.rst \
  samples/drivers/adc/adc_dt/src/main.c \
  samples/sensor/soc_voltage/README.rst
git commit -m "$(cat <<'EOF'
docs: adc_stm32: document VREFINT-calibrated internal reference

Clarify DT fallback, migration impact, and sample/sensor cross-links.

Tested with: bindings/migration text review

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.6
EOF
)"
```

---

### Task 5: STM32 integration tests

**Files:**
- Create: `deps/zephyr/tests/drivers/adc/adc_stm32_vref/src/main.c`
- Create: `deps/zephyr/tests/drivers/adc/adc_stm32_vref/CMakeLists.txt`
- Create: `deps/zephyr/tests/drivers/adc/adc_stm32_vref/prj.conf`
- Create: `deps/zephyr/tests/drivers/adc/adc_stm32_vref/testcase.yaml`

**Interfaces:**
- Consumes: Part 1 `adc_ref_get` + Part 2 driver
- Produces: automated checks for spec §6.2

- [ ] **Step 1: Add SPDX headers and test sources**

`CMakeLists.txt`:

```cmake
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(adc_stm32_vref)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
```

`prj.conf`:

```
CONFIG_ZTEST=y
CONFIG_ADC=y
CONFIG_ADC_STM32=y
```

`testcase.yaml`:

```yaml
common:
  tags:
    - adc
    - drivers
    - stm32
  platform_allow:
    - nucleo_l476rg
    - nucleo_g071rb
    - disco_l475_iot1
  integration_platforms:
    - nucleo_l476rg
  filter: dt_compat_enabled("st,stm32-vref")
tests:
  drivers.adc.stm32.vrefint:
    extra_configs:
      - CONFIG_ADC_STM32_VREFINT_CALIBRATE=y
  drivers.adc.stm32.vrefint.off:
    extra_configs:
      - CONFIG_ADC_STM32_VREFINT_CALIBRATE=n
```

`src/main.c` (trim if a board has only one ADC; skip second-ADC assert when `adc2` is not okay):

```c
/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/adc.h>
#include <zephyr/ztest.h>

#define ADC1_NODE DT_NODELABEL(adc1)

ZTEST(adc_stm32_vref, test_ref_internal_after_boot)
{
	const struct device *adc1 = DEVICE_DT_GET(ADC1_NODE);
	uint16_t mv;
	int ret;

	zassert_true(device_is_ready(adc1), "adc1 not ready");

	ret = adc_ref_get(adc1, ADC_REF_INTERNAL, &mv);
	zassert_ok(ret, "adc_ref_get(INTERNAL) failed: %d", ret);
	zassert_true(mv > 0, "expected positive INTERNAL mV");
	zassert_equal(adc_ref_internal(adc1), mv,
		      "adc_ref_internal mismatch");

	ret = adc_ref_get(adc1, ADC_REF_EXTERNAL0, &mv);
	zassert_equal(ret, -ENOTSUP,
		      "EXTERNAL0 should be -ENOTSUP, got %d", ret);
}

ZTEST(adc_stm32_vref, test_calibrate_refresh_does_not_fail)
{
	const struct device *adc1 = DEVICE_DT_GET(ADC1_NODE);
	int16_t buf;
	struct adc_sequence seq = {
		.channels = BIT(0),
		.buffer = &buf,
		.buffer_size = sizeof(buf),
		.resolution = 12,
		.calibrate = true,
	};
	int ret;

	zassert_true(device_is_ready(adc1), "adc1 not ready");

	/* Channel 0 may not be a valid analog pin on every board; the point
	 * is that sequence.calibrate on the owner still completes and
	 * adc_ref_internal() remains usable. If adc_read fails for pinmux,
	 * skip rather than fail the suite.
	 */
	ret = adc_read(adc1, &seq);
	if (ret == -EINVAL || ret == -ENOTSUP) {
		ztest_test_skip();
	}
	zassert_ok(ret, "calibrate sequence failed: %d", ret);
	zassert_true(adc_ref_internal(adc1) > 0, "ref after calibrate");
}

ZTEST_SUITE(adc_stm32_vref, NULL, NULL, NULL, NULL, NULL);
```

For `drivers.adc.stm32.vrefint.off`, assert `adc_ref_internal()` equals DT `vref-mv` of adc1 (`DT_PROP(DT_NODELABEL(adc1), vref_mv)` with default 3300). Split that into a second file or `#ifndef CONFIG_ADC_STM32_VREFINT_CALIBRATE` in the same suite.

If creating a **new** test directory, put `Origin: Original` in that commit's footer.

- [ ] **Step 2: Run twister for allowed platforms when HW/SDK is available**

```bash
cd /agent/repos/zephyr-devel
export ZEPHYR_BASE=/agent/repos/zephyr-devel/deps/zephyr
uv run west twister -T deps/zephyr/tests/drivers/adc/adc_stm32_vref \
  -p nucleo_l476rg --build-only
```

Expected: build PASS. Runtime PASS only on hardware.

- [ ] **Step 3: Build smoke with feature on/off**

```bash
uv run west build -b nucleo_l476rg -d /tmp/b_adc_dt \
  deps/zephyr/samples/drivers/adc/adc_dt
uv run west build -b nucleo_l476rg -d /tmp/b_adc_dt_off \
  deps/zephyr/samples/drivers/adc/adc_dt -- \
  -DCONFIG_ADC_STM32_VREFINT_CALIBRATE=n
```

Expected: both builds succeed on a board whose overlay enables ADC.

- [ ] **Step 4: Commit (no signoff; with Assisted-by)**

```bash
cd /agent/repos/zephyr
git add tests/drivers/adc/adc_stm32_vref/
git commit -m "$(cat <<'EOF'
tests: adc_stm32: cover VREFINT-calibrated adc_ref_internal

Verify INTERNAL ref_get, unsupported enums, calibrate path, and
Kconfig-off DT fallback.

Tested with: west twister -T tests/drivers/adc/adc_stm32_vref -p nucleo_l476rg --build-only

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971
Origin: Original

Assisted-by: Cursor:grok-4.6
EOF
)"
```

---

### Task 6: Part 2 verification gate

- [ ] **Step 1: Confirm stack**

```bash
cd /agent/repos/zephyr
git log --oneline origin/cursor/adc-vref-runtime-api-c2e3..HEAD
# or, after #115439 merges: git log --oneline origin/main..HEAD
```

Expected: Part 2 commits only (Kconfig/cache, measure, docs, tests).

- [ ] **Step 2: Run check_compliance.py on the Part 2 range**

```bash
cd /agent/repos/zephyr
./scripts/ci/check_compliance.py -c origin/cursor/adc-vref-runtime-api-c2e3..HEAD
```

Expected: no unexpected failures; fix any real issues before handoff.

- [ ] **Step 3: Confirm commit trailers on Part 2 commits**

```bash
git log --format=%B HEAD --not origin/cursor/adc-vref-runtime-api-c2e3 \
  | rg -i 'signed-off-by' || true
git log --format=%B HEAD --not origin/cursor/adc-vref-runtime-api-c2e3 \
  | rg -c '^Assisted-by:'
git log --format=%B HEAD --not origin/cursor/adc-vref-runtime-api-c2e3 \
  | rg -c '^Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971'
```

Expected: no `Signed-off-by`; every AI-authored Part 2 commit has `Assisted-by:` and the RFC `Link:`. The tests commit includes `Origin: Original`.

- [ ] **Step 4: Hand off (human DCO / PR)**

Summarize the branch for the human’s upstream PR. Do not push/open the Zephyr PR unless asked.

Remind the human: add `Signed-off-by:` before upstream push, keep `Assisted-by:` / RFC `Link:`, PR body should summarize behavior/breaking changes, link [#113971](https://github.com/zephyrproject-rtos/zephyr/issues/113971) and [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439), mention default-on measured mV and the lock-free conversion path (ST review), and watch CI.

---

## Spec coverage (Part 2)

| Spec item | Task |
|---|---|
| Kconfig + install `ref_get` | Task 2 |
| Shared cache / INTERNAL-only / per-instance `vref-mv` | Task 2 |
| Measure formula / init / calibrate / sequencer restore | Task 3 |
| Stream live vref / no PM auto refresh | Task 3 |
| Bindings + migration + samples | Task 4 |
| Integration tests §6.2 | Task 5 |
| Common API / emul | Part 1 (already landed) |
