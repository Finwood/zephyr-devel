# ADC `ref_get` + STM32 VREFINT Calibrate — Design

- **Date:** 2026-08-13
- **Status:** Part 1 API landed (upstream PR
  [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439), approved,
  pending merge). Part 2 STM32 driver: approved design, pre-implementation.
- **Scope:** Record the **landed** ADC `adc_ref_get()` contract, and the STM32
  driver change that consumes it (measure VREF+ from VREFINT, cache, expose).
- **Upstream target:** `zephyrproject-rtos/zephyr`
- **Related:** RFC [#113971](https://github.com/zephyrproject-rtos/zephyr/issues/113971);
  API PR [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439)

**Supersedes (do not execute / do not treat as API-normative):**

- `docs/superpowers/specs/2026-07-18-stm32-adc-vrefint-dynamic-ref-design.md`
  (enum-keyed `vref_get`/`vref_set` + public `adc_ref_internal_set()`)
- `docs/superpowers/specs/2026-07-18-zephyr-adc-runtime-ref-internal-pr-draft.md`
- `docs/superpowers/plans/2026-08-01-adc-vref-runtime-api.md` (Part 1; done via
  [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439))
- `docs/superpowers/plans/2026-08-02-adc-stm32-vrefint-calibrate.md` (Part 2
  wired to the old get/set API)

An intermediate getter-only `ref_internal_get(dev)` reshape exists only on
`zephyr-devel` branch `cursor/pin-adc-vref-runtime-api-c2e3` (2026-08-05). That
shape was **not** what merged in review; do not revive it. This document is the
design of record for Part 2.

**Implementation plan:** `docs/superpowers/plans/2026-08-13-adc-stm32-vrefint-calibrate.md`

---

## 1. Purpose

STM32 ADCs convert against **VREF+/VDDA**, not against the factory bandgap.
Zephyr’s STM32 ADC driver exposes that scale as a static DT `vref-mv` (default
3300) via a single shared `DEVICE_API` that always uses **instance 0’s**
property. Apps that need accurate millivolt conversion compensate in user code
(typically the `st,stm32-vref` sensor).

Part 1 (landed) lets a driver publish a **runtime** millivolt scale through
`adc_ref_get()` / `adc_ref_internal()`, so
`adc_raw_to_millivolts_dt()` picks it up without app-side compensation.

Part 2 (this design) is the first in-tree consumer: `adc_stm32` measures VREF+
from VREFINT + factory `VREFINT_CAL`, caches it SoC-wide, and implements
`ref_get` for `ADC_REF_INTERNAL`.

### Goals

1. Measure VREF+ from VREFINT inside `adc_stm32` (not via sensor-driver calls).
2. Expose the cache through `adc_ref_get()` / `adc_ref_internal()` so existing
   DT conversion helpers become accurate.
3. Keep other vendors unchanged (`ref_get` NULL).
4. No public or driver-API setter. The driver owns the cache.

### Non-goals

- Public `adc_ref_internal_set()` / `adc_vref_set()`.
- Channel-id in the get API.
- Provider / regulator / `reference-supplies` / BQ76925 wiring (separate from
  VREFINT; see RFC).
- Removing or replacing the `st,stm32-vref` sensor.
- Auto re-measure on PM resume.
- Implementing `ref_get` for non-`ADC_REF_INTERNAL` enums on STM32.

---

## 2. How this differs from prior docs and the RFC

| Topic | Original RFC / Jul–Aug plans | After RFC + PR review | This design |
|---|---|---|---|
| Public setter | `adc_ref_internal_set()` | Rejected as first API (`ZhaoxiangJin`, 3 Aug) | None |
| Driver op | INTERNAL-only get, then enum `vref_get`/`vref_set` | Enum-keyed **get** with errno out-pointer | Landed `adc_api_ref_get` |
| DT helpers | INTERNAL → `adc_ref_internal()`; else `spec->vref_mv` | Prefer `adc_ref_get()` for **any** channel ref; DT fallback on failure | Match landed header |
| STM32 measure | Inside `adc_stm32`, cache, init + `sequence.calibrate` | Confirmed 7 Aug (`ZhaoxiangJin`); not a provider | Unchanged mechanics |
| External AFE (BQ76925) | Same setter | Separate provider/`reference-supplies` later | Out of scope |
| Channel-id argument | Requested (`maass-hamburg`) | Not adopted; `enum adc_reference` instead | No channel-id |

STM32 mechanics from the 18 Jul spec (§§6–8: Kconfig, shared cache, formula,
owner-only measure, no PM refresh, per-instance `vref-mv` quirk) remain the
intent. Only the **API wiring** changed: install `ref_get`, never `vref_set`.

---

## 3. Part 1 — landed API (normative for Part 2)

Do **not** re-implement Part 1. Consume this contract from
`include/zephyr/drivers/adc.h` after [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439)
merges (or from Finwood branch `cursor/adc-vref-runtime-api-c2e3` until then).

```c
typedef int (*adc_api_ref_get)(const struct device *dev, enum adc_reference ref,
			       uint16_t *vref_mv);

/* in struct adc_driver_api, after ref_internal: */
adc_api_ref_get ref_get; /* optional */

int adc_ref_get(const struct device *dev, enum adc_reference ref,
		uint16_t *vref_mv);
uint16_t adc_ref_internal(const struct device *dev); /* wrapper */
```

### Semantics

| Case | Result |
|---|---|
| `vref_mv == NULL` | `-EINVAL` (wrapper) |
| `ref_get == NULL`, `ADC_REF_INTERNAL` | `*vref_mv = api->ref_internal`; `0` or `-ENODATA` if 0 |
| `ref_get == NULL`, other enum | `-ENOTSUP` |
| `ref_get` implemented | Driver callback; `0` / `-ENOTSUP` / `-ENODATA` |
| `adc_ref_internal()` | `adc_ref_get(..., ADC_REF_INTERNAL, ...)`; `0` if get fails |

`adc_raw_to_x_dt_chan()` calls `adc_ref_get(spec->dev, channel_cfg->reference, …)`
and uses `spec->vref_mv` only when that call fails. STM32 therefore only needs
to succeed for `ADC_REF_INTERNAL`; other enums should return `-ENOTSUP` so DT
channel millivolts still apply.

No setter. Tests mutate emul via `adc_emul_ref_voltage_set()`.

---

## 4. Part 2 — STM32 driver

**Primary file:** `drivers/adc/adc_stm32.c`  
**Kconfig:** `drivers/adc/Kconfig.stm32`  
**Bindings:** no new required ADC properties; consume `st,stm32-vref`

### 4.1 Kconfig

```
config ADC_STM32_VREFINT_CALIBRATE
	bool "Measure VREF+ from VREFINT at init/calibrate"
	depends on ADC_STM32
	depends on DT_HAS_ST_STM32_VREF_ENABLED
	default y
```

When enabled:

- Install `.ref_get = adc_stm32_ref_get` on **every** STM32 ADC instance.
- Measure from the ADC named in `vref:` `io-channels` at that instance’s init
  and when `sequence.calibrate` is set on that instance.
- Do **not** `select` / depend on `CONFIG_STM32_VREF` (sensor may stay off).

When disabled or no okay `st,stm32-vref` node: `ref_get` stays NULL; static
per-instance `vref-mv` only.

Optional sibling Kconfig (do not reuse `STM32_VREF_READ_CALIB_VIA_NVMEM`, which
sits under `if STM32_VREF`):

```
config ADC_STM32_VREFINT_CALIB_VIA_NVMEM
	bool
	default y
	depends on ADC_STM32_VREFINT_CALIBRATE && OTP && NVMEM
```

Mirror the sensor’s nvmem vs OTP-pointer read. Do not require the sensor.

### 4.2 Devicetree

From the existing SoC `st,stm32-vref` node (same properties as the sensor):

| Property | Role |
|---|---|
| `io-channels` | VREFINT-owning ADC + channel id |
| `nvmem-cells` | `VREFINT_CAL` |
| `vrefint-cal-mv` | Factory VREF+ (mV) |
| `vrefint-cal-resolution` | Shift to 12-bit measurement |

Controller `vref-mv`: optional, default 3300; **per-instance fallback** for
`api.ref_internal` and for `ref_get` when the cache is invalid.

Access the vref node from `adc_stm32.c` without changing `DT_DRV_COMPAT`, e.g.
`DT_INST(0, st_stm32_vref)` (one node per SoC).

**Breaking (document in migration guide):** today all instances share
`DT_INST_PROP(0, vref_mv)` via one `DEVICE_API`. After this change each
instance’s `.ref_internal` (and DT fallback in the getter) uses **that**
instance’s `vref-mv`. Boards that omit the property or use the same value
everywhere are unaffected.

### 4.3 Shared cache + `ref_get`

```c
static struct {
	struct k_mutex lock;
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

	k_mutex_lock(&stm32_adc_vref.lock, K_FOREVER);
	*vref_mv = stm32_adc_vref.valid ? stm32_adc_vref.mv : cfg->vref_mv;
	k_mutex_unlock(&stm32_adc_vref.lock);

	return (*vref_mv == 0U) ? -ENODATA : 0;
}
```

- Mutex: `K_MUTEX_DEFINE` (or owner-init once).
- Document the cache as **rail-global** across ADC instances.
- Invalid cache → DT fallback (`0`, not `-ENODATA`) unless that DT value is 0.
- Split the shared `api_stm32_driver_api` into **per-instance** `DEVICE_API`
  so `.ref_internal = DT_INST_PROP(index, vref_mv)` and optional `.ref_get`.

Store `vref_mv` in `struct adc_stm32_cfg` from `DT_INST_PROP(n, vref_mv)`.

### 4.4 Measurement

Align with `drivers/sensor/st/stm32_vref/stm32_vref.c` (12-bit):

\[
V_{\mathrm{REF+}} = \frac{\texttt{vrefint\_cal\_mv} \times (\texttt{VREFINT\_CAL} \gg \texttt{shift})}{\texttt{raw}}
\]

Procedure:

1. Identify owner: `dev == DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(vref_node))`.
2. Runtime PM get if applicable.
3. Enable `LL_ADC_PATH_INTERNAL_VREFINT` + stabilization delay.
4. Dedicated **lock-free** one-shot conversion (12-bit, VREFINT channel, long
   acquisition). Do **not** call `adc_channel_setup()` or public `adc_read()`:
   both take `adc_context` (`channel_setup` locks; `start_read` already holds
   the lock during `sequence.calibrate`).
5. Disable VREFINT path when done unless injected-mode policy requires otherwise.
6. On success: lock cache, store mV, `valid = true`. On failure: log; leave
   prior valid value or remain invalid (DT fallback). **ADC init still succeeds.**

**When:**

- Init of the VREFINT-owning ADC, after existing HW offset/linearity cal
  (`adc_stm32_calibrate(dev, true)` in `adc_stm32_init`).
- `sequence.calibrate == true` on that owning ADC, after existing HW cal in
  `start_read()`. Then **re-apply** `set_resolution()` + `set_sequencer()` so
  the user sequence is not left configured as VREFINT-only.
- Non-owner `calibrate`: existing HW cal only.

`adc_stm32_channel_setup()` takes `adc_context_lock`; init’s context starts
locked (`ADC_CONTEXT_INIT_LOCK` / unlock at end of init). Measure must not
re-enter that lock.

### 4.5 Stream / RTIO

Replace `hdr->vref_mv = STM32_ADC_VREF_MV` with the live INTERNAL scale
(`adc_ref_internal(dev)` or a cache read) so streamed frames match the getter.

### 4.6 Power management

Do **not** automatically re-measure on PM resume (would surprise apps that
rely on a stable cache; they can set `sequence.calibrate` if the rail may have
changed).

---

## 5. Error handling

| Path | Condition | Behavior |
|---|---|---|
| `adc_ref_get` INTERNAL, cache valid | OK | `0`, cached mV |
| `adc_ref_get` INTERNAL, cache invalid | DT fallback > 0 | `0`, DT mV |
| `adc_ref_get` INTERNAL | DT/cache mV is 0 | `-ENODATA` |
| `adc_ref_get` other enum | STM32 | `-ENOTSUP` |
| Measure | raw 0 / ADC error / cal read fail | Log; cache unchanged if valid; else DT fallback; init succeeds |
| Concurrency | get / measure | mutex around shared cache |

---

## 6. Testing

### 6.1 Already covered (Part 1)

`tests/drivers/adc/adc_emul` on `native_sim` covers `adc_ref_get` contracts.
Part 2 does not add emul cases.

### 6.2 STM32 integration (HW)

New test under `tests/drivers/adc/` (e.g. `adc_stm32_vref/`) with
`platform_allow` for boards that enable `&vref` (e.g. `nucleo_l476rg`,
`nucleo_g071rb`, `disco_l475_iot1`):

1. After boot, `adc_ref_get(adc, ADC_REF_INTERNAL, &mv) == 0` and `mv` is
   plausible (non-zero, typically ~3000–3400 on 3.3 V boards).
2. `adc_ref_internal(adc)` matches that value.
3. `adc_ref_get(adc, ADC_REF_EXTERNAL0, &mv) == -ENOTSUP`.
4. If two ADCs enabled: both INTERNAL getters return the same cached value.
5. `sequence.calibrate` on the owner still returns INTERNAL successfully
   (refresh path does not fail the sequence).
6. Optional: with `CONFIG_STM32_VREF=y`, sensor voltage ≈ `adc_ref_internal()`
   within a stated tolerance (e.g. 50 mV). Skip if sensor node/alias absent.
7. Extra yaml case `CONFIG_ADC_STM32_VREFINT_CALIBRATE=n`: `ref_get` NULL;
   `adc_ref_internal()` equals that instance’s DT `vref-mv`.

Mark runtime vs build-only appropriately for CI. Skip gracefully when a second
ADC or the sensor is missing.

### 6.3 Build smoke

Build an ADC sample (e.g. `samples/drivers/adc/adc_dt`) for one board with
`vref:` okay, feature on and off.

---

## 7. Documentation (Part 2 PR)

| Artifact | What |
|---|---|
| `st,stm32-adc` `vref-mv` | Board nominal / **fallback** when calibrate is off or cache invalid |
| `st,stm32-vref` | ADC driver may consume the same node for INTERNAL cal; sensor remains usable |
| Kconfig help | Shared-rail cache; init / owner `sequence.calibrate`; no setter |
| `migration-guide-4.5.rst` (ADC section, keep-sorted) | (1) default measured INTERNAL mV; (2) per-instance `vref-mv` |
| Release notes | Driver note + pointer to migration guide |
| `samples/drivers/adc/adc_dt` | Print `adc_ref_internal()` (already useful; STM32 now live) |
| `samples/sensor/soc_voltage/README.rst` | Short cross-link: sensor ≈ `adc_ref_internal()` when feature on |

No new STM32 hardware RST chapter unless maintainers ask.

---

## 8. Migration & compatibility

1. **Measured INTERNAL scale (default on when `st,stm32-vref` is okay)**  
   `adc_ref_internal()` / INTERNAL `adc_raw_to_*_dt()` may differ from DT
   `vref-mv` / 3300 — usually more accurate. Restore old behavior with
   `CONFIG_ADC_STM32_VREFINT_CALIBRATE=n`.

2. **Per-instance `vref-mv`**  
   Multi-ADC boards with **divergent** per-instance `vref-mv` that relied on
   every instance reporting instance 0’s value must give each ADC the intended
   property (or omit it and keep the shared default 3300).

Apps that already compensate with `st,stm32-vref` should drop double
correction once they trust `adc_ref_internal()`. The sensor stays available
for explicit voltage channels.

---

## 9. Future work (out of this slice)

- Public setter or provider/`reference-supplies` for external AFE refs.
- Shared helper used by both `adc_stm32` and `stm32_vref` sensor.
- Optional sticky “do not overwrite cache on calibrate”.
- Other vendors opting into `ref_get`.

---

## 10. Open implementation notes

- Measure during init/`calibrate` must not re-enter `adc_context`.
- Series without `st,stm32-vref` (e.g. F1) stay on static `vref-mv` via Kconfig
  `depends`.
- ST review risk: `mathieuchopstm` preferred app-side scale on the RFC; the
  cache-at-init/`calibrate` approach was later endorsed by `ZhaoxiangJin`.
  Call out default-on measured mV and the lock-free conversion path in the
  upstream PR body.
- Stack Part 2 on a **`cursor/`-prefixed** branch from upstream `main` after
  [#115439](https://github.com/zephyrproject-rtos/zephyr/pull/115439) merges;
  until then, stack on `cursor/adc-vref-runtime-api-c2e3`. Cloud Agents need
  that prefix to access GitHub PRs; the descriptive suffix is not a hard name.
