# STM32 ADC Dynamic VREF+ via VREFINT — Design

- **Date:** 2026-07-18 (API shape revised 2026-08-02)
- **Status:** Approved design (pre-implementation)
- **Scope:** Zephyr ADC common API extension + `adc_stm32` driver support for
  measured / runtime-configurable internal reference millivolts
- **Upstream target:** `zephyrproject-rtos/zephyr` (design captured here for
  implementation planning; changes land against Zephyr’s ADC subsystem)
- **Related:** RFC [#113971](https://github.com/zephyrproject-rtos/zephyr/issues/113971);
  PR/RFC paste draft `2026-07-18-zephyr-adc-runtime-ref-internal-pr-draft.md`
  (not updated in this revision — still describes the older INTERNAL-only ops)

## 1. Purpose & goals

STM32 ADCs convert against **VREF+/VDDA**, not against the factory bandgap.
Zephyr’s STM32 ADC driver today exposes that scale only as a static DT property
`vref-mv` (default 3300), read into `adc_driver_api.ref_internal`. Apps that need
accurate millivolt conversion must compensate in user code (e.g. via the
`st,stm32-vref` sensor).

**Goals:**

1. Measure VREF+ from VREFINT + factory `VREFINT_CAL` **inside the ADC driver**.
2. Expose the cached value through the normal ADC API so
   `adc_ref_internal()` / `adc_raw_to_millivolts_dt()` work without app-side
   compensation.
3. Allow the application (or another subsystem) to **override** the cached mV at
   runtime (external reference / calibration table).
4. Keep other vendors’ drivers unchanged unless they opt in.
5. Key optional driver get/set by `enum adc_reference` (emul-shaped) so other
   rails can opt in later without another API break — while keeping the **public**
   v1 surface INTERNAL-focused.

**Non-goals (this design):**

- Selecting VREFINT as a hardware ADC reference mux (STM32 does not work that way).
- Coordinated dual/interleaved multi-ADC hardware modes.
- Changing how non-internal refs use channel DT `zephyr,vref-mv` (DT helpers still
  use `spec->vref_mv` for non-`ADC_REF_INTERNAL`).
- Public `adc_vref_get()` / `adc_vref_set()` wrappers in v1 (driver ops are
  general; public shorthands stay INTERNAL-only).
- Channel-id in the get/set API (per-channel scales of the same enum remain out
  of scope; see §12 / RFC discussion).
- Removing or replacing the `st,stm32-vref` sensor in the first slice (it may
  later share logic/cache).

## 2. Background

### 2.1 Zephyr ADC API shape

Each ADC controller is a `struct device`. The driver API is:

```c
struct adc_driver_api {
	adc_api_channel_setup channel_setup;
	adc_api_read          read;
	/* optional: read_async, submit, get_decoder */
	uint16_t ref_internal;	/* mV — today: compile-time constant */
};
```

`adc_ref_internal(dev)` returns `api->ref_internal`.

`adc_raw_to_millivolts_dt()` chooses the scale as:

- `ADC_REF_INTERNAL` → `adc_ref_internal(spec->dev)`
- otherwise → `spec->vref_mv` from channel DT `zephyr,vref-mv`

There is **no** general runtime setter today (except emulator-only
`adc_emul_ref_voltage_set(dev, enum adc_reference, mv)`).

### 2.2 Nordic vs STM32

| | Nordic SAADC | STM32 ADC |
|---|---|---|
| `ADC_REF_INTERNAL` | True on-chip bandgap (~0.6 V) as conversion reference | Naming only: conversion is vs VREF+/VDDA |
| `ref_internal` | Fixed constant | Board DT `vref-mv` assumption |
| Factory cal for rail | N/A for that constant | VREFINT channel + `VREFINT_CAL` → compute VREF+ |

### 2.3 Existing `st,stm32-vref` sensor

Sensor client of the ADC: enables VREFINT path, reads raw, computes

\[
V_{\mathrm{REF+}} = \frac{vrefint\_cal\_mv \times VREFINT\_CAL}{VREFINT\_DATA}
\]

Does **not** update `adc_ref_internal()`. This design reuses the same formula and
DT (`vref:` node) inside `adc_stm32`.

### 2.4 Multi-ADC

Zephyr models multiple controllers as separate devices (`adc1`, `adc2`, …).
Sequences never span controllers. On STM32, VREF+ is typically one shared rail;
VREFINT is often only on one ADC. The design uses a **shared rail cache** with
per-device get/set entry points.

### 2.5 RFC feedback (channel vs enum)

Issue comment discussion on [#113971](https://github.com/zephyrproject-rtos/zephyr/issues/113971)
asked whether get/set should take a channel id because some ADCs may not share
one scale across channels. This design keys by `enum adc_reference` (mux / rail
select), matching `adc_emul_ref_voltage_set()`, **not** by channel id.

That answers “other reference rails” without pretending to solve true
per-channel scales of the same enum. Channel-scoped overrides stay future work
if a concrete in-tree need appears.

## 3. Decisions (locked)

| Topic | Choice |
|---|---|
| Refresh policy | Once at init; again when `sequence.calibrate` is set on the **VREFINT-owning** ADC |
| Common API | **Hybrid:** keep `ref_internal` as fallback; add optional `vref_get` / `vref_set` keyed by `enum adc_reference` |
| Public surface (v1) | `adc_ref_internal()` / `adc_ref_internal_set()` only; no public general `adc_vref_*` |
| v1 enum contract | Only `ADC_REF_INTERNAL` required where implemented; other enums → get `0`, set `-ENOTSUP` |
| Unsupported get | Return `0` (same “unavailable” convention as today’s empty internal ref) |
| Fallback ownership | When `vref_get` is present, **getter** returns DT `ref_internal` if cache invalid |
| DT helpers (v1) | Unchanged split: INTERNAL → `adc_ref_internal()`; else → `spec->vref_mv` |
| Measurement logic | Inside `adc_stm32.c` (not via sensor driver calls) |
| DT for cal/channel | Reuse existing SoC `st,stm32-vref` node |
| Multi-ADC cache | **Shared SoC/rail cache**; all STM32 ADC getters return it for INTERNAL |
| Setter scope | **Any** STM32 ADC device may set INTERNAL; updates shared cache (documented rail-global) |
| `calibrate` vs app set | Re-measure on VREFINT owner **overwrites** app-set value |
| Init measure failure | ADC still initializes; keep DT fallback / prior cache |

## 4. Architecture

```
Application / adc_raw_to_millivolts_dt() / sensors
        │
        ▼
adc_ref_internal(dev) / adc_ref_internal_set(dev, mv)
        │  (shorthands always pass ADC_REF_INTERNAL)
        ▼
adc_driver_api
  ref_internal     ← DT fallback (per instance)
  vref_get         ← optional (dev, enum adc_reference)
  vref_set         ← optional (dev, enum adc_reference, mv)
        │
        ▼  (STM32, feature on; INTERNAL only)
Shared rail cache  stm32_adc_vref_mv
        ▲
        │  seed / refresh
VREFINT-owning ADC: init + sequence.calibrate
  enable path → convert → formula → cache
```

Other vendors leave get/set NULL → behavior unchanged.
Non-internal DT conversion paths do **not** call `vref_get` in this slice.

## 5. Common API changes

**File:** `include/zephyr/drivers/adc.h`

```c
struct adc_driver_api {
	adc_api_channel_setup channel_setup;
	adc_api_read          read;
#ifdef CONFIG_ADC_ASYNC
	adc_api_read_async    read_async;
#endif
#ifdef CONFIG_ADC_STREAM
	adc_api_submit        submit;
	adc_api_get_decoder   get_decoder;
#endif
	uint16_t ref_internal; /* mV, static fallback */

	/**
	 * Optional: return the millivolt scale for @p reference.
	 * When NULL, adc_ref_internal() returns @c ref_internal.
	 * Unsupported @p reference: return 0.
	 * For ADC_REF_INTERNAL, when a live cache is invalid, return
	 * the device's DT ref_internal fallback (getter owns fallback).
	 */
	uint16_t (*vref_get)(const struct device *dev,
			     enum adc_reference reference);

	/**
	 * Optional: set the millivolt scale for @p reference used by
	 * conversion helpers. Does not reconfigure the hardware mux.
	 * When NULL, adc_ref_internal_set() returns -ENOTSUP.
	 *
	 * @retval 0 on success
	 * @retval -EINVAL if @p vref_mv is not acceptable (e.g. zero)
	 * @retval -ENOTSUP if @p reference is not supported by the driver
	 */
	int (*vref_set)(const struct device *dev,
			enum adc_reference reference,
			uint16_t vref_mv);
};

static inline uint16_t adc_ref_internal(const struct device *dev)
{
	const struct adc_driver_api *api = DEVICE_API_GET(adc, dev);

	if (api->vref_get != NULL) {
		return api->vref_get(dev, ADC_REF_INTERNAL);
	}
	return api->ref_internal;
}

static inline int adc_ref_internal_set(const struct device *dev,
				       uint16_t vref_mv)
{
	const struct adc_driver_api *api = DEVICE_API_GET(adc, dev);

	if (api->vref_set == NULL) {
		return -ENOTSUP;
	}
	return api->vref_set(dev, ADC_REF_INTERNAL, vref_mv);
}
```

**Compatibility:** Existing drivers zero-fill new pointer fields (or explicit
NULL). No change required to Nordic/etc. for this feature.

**Syscall note:** If `adc_ref_internal` remains inline userspace-readable via
existing patterns, `adc_ref_internal_set` should be a `__syscall` (or documented
supervisor-only) consistent with other mutable ADC controls. Implementation plan
must match Zephyr’s current userspace wrapping for ADC.

**Emulator:** `adc_emul_ref_voltage_set()` already takes `enum adc_reference`.
Host tests may implement `vref_get` / `vref_set` on emul and optionally make the
existing emul helper a thin wrapper; dual paths are acceptable for this slice.

## 6. STM32 driver design

**Primary file:** `drivers/adc/adc_stm32.c`  
**Kconfig:** `drivers/adc/Kconfig.stm32`  
**Bindings:** no new required ADC properties; consume `st,stm32-vref`

### 6.1 Kconfig

```
config ADC_STM32_VREFINT_CALIBRATE
	bool "Measure VREF+ from VREFINT at init/calibrate"
	depends on ADC_STM32
	depends on DT_HAS_ST_STM32_VREF_ENABLED
	default y
```

When enabled and the `vref:` node’s ADC is okay:

- Install `vref_get` / `vref_set` on **all** STM32 ADC instances.
- Perform VREFINT measurement from the ADC named in `vref:` `io-channels`.
- Only `ADC_REF_INTERNAL` is supported (maps to the shared VREF+ rail scale).

When disabled or no `vref:` node: pointers NULL; static `vref-mv` only; set →
`-ENOTSUP`.

Does **not** require `CONFIG_STM32_VREF` (sensor may remain disabled).

### 6.2 Devicetree inputs (from `st,stm32-vref`)

| Property | Role |
|---|---|
| `io-channels` | VREFINT-owning ADC + channel id |
| `nvmem-cells` / OTP mapping | `VREFINT_CAL` |
| `vrefint-cal-mv` | Factory VREF+ (mV) |
| `vrefint-cal-resolution` | Shift to 12-bit measurement |

Controller property `vref-mv`: per-instance **fallback** for `api.ref_internal`.
Fix today’s `DT_INST_PROP(0, vref_mv)` quirk so each instance uses its own
property for the static field (measured/set cache still shared via getters).

### 6.3 Shared cache

```c
static struct {
	struct k_mutex lock;
	uint16_t mv;
	bool valid; /* true after successful measure or set */
} stm32_adc_vref;
```

- **Get (`ADC_REF_INTERNAL`):** if `valid` → `mv`; else → that device’s
  `api.ref_internal` (DT). Other enums → `0`.
- **Set (`ADC_REF_INTERNAL`, any STM32 ADC):** reject `vref_mv == 0` with
  `-EINVAL`; else store and mark valid. Other enums → `-ENOTSUP`.
- **Measure (owner only):** on success update cache; on failure leave prior
  valid value, or remain invalid (DT fallback).

Document that the cache is **rail-global** across ADC instances.

### 6.4 Measurement procedure

Align with `stm32_vref` sensor (12-bit):

1. Ensure ADC ready / take runtime PM if applicable.
2. Setup VREFINT channel (`ADC_GAIN_1`, `ADC_REF_INTERNAL`, long acquisition).
3. Enable `LL_ADC_PATH_INTERNAL_VREFINT` (+ stabilization delay).
4. `adc_read` (internal helper or equivalent one-shot; avoid recursion deadlocks
   with `adc_context` — prefer a dedicated low-level conversion path used from
   init/calibrate, not a nested public `adc_read` from within an active sequence).
5. Disable VREFINT path when done (unless injected-mode policy says otherwise).
6. `mv = (cal_mv * (vrefint_cal >> shift)) / raw`.

**When:**

- Init of VREFINT-owning ADC (after HW offset calibration path as appropriate).
- `sequence.calibrate == true` on that owning ADC (in addition to existing
  offset/linearity calibration).
- Non-owner `calibrate`: existing HW cal only; no VREFINT refresh.

### 6.5 Stream / RTIO

Replace compile-time-only `STM32_ADC_VREF_MV` in stream headers with the live
value from the getter/cache so streamed frames match `adc_ref_internal()`.

### 6.6 Power management

Do **not** automatically re-measure on PM resume (preserves app `set`). Apps may
call calibrate or set again if the rail may have changed.

## 7. Error handling

| API / path | Condition | Behavior |
|---|---|---|
| `adc_ref_internal_set` | No `vref_set` | `-ENOTSUP` |
| `vref_set` | Unsupported `reference` | `-ENOTSUP` |
| `vref_set` / `adc_ref_internal_set` | `vref_mv == 0` | `-EINVAL` |
| `vref_set` INTERNAL | OK | `0`, cache updated |
| `vref_get` | Unsupported `reference` | `0` |
| Measure | raw 0 / ADC error / cal read fail | Log; cache unchanged if valid; else DT fallback; **ADC init still succeeds** |
| `adc_ref_internal` | `vref_get` present, cache valid | cached mV |
| `adc_ref_internal` | `vref_get` present, cache invalid | DT `ref_internal` (via getter) |
| `adc_ref_internal` | `vref_get` NULL | static `api.ref_internal` |
| Concurrency | get/set/measure | mutex around shared cache |

## 8. Testing

### 8.1 API / unit

- `vref_get` preferred over static field for INTERNAL; NULL getter → static.
- Setter NULL → `-ENOTSUP`; zero → `-EINVAL`; success visible via get.
- Unsupported enum: get → `0`; set → `-ENOTSUP`.
- `adc_raw_to_millivolts_dt` with `ADC_REF_INTERNAL` uses getter value.
- Non-internal DT path still uses `spec->vref_mv` (unchanged).

### 8.2 STM32 integration (HW or `platform_allow`)

1. After boot, `adc_ref_internal(adc)` ≈ `st,stm32-vref` sensor (± tolerance).
2. With two ADCs enabled, getters return the same value.
3. `set` on non-owner updates both getters and mV conversion.
4. `calibrate` on owner overwrites a prior `set`.
5. Feature off: no getter/setter; DT `vref-mv` only.

### 8.3 Build CI

- Build with feature on/off across representative STM32 series that have `vref:`.

### 8.4 Sample

- Small addition to ADC or `soc_voltage` sample: print DT fallback, measured
  `adc_ref_internal`, demonstrate optional set.

## 9. Documentation plan

How this feature should be documented in upstream Zephyr. Prefer extending
existing surfaces over inventing parallel guides. Binding HTML pages are
**generated from YAML** (`:dtcompatible:` links); there is no separate hand-written
`st,stm32-adc` RST page today.

### 9.1 Common ADC API (required)

| Artifact | Path / mechanism | What to add |
|---|---|---|
| Doxygen (primary) | `include/zephyr/drivers/adc.h` → group `adc_interface` | Full docs for `vref_get` / `vref_set`, updated `adc_ref_internal()`, new `adc_ref_internal_set()`. State: optional; NULL = legacy static; v1 public surface is INTERNAL shorthands; set rejects 0; unsupported enum get `0` / set `-ENOTSUP`; does not change mux enums; does not reconfigure hardware. |
| Peripheral overview | `doc/hardware/peripherals/adc.rst` | Today this is almost only a doxygen include. Add a short **Overview** subsection: static vs dynamic internal reference; point to `adc_ref_internal()` / `adc_ref_internal_set()`; clarify channel DT `zephyr,vref-mv` is for **non-internal** refs and is not routed through `vref_get` in this slice. |
| Emulator header | `include/zephyr/drivers/adc/adc_emul.h` | If emul implements the new ops, document relationship to `adc_emul_ref_voltage_set()` (wrapper vs dual path). |
| Release notes | `doc/releases/release-notes-<next>.rst` → **API Changes** | New APIs listed (Zephyr style: new APIs go here; behavioral notes may also need migration guide). |
| Migration guide | `doc/releases/migration-guide-<next>.rst` | Only if default driver behavior changes observed mV (e.g. STM32 measure-on-init). Explain: more accurate scale; apps that assumed exact DT `vref-mv` may see small deltas; how to disable feature via Kconfig. |

### 9.2 Samples (required for new API surface)

Contributor expectations: new API needs an example usage.

| Approach | Recommendation |
|---|---|
| **Extend existing** `samples/drivers/adc/adc_dt` | **Preferred minimum.** Already uses `adc_raw_to_millivolts_dt()`. Add optional printk of `adc_ref_internal(dev)` and, behind a sample Kconfig or `#ifdef`, a demo `adc_ref_internal_set()` + reconvert. Keeps one well-known entry point. |
| **Extend** `samples/drivers/adc/adc_sequence` | Already resolves internal vref via `adc_ref_internal()` into a local array. Document that with a getter-enabled driver this becomes live; optionally refresh after set/calibrate. |
| **New sample** `samples/drivers/adc/adc_ref_internal` (or similar) | Only if the demo needs multi-step narrative (measure → set → calibrate overwrite) that would clutter `adc_dt`. Register in `samples/drivers/adc/index.rst` via `zephyr:code-sample`. |
| **Cross-link** `samples/sensor/soc_voltage` | Keep as the sensor-facing VREF+/VBAT demo. README note: rail voltage from `st,stm32-vref` should agree with `adc_ref_internal()` when the ADC driver seeds from VREFINT; apps should prefer ADC API for conversion helpers, sensor for explicit voltage channels. Do **not** make soc_voltage the primary ADC API sample. |

Sample README requirements (American English, `:relevant-api: adc_interface`):

- When getters are unsupported, sample still runs (set returns `-ENOTSUP`).
- One overlay/board where dynamic ref is exercised (STM32 with `vref:` okay is fine as *an* example board, not the only story).

### 9.3 Devicetree / bindings documentation

| Binding | Change |
|---|---|
| Common channel binding `adc-controller.yaml` → `zephyr,vref-mv` | Optionally tighten description: still for non-internal refs; internal scale comes from `adc_ref_internal()`. |
| `st,stm32-adc` → `vref-mv` | Expand description: board nominal / **fallback** when VREFINT calibration is disabled or has not produced a cache yet; not a substitute for runtime measurement when `CONFIG_ADC_STM32_VREFINT_CALIBRATE` is enabled. |
| `st,stm32-vref` | Note that the ADC driver may consume the same node for cal/channel when calibrating internal ref; sensor remains independently usable. |
| Kconfig help | `CONFIG_ADC_STM32_VREFINT_CALIBRATE` (and any sample Kconfig) with clear help text — appears in `menuconfig` and generated Kconfig docs. |

No new hand-written STM32 “hardware guide” RST is required unless maintainers want a short note under an existing STM32 doc; bindings + Kconfig + ADC overview cover the hardware-facing story.

### 9.4 Driver-specific documentation (STM32 consumer)

Keep vendor detail out of the common API page; put it next to the driver:

- `drivers/adc/Kconfig.stm32` help for `ADC_STM32_VREFINT_CALIBRATE`
- Binding text above
- Optional brief comment block at top of the VREFINT helpers in `adc_stm32.c` pointing at RM “Temperature sensor and internal reference voltage” chapters in general terms (no single RM number for all series)

Document STM32 policy in Kconfig/binding prose:

- Shared rail cache across ADC instances
- Measure on VREFINT-owning ADC at init / `sequence.calibrate`
- `adc_ref_internal_set()` on any instance updates the shared cache
- Calibrate on owner overwrites a prior set
- Only `ADC_REF_INTERNAL` is supported via `vref_get` / `vref_set`

### 9.5 Tests as executable docs

- Emul/unit tests name the contracts (`get` prefers callback, unsupported enum,
  `set` → `-ENOTSUP`, etc.).
- Twister integration case README or test docstring: expected relationship between sensor VREF and `adc_ref_internal()` tolerance.

### 9.6 Documentation checklist (implementation PRs)

- [ ] Doxygen on all new/changed public symbols
- [ ] `doc/hardware/peripherals/adc.rst` overview blurb
- [ ] Release notes **API Changes** entry
- [ ] Migration guide entry if STM32 default mV behavior changes
- [ ] Binding YAML descriptions updated (`st,stm32-adc`, optionally `st,stm32-vref`, channel `zephyr,vref-mv`)
- [ ] Kconfig help complete
- [ ] Sample updated or added + `index.rst` / `zephyr:code-sample` metadata
- [ ] `soc_voltage` README cross-link (short)
- [ ] Build docs locally (`west build -b … -t html` or project doc build) and verify `:c:func:` / `:dtcompatible:` links

## 10. Files to touch (implementation)

| Area | Paths |
|---|---|
| Common API | `include/zephyr/drivers/adc.h` (+ syscall/header mirrors if required) |
| STM32 driver | `drivers/adc/adc_stm32.c`, `drivers/adc/Kconfig.stm32` |
| Docs (common) | `doc/hardware/peripherals/adc.rst`; release notes + migration guide |
| Docs (DT) | `dts/bindings/adc/st,stm32-adc.yaml`; `dts/bindings/sensor/st,stm32-vref.yaml`; optionally `adc-controller.yaml` |
| Tests | `tests/drivers/adc/…` and/or new STM32-focused case; `adc_emul` if extended |
| Sample | Prefer `samples/drivers/adc/adc_dt` (+ optional dedicated sample); cross-link `samples/sensor/soc_voltage` |

Emulator may optionally implement `vref_get` / `vref_set` for host tests (`adc_emul`).

## 11. Migration & compatibility

- Default `ADC_STM32_VREFINT_CALIBRATE=y` when `st,stm32-vref` is enabled changes
  millivolt results vs blind 3300 DT assumption — **more accurate**, may shift
  numbers slightly for apps that assumed exact 3300.
- Apps that already compensate with the sensor can drop double correction once
  they trust `adc_ref_internal()`.
- `st,stm32-vref` sensor remains available for direct voltage reporting.
- Document the behavior change in the migration guide (see §9.1).

## 12. Future work (out of this slice)

- Public `adc_vref_get()` / `adc_vref_set()` (or DT-spec shorthands) once a
  non-internal runtime use case and DT-vs-driver precedence story exist.
- Route non-internal DT conversions through `vref_get` only after that precedence
  is defined (including `ADC_REF_VDD_1*` fraction consistency).
- Channel-scoped scale API if an in-tree driver truly needs per-channel overrides
  of the same `enum adc_reference`.
- Shared helper used by both `adc_stm32` and `stm32_vref` sensor.
- Optional “sticky app set” bit so `calibrate` does not overwrite until cleared.
- Per-device external references if a product has truly independent VREF+ pins.
- Broader common-API adoption by other vendors with measurable internal refs.

## 13. Open implementation notes

- Avoid deadlock: VREFINT measure during init/calibrate must not re-enter
  `adc_context` in a way that takes the same lock twice; use an internal
  conversion helper.
- Userspace: confirm whether `adc_ref_internal_set` needs `__syscall` wrapping.
- Confirm series without `st,stm32-vref` (e.g. F1) remain on static `vref-mv`
  only via Kconfig `depends`.
- Keep the upstream RFC/PR draft text in sync before pasting further replies
  (this design revision intentionally did not edit that draft).
