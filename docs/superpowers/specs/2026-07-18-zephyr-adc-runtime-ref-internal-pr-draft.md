# DRAFT — Zephyr PR / RFC: Runtime-configurable ADC internal reference

> **Superseded.** The RFC was filed as
> [#113971](https://github.com/zephyrproject-rtos/zephyr/issues/113971). Part 1
> landed as [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439)
> (`adc_ref_get`, no public setter). Current design of record:
> `docs/superpowers/specs/2026-08-13-adc-ref-get-stm32-vrefint-design.md`.
> Keep this file for historical context only.

> **Status:** Historical draft (API names in this file are stale)  
> **Intended labels:** `RFC`, `area: ADC`, `API`  
> **Related design (implementation detail):**  
> Historical: `docs/superpowers/specs/2026-07-18-stm32-adc-vrefint-dynamic-ref-design.md`  
> Current: `docs/superpowers/specs/2026-08-13-adc-ref-get-stm32-vrefint-design.md`  
> **Upstream process notes:** Zephyr does not ship a GitHub `PULL_REQUEST_TEMPLATE.md`.
> Substantial API surface changes should follow the [RFC / Proposal][rfc-form]
> issue form and [Contributor Expectations][contrib-expect] (PR description with
> summary + rationale; docs/tests/samples for new API; prefer small incremental
> PRs after RFC consensus). This document is written to be pasted/adapted into
> an RFC issue and later into a draft PR description.

[rfc-form]: https://github.com/zephyrproject-rtos/zephyr/issues/new?template=003_rfc-proposal.yml
[contrib-expect]: https://docs.zephyrproject.org/latest/contribute/contributor_expectations.html

---

## Suggested titles

- **RFC issue:** `RFC: ADC: runtime get/set for internal reference voltage (mV)`
- **Implementation PR (after RFC):** `drivers: adc: add optional ref_internal get/set API`

---

## Problem Description

The Zephyr ADC API exposes a single millivolt scale for
`ADC_REF_INTERNAL` via `adc_driver_api.ref_internal`, returned by
`adc_ref_internal()`. That field is a **compile-time constant** baked into the
driver API struct.

That model fits hardware with a true fixed bandgap used as the conversion
reference. It is a poor fit when:

1. The “internal” reference voltage is really a **board or supply rail** whose
   value is only approximately known at build time (DT), or drifts at runtime.
2. The SoC can **measure** that rail more accurately using factory calibration
   data and an internal sense channel.
3. The application or another subsystem learns a better reference value at
   runtime (lab calibration, external precision reference, shared measurement
   from another core/service).

Today, accurate conversion in those cases requires **application-side
compensation**: read a sensor or cal table, then call
`adc_raw_to_millivolts(ref_mv, …)` with a hand-supplied scale, bypassing
`adc_raw_to_millivolts_dt()` / `adc_ref_internal()`. There is no supported way
for a driver to publish an updated internal-reference millivolt value, and no
way for an application to feed one back into the common helpers.

The emulator has `adc_emul_ref_voltage_set()`, which shows the need in tests,
but production drivers have no equivalent.

---

## Motivation / Use Cases

### Use case 1 — Supply-referenced ADCs with measurable rail

Many MCUs ADCs convert against VDDA / VREF+, not against a selectable bandgap.
The bandgap (or similar) is an **input channel** used to *infer* the rail
voltage via factory calibration. Boards often put `vref-mv = <3300>` (or
similar) in DT as a static guess. Battery-powered and unregulated designs need
the measured rail for correct `raw → mV` results without every app reimplementing
the same formula.

### Use case 2 — Application or system-provided calibration

A product may:

- Store a one-time calibration constant in NVS/OTP written at manufacturing.
- Receive a reference voltage from another subsystem (PMIC ADC, external
  meter, second MCU).
- Temporarily force a known scale during self-test.

The app should be able to install that millivolt value so existing code using
`adc_ref_internal()` / `adc_raw_to_millivolts_dt()` picks it up.

### Use case 3 — Keep DT helpers honest

`adc_raw_to_millivolts_dt()` already branches:

- `ADC_REF_INTERNAL` → `adc_ref_internal(dev)`
- other refs → channel DT `zephyr,vref-mv`

If internal reference can be dynamic, the DT helper path stays the right default
for applications; they should not have to abandon DT-based conversion.

### Use case 4 — Multi-instance ADC controllers

SoCs with multiple ADC blocks still usually share one analog reference rail.
The API remains per-`device`, but drivers may back getters/setters with a
shared cache so `adc_ref_internal(adc1)` and `adc_ref_internal(adc2)` stay
consistent when appropriate.

---

## Proposed Change (Summary)

Extend the ADC driver API with **optional** callbacks to get and set the
internal reference voltage in millivolts, and update `adc_ref_internal()` to
prefer the getter when present. Add `adc_ref_internal_set()`.

- **Additive / opt-in:** drivers that leave the new pointers `NULL` behave
  exactly as today.
- **Not a new reference enum:** this does not change mux selection
  (`ADC_REF_INTERNAL` vs `ADC_REF_VDD_1` / external). It only makes the
  **millivolt scale** associated with `ADC_REF_INTERNAL` dynamic when a driver
  supports it.
- **Minimum viable implementation:** common API + documentation + tests, plus
  at least one in-tree driver that implements get/set (and, where applicable,
  seeds the value from hardware measurement or DT fallback).

---

## Proposed API changes (normative)

> This section is the contract. Driver-specific measurement algorithms are
> **out of scope** for the API text; they belong in vendor drivers.

### Current API (unchanged fields)

```c
__subsystem struct adc_driver_api {
	adc_api_channel_setup channel_setup;
	adc_api_read          read;
	/* optional async / stream members … */
	uint16_t ref_internal;	/* mV, static */
};
```

```c
static inline uint16_t adc_ref_internal(const struct device *dev);
```

### Proposed additions to `struct adc_driver_api`

```c
	/**
	 * Optional: return the current internal reference voltage in mV.
	 * When NULL, adc_ref_internal() returns @c ref_internal.
	 */
	uint16_t (*ref_internal_get)(const struct device *dev);

	/**
	 * Optional: set the internal reference voltage in mV used for
	 * conversions via adc_ref_internal() / DT helpers.
	 * When NULL, adc_ref_internal_set() returns -ENOTSUP.
	 *
	 * @retval 0 on success
	 * @retval -EINVAL if @p vref_mv is not acceptable to the driver
	 * @retval -ENOTSUP if not implemented (wrapper handles NULL pointer)
	 */
	int (*ref_internal_set)(const struct device *dev, uint16_t vref_mv);
```

### Proposed public wrappers

```c
/**
 * @brief Get the internal reference voltage (mV).
 *
 * If the driver implements ref_internal_get(), return its result.
 * Otherwise return the static adc_driver_api.ref_internal value.
 */
static inline uint16_t adc_ref_internal(const struct device *dev);

/**
 * @brief Set the internal reference voltage (mV).
 *
 * Updates the value later returned by adc_ref_internal() for drivers that
 * implement ref_internal_set(). Does not by itself reconfigure analog muxes;
 * it updates the scale used for raw-to-voltage helpers.
 *
 * @retval 0 on success
 * @retval -ENOTSUP if the driver does not support setting the reference
 * @retval -EINVAL if @p vref_mv is rejected (e.g. zero)
 */
int adc_ref_internal_set(const struct device *dev, uint16_t vref_mv);
```

(`adc_ref_internal_set` should follow Zephyr userspace conventions for ADC,
e.g. `__syscall` if other mutable ADC entry points are wrapped.)

### Semantics

| Topic | Rule |
|---|---|
| Static `ref_internal` | Remains required fallback / default for drivers without a getter |
| Getter present | `adc_ref_internal()` must use it |
| Setter present | Updates whatever the getter returns (driver-defined storage) |
| `vref_mv == 0` on set | Reject with `-EINVAL` (zero is useless as a scale) |
| Channel DT `zephyr,vref-mv` | Unchanged; still used for **non-internal** references only |
| Hardware mux | Unchanged; `adc_channel_setup()` reference enum behavior unchanged |
| Multi-device | Per-device API; drivers may share backing storage across instances if the rail is shared — must document |

### Compatibility

- **In-tree / out-of-tree drivers:** rebuilt against new headers; new pointers
  default to `NULL` if omitted from designated initializers → no behavior change.
- **Applications:** existing callers of `adc_ref_internal()` automatically see
  dynamic values when a driver implements the getter.
- **API maturity:** additive extension to a stable subsystem API; document in
  release notes under API changes. Not a breaking change if pointers are
  appended and optional.

---

## Proposed Change (Detailed) — implementation sketch for reviewers

This is informational for the RFC; the first landing PR may be API-only + emul
+ tests, with vendor drivers following.

1. **`include/zephyr/drivers/adc.h`** — extend `adc_driver_api`; update
   `adc_ref_internal()`; add `adc_ref_internal_set()`; document thoroughly.
2. **Syscall / userspace** — mirror other ADC write-style APIs as required.
3. **`adc_emul`** — implement get/set (may unify with existing
   `adc_emul_ref_voltage_set()` concepts) for host tests.
4. **At least one real driver** — implement get/set; optionally seed from HW
   measurement or keep DT fallback until set/measured.
5. **Tests** — behavior contracts for NULL vs non-NULL ops; DT millivolt helper
   uses getter when `ADC_REF_INTERNAL`.
6. **Sample snippet** — show get after boot / set then convert.
7. **Docs + release notes** — ADC API page; “API Changes” entry.

Vendor drivers that can measure their rail (or accept runtime calibration) are
encouraged to opt in over time. Drivers with a truly fixed bandgap can keep
pointers NULL forever.

---

## Alternatives Considered (rejected ideas)

### 1. Application-only compensation (status quo)

Apps call `adc_raw_to_millivolts(my_ref_mv, …)` after private measurement.

- **Rejected as the only solution:** works, but fragments the ecosystem; DT
  helpers and sensors that call `adc_ref_internal()` stay wrong; every product
  reimplements the same glue.

### 2. Replace `uint16_t ref_internal` with a mandatory getter

```c
uint16_t (*ref_internal)(const struct device *dev);
```

- **Rejected for this change:** forces a treewide driver migration for no
  functional gain on fixed-bandgap devices. Hybrid optional pointers are
  additive.

### 3. Mutable RAM `adc_driver_api` and patch `ref_internal` in place

- **Rejected:** `DEVICE_API()` objects are `const`; writing them is undefined /
  faults on XIP. Bypassing `DEVICE_API()` breaks `DEVICE_API_IS()`. A getter
  reading driver `data` is the clean pattern.

### 4. New channel reference enum (e.g. `ADC_REF_MEASURED`)

- **Rejected:** conflates mux selection with scale provenance. The hardware
  reference selection is unchanged; only the mV metadata is dynamic.

### 5. Put millivolts only in DT / overlay at build time

- **Rejected as sufficient:** cannot express runtime measurement or field
  calibration.

### 6. Sensor-only reporting of VREF (no ADC API change)

Some platforms already expose a voltage sensor for the rail.

- **Rejected as sufficient:** useful, but does not fix
  `adc_raw_to_millivolts_dt()` / generic ADC consumers. The ADC API remains the
  place conversion helpers look for `ADC_REF_INTERNAL`.

### 7. Common API setter that always succeeds by writing a framework-owned map

- **Rejected:** reference semantics and validity checks are driver-/platform-
  specific (shared rail vs per-instance, allowed ranges). Optional driver
  callbacks keep policy in the driver.

---

## Dependencies / Impact

| Area | Impact |
|---|---|
| ADC subsystem | Header + docs + tests; optional syscall |
| Existing ADC drivers | None required; NULL pointers |
| Out-of-tree drivers | Rebuild; no source change if using designated initializers |
| Applications | Optional use of `adc_ref_internal_set()`; automatic benefit when drivers implement getter |
| Devicetree | No mandatory binding change for the common API |
| Architecture WG | Review recommended (stable ADC API additive extension) |

---

## Concerns and Unresolved Questions

1. **Userspace:** exact `__syscall` wrapping for `adc_ref_internal_set`.
2. **Struct layout:** confirm appending two function pointers to
   `adc_driver_api` is acceptable for Zephyr’s device-API versioning practice
   (drivers are compiled with the tree; no stable binary driver ABI across
   major versions in the usual sense).
3. **Interaction with `sequence.calibrate`:** left to drivers whether hardware
   calibration also refreshes the cached mV; must be documented per driver.
4. **Whether `adc_emul_ref_voltage_set()` should become a thin wrapper** around
   the new API for consistency.

---

## Test Strategy

- Unit / emul: getter vs static fallback; set success/`-ENOTSUP`/`-EINVAL`;
  `adc_raw_to_millivolts_dt` observes getter.
- Driver tests (platform-specific PRs): measured or set value visible via
  `adc_ref_internal()`; multi-instance consistency where the driver claims a
  shared rail.
- Sample: demonstrate set + convert on a supported board.

---

## Documentation plan

Zephyr documents ADC mainly through **doxygen** (`adc_interface`), a thin
peripheral page, **generated DT binding** pages, samples, and release/migration
notes. There is no separate hand-written per-SoC ADC guide for STM32 today.

### Required (common API PR)

1. **Doxygen** in `include/zephyr/drivers/adc.h` for the new/changed symbols
   (primary API reference; pulled into `doc/hardware/peripherals/adc.rst`).
2. **Short overview** in `doc/hardware/peripherals/adc.rst`: static vs optional
   dynamic internal reference; point apps at `adc_ref_internal()` /
   `adc_ref_internal_set()`; remind that channel `zephyr,vref-mv` is for
   non-internal references.
3. **Release notes** → *API Changes*: list new APIs.
4. **Sample usage** (contributor expectation for new API):
   - Prefer extending `samples/drivers/adc/adc_dt` (already converts via
     `adc_raw_to_millivolts_dt()`): print `adc_ref_internal()`, optionally demo
     `adc_ref_internal_set()`.
   - Add a dedicated `samples/drivers/adc/…` sample only if the story needs
     more steps than `adc_dt` should carry.
5. **Emulator docs** if `adc_emul` gains get/set (relate to existing
   `adc_emul_ref_voltage_set()`).

### Driver / hardware follow-on PRs (example: STM32)

6. **Binding YAML descriptions** (become `:dtcompatible:` docs):
   - `st,stm32-adc` `vref-mv`: clarify **fallback / nominal board** role when
     runtime calibration is enabled.
   - `st,stm32-vref`: note ADC may consume the same node for cal data.
7. **Kconfig help** for `CONFIG_ADC_STM32_VREFINT_CALIBRATE` (shared-rail
   cache, init/`calibrate`, setter policy).
8. **Migration guide** entry if default measured VREF+ changes observed mV vs
   blind DT `vref-mv`.
9. **Cross-link** `samples/sensor/soc_voltage` README: sensor reading should
   agree with `adc_ref_internal()`; conversion helpers should use the ADC API.

### Not required

- A new STM32-only hardware RST book chapter (bindings + Kconfig + ADC overview
  are enough unless maintainers request more).

---

## Suggested PR split (after RFC consensus)

Per contributor expectations (small, reviewable PRs):

1. **PR A — API + docs + emul + tests** (no vendor policy beyond emul)
2. **PR B — Driver X** implements get/set (+ optional HW seed)
3. **PR C — Sample / further drivers**

This draft describes the full scope; landing can be incremental.

---

## Draft PR description (paste-ready sketch)

```markdown
## Summary

This change extends the ADC API so drivers may optionally report and accept a
runtime internal-reference voltage in millivolts. `adc_ref_internal()` prefers
an optional getter; `adc_ref_internal_set()` updates that value when supported.
Drivers that do not implement the new callbacks retain today’s static
`ref_internal` behavior.

## Motivation

`ref_internal` is currently a compile-time constant. That is insufficient when
the effective ADC reference rail is measured or calibrated at runtime, and it
pushes compensation into every application. See RFC: <link>.

## Proposed API

- `adc_driver_api.ref_internal_get` (optional)
- `adc_driver_api.ref_internal_set` (optional)
- `adc_ref_internal()` — use getter when non-NULL
- `adc_ref_internal_set()` — `-ENOTSUP` if unsupported

## Testing

- Emulator / unit coverage for get/set contracts
- <driver / board tests as applicable>

## Documentation

- Doxygen + `doc/hardware/peripherals/adc.rst` overview
- Release notes API Changes; migration guide if driver defaults change mV
- Sample: extend `samples/drivers/adc/adc_dt` (or dedicated sample)
- Driver PRs: binding/Kconfig text as needed

## RFC

Link: https://github.com/zephyrproject-rtos/zephyr/issues/<RFC number>

## Notes

- Additive, opt-in; not a breaking API change
- Does not change channel reference mux enums or `zephyr,vref-mv` for
  non-internal references
```

---

## Checklist before opening upstream

- [ ] File RFC issue using the Zephyr RFC form; paste Problem / Summary /
      Detailed / Alternatives / Dependencies / Concerns from this doc
- [ ] Discuss in Architecture / ADC maintainer channels if needed
- [ ] Open draft PR linking the RFC; keep PR focused (API-first split)
- [ ] DCO `Signed-off-by` on all commits; non-empty commit bodies
- [ ] Docs: doxygen, adc.rst overview, release-notes API Changes
- [ ] Migration guide if a driver changes default observed mV
- [ ] Tests + at least one implementing driver + sample usage
- [ ] Binding/Kconfig docs in driver follow-on PRs
