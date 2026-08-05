# ADC Optional `ref_internal_get` — Design

- **Date:** 2026-08-05
- **Status:** Approved design (pre-implementation)
- **Scope:** Thin Zephyr ADC common API so drivers can publish a runtime
  INTERNAL millivolt scale without new public setter/getter symbols; reshape
  Part 1 on the current branch; prepare Part 2 STM32 measure/cache.
- **Upstream target:** `zephyrproject-rtos/zephyr`
- **Related:** RFC [#113971](https://github.com/zephyrproject-rtos/zephyr/issues/113971);
  supersedes the **common-API** portions of
  `2026-07-18-stm32-adc-vrefint-dynamic-ref-design.md` (enum-keyed `vref_get` /
  `vref_set` and public `adc_ref_internal_set`). STM32 measurement mechanics in
  that older doc remain useful background for Part 2, but Part 2 must drop the
  public setter and install `ref_internal_get` instead of `vref_*`.

## 1. Purpose & goals

Maintainer feedback on [#113971](https://github.com/zephyrproject-rtos/zephyr/issues/113971)
rejected a public `adc_ref_internal_set()` as the first API: the ADC driver
should own measured / calibrated INTERNAL mV and expose it through the existing
`adc_ref_internal()` path so `adc_raw_to_millivolts_dt()` users benefit without
app changes. External precision references belong in a later provider /
`reference-supplies` model, not an open setter.

**Goals:**

1. Optional driver op so `adc_ref_internal()` can return a live value.
2. No new public ADC symbols and no syscall for set.
3. Emul + host tests prove the dynamic path without a public setter.
4. Keep Part 1 (common API) and Part 2 (STM32 calibrate) as stacked PRs.
5. Reshape the existing Part 1 commits on the current branch; do not force-push
   the remote (reconcile remote history later).

**Non-goals:**

- Public `adc_ref_internal_set()` / general `adc_vref_*`.
- Enum-keyed `vref_get` / `vref_set`.
- Channel-id in the get API.
- Provider / regulator / BQ76925 wiring.
- Routing non-internal channel DT refs through the new op.
- Removing the `st,stm32-vref` sensor in Part 2’s first slice.

## 2. Decisions (locked)

| Topic | Choice |
|---|---|
| Public surface | Signature-stable: only existing `adc_ref_internal()`; may become dynamic |
| Driver op | Optional INTERNAL-only `ref_internal_get(dev)` |
| Setter in ADC API | None (public or driver-api) |
| Static `ref_internal` | Remains mandatory fallback when getter is NULL |
| Fallback when getter present | Getter returns DT/`api->ref_internal` if cache invalid |
| DT helpers | Unchanged: INTERNAL → `adc_ref_internal()`; else → `spec->vref_mv` |
| Emul mutation | Existing `adc_emul_ref_voltage_set()` only |
| Delivery | Part 1 API+emul+tests+docs; Part 2 STM32 stacked |
| Branch process | Fold into existing Part 1 commits locally; no force-push |

## 3. Minimal API surface

### 3.1 Public

```c
static inline uint16_t adc_ref_internal(const struct device *dev)
{
	const struct adc_driver_api *api = DEVICE_API_GET(adc, dev);

	if (api->ref_internal_get != NULL) {
		return api->ref_internal_get(dev);
	}

	return api->ref_internal;
}
```

No `adc_ref_internal_set`. Document that the return value may change over time
when the driver implements `ref_internal_get`.

### 3.2 Driver API

```c
struct adc_driver_api {
	/* … existing members … */
	uint16_t ref_internal; /* mV, static fallback; 0 = unsupported */

	/**
	 * @driver_ops_optional
	 * Return the current INTERNAL reference scale in millivolts.
	 * When NULL, adc_ref_internal() returns @c ref_internal.
	 * When implemented, return the live cache if valid, otherwise the
	 * instance DT / @c ref_internal fallback.
	 */
	uint16_t (*ref_internal_get)(const struct device *dev);
};
```

Remove from the current Part 1 branch: `adc_api_vref_get` / `adc_api_vref_set`,
`vref_get`, `vref_set`, `adc_ref_internal_set`, and its syscall/handler.

### 3.3 Ownership

- Driver (or emul) owns mutable cache in device data (STM32: shared SoC/rail
  cache is fine; document rail-global behavior).
- Only the driver updates the cache (init measure, `sequence.calibrate`, etc.).
- Applications and other subsystems do not push mV into the ADC API in this
  design.

## 4. Architecture

```
Application / adc_raw_to_millivolts_dt() / sensors
        │
        ▼
adc_ref_internal(dev)          /* public, unchanged signature */
        │
        ├─ ref_internal_get == NULL  → api->ref_internal
        └─ ref_internal_get != NULL  → driver getter
                │
                ▼
         driver/emul cache (dev->data or shared rail)
                ▲
                │  write only inside driver
         measure / calibrate / emul_ref_voltage_set (tests)
```

## 5. Part 1 — common API + emul

| Path | Change |
|---|---|
| `include/zephyr/drivers/adc.h` | Add optional `ref_internal_get`; update `adc_ref_internal()`; remove set/enum ops |
| `drivers/adc/adc_handlers.c` | Remove `adc_ref_internal_set` verify |
| `drivers/adc/adc_emul.c` | Install `ref_internal_get` over existing INTERNAL storage |
| `include/zephyr/drivers/adc/adc_emul.h` | Note that `adc_emul_ref_voltage_set` updates what `adc_ref_internal` observes |
| `tests/drivers/adc/adc_emul/` | Drive via `adc_emul_ref_voltage_set`; drop public-set tests |
| `doc/hardware/peripherals/adc.rst` | Dynamic INTERNAL via optional getter; driver-owned updates |
| `doc/releases/release-notes-*.rst` | Behavior note, not a new public symbol |
| `samples/drivers/adc/adc_dt/` | Optional printk of `adc_ref_internal()`; no set demo |

**Tests:** After `adc_emul_ref_voltage_set(dev, ADC_REF_INTERNAL, distinctive_mv)`,
`adc_ref_internal(dev)` and INTERNAL-channel `adc_raw_to_millivolts_dt()` observe
that value; restore afterward.

**Branch:** Fold these edits into the existing Part 1 commits on
`cursor/adc-vref-runtime-api-c2e3` (or successor). Do **not** force-push the
remote; leave remote reconciliation to the human (new PR branch or later push
strategy).

## 6. Part 2 — STM32 (stacked; API wiring only here)

Part 2 implementation plan remains separate, but must align as follows:

- Kconfig-gated measure of VREF+ from VREFINT using `st,stm32-vref` DT/formula
  inside `adc_stm32` (no sensor-driver calls at init).
- Shared rail cache; all STM32 ADC instances install the same
  `ref_internal_get` when the feature is on.
- Init measure + `sequence.calibrate` on the VREFINT-owning ADC write the cache.
- Measure failure: keep DT fallback; ADC init still succeeds.
- **Remove** any `vref_set` / `adc_ref_internal_set` from Part 2 Kconfig help,
  docs, and migration notes.
- External provider / `reference-supplies` remains future work.

| Case | Behavior |
|---|---|
| Getter NULL | Static `api->ref_internal` |
| Cache invalid / measure failed | Getter returns DT fallback; init succeeds |
| Getter returns 0 | Same as today: no internal ref info |
| Multi-ADC | Shared cache visible to all instance getters |
| `sequence.calibrate` | Owner re-measure overwrites cache |

## 7. Spec coverage vs prior docs

| Prior item | This design |
|---|---|
| Public `adc_ref_internal_set` | Dropped |
| Enum-keyed `vref_get` / `vref_set` | Replaced by INTERNAL-only `ref_internal_get` |
| Emul implements ops | Getter only; mutate via `adc_emul_ref_voltage_set` |
| STM32 measure/cache | Still Part 2; no public override |
| Provider model (BQ76925) | Explicitly deferred |

## 8. Success criteria

- Part 1: no new public ADC symbols; `adc_ref_internal()` uses optional getter;
  emul twister on `native_sim` passes; docs describe driver-owned dynamic scale.
- Part 2 (later): STM32 feature on → measured mV appears via existing helpers
  without app-side compensation; feature off / no `st,stm32-vref` → DT fallback.
