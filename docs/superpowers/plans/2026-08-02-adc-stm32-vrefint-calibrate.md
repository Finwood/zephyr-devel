# STM32 ADC VREFINT Calibrate (Part 2) Implementation Plan

> **Superseded** by `docs/superpowers/plans/2026-08-13-adc-stm32-vrefint-calibrate.md`
> (spec `docs/superpowers/specs/2026-08-13-adc-ref-get-stm32-vrefint-design.md`).
>
> This plan still installs `vref_get`/`vref_set` and tests
> `adc_ref_internal_set()`. That API did not land. Do **not** execute.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On top of Part 1's ADC `vref_get` / `vref_set` API, make `adc_stm32` measure VREF+ from VREFINT, cache it SoC-wide, and expose it through `adc_ref_internal()` / `adc_ref_internal_set()`.

**Architecture:** Kconfig-gated feature reuses `st,stm32-vref` DT (channel, cal, shift) inside `adc_stm32` (no sensor-driver calls). Shared mutex-protected rail cache; all STM32 ADC instances install the same get/set ops; only the VREFINT-owning ADC measures at init and on `sequence.calibrate`. App set on any instance updates the cache; owner calibrate overwrites. Measure failure keeps DT fallback; ADC init still succeeds.

**Tech Stack:** `drivers/adc/adc_stm32.c`, STM32 LL ADC VREFINT path, existing `stm32_vref` sensor as formula reference, twister on STM32 `platform_allow` boards, west/`uv run`.

**Spec:** `docs/superpowers/specs/2026-07-18-stm32-adc-vrefint-dynamic-ref-design.md` (§§6–11).
**Depends on:** Part 1 branch/plan `docs/superpowers/plans/2026-08-01-adc-vref-runtime-api.md` (`adc-vref-runtime-api`).

## Global Constraints

- **Work tree:** Edit upstream code/tests/docs/samples only in `deps/zephyr/` (west-managed).
- **Branch (Part 2):** `adc-stm32-vrefint-calibrate`, created **from** Part 1 branch `adc-vref-runtime-api` (stacked). If Part 1 is not merged upstream yet, keep the stack local on the Finwood/west checkout.
- **Commits:** Do **not** use `git commit -s`, `--signoff`, or `Signed-off-by`. Human prepares DCO/upstream history later.
- **AI attribution:** Every commit in `deps/zephyr` that an AI agent helped author **must** include an `Assisted-by:` trailer per Zephyr’s [Usage disclosure and attribution](https://docs.zephyrproject.org/latest/contribute/guidelines.html#usage-disclosure-and-attribution):

  ```
  Assisted-by: [Agent Name]:[Model Version] [Tool1] [Tool2]
  ```

  - `[Agent Name]` — AI tool/framework (e.g. `Cursor`)
  - `[Model Version]` — specific model used for that commit (e.g. `grok-4.5`)
  - `[Tool1] [Tool2]` — optional specialized analysis tools only; do **not** list basic tools (git, gcc, make, editors, west, twister)
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
- **Formal tests & samples:** Live in `deps/zephyr`. Workspace repo is for interactive hacking/debugging only.
- **v1 STM32 contract:** Only `ADC_REF_INTERNAL` supported via `vref_*`; other enums → get `0`, set `-ENOTSUP`; set `0` → `-EINVAL`; getter returns DT `vref-mv` when cache invalid.
- **Out of scope:** Public `adc_vref_*`; non-internal DT routing; channel-id API; removing `st,stm32-vref` sensor; auto re-measure on PM resume.
- **Build:** `ZEPHYR_BASE=…/deps/zephyr`, `uv run west …`. Prefer a Nucleo (or similar) with `vref:` in SoC DT for integration tests.

---

## File map (Part 2)

| Path | Role |
|---|---|
| `deps/zephyr/drivers/adc/Kconfig.stm32` | `ADC_STM32_VREFINT_CALIBRATE` |
| `deps/zephyr/drivers/adc/adc_stm32.c` | Cache, measure, get/set, init/calibrate hooks, per-instance `vref-mv`, stream live vref |
| `deps/zephyr/dts/bindings/adc/st,stm32-adc.yaml` | Clarify `vref-mv` as fallback |
| `deps/zephyr/dts/bindings/sensor/st,stm32-vref.yaml` | Note ADC may consume node for cal |
| `deps/zephyr/tests/drivers/adc/…` (new or extended) | STM32 integration cases from spec §8.2 |
| `deps/zephyr/samples/drivers/adc/adc_dt/` and/or sequence | Exercise measured/set path on STM32 overlay |
| `deps/zephyr/samples/sensor/soc_voltage/README.rst` | Short cross-link |
| `deps/zephyr/doc/releases/migration-guide-4.5.rst` (or current) | Default mV may change |
| `deps/zephyr/doc/releases/release-notes-*.rst` | Driver/feature note if needed beyond Part 1 API entry |

**Reference implementation (read-only):** `deps/zephyr/drivers/sensor/st/stm32_vref/stm32_vref.c` for enable-path, cal read, and numerator/raw formula.

---

### Task 1: Stack branch on Part 1

**Files:** none (git only)

**Interfaces:**
- Consumes: local branch `adc-vref-runtime-api` with Part 1 complete
- Produces: branch `adc-stm32-vrefint-calibrate`

- [ ] **Step 1: Verify Part 1 is present**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git switch adc-vref-runtime-api
rg -n "vref_get|adc_ref_internal_set" include/zephyr/drivers/adc.h | head
```

Expected: symbols exist.

- [ ] **Step 2: Create Part 2 branch**

```bash
git switch -c adc-stm32-vrefint-calibrate
```

- [ ] **Step 3: Commit N/A**

---

### Task 2: Kconfig + shared cache + get/set stubs

**Files:**
- Modify: `deps/zephyr/drivers/adc/Kconfig.stm32`
- Modify: `deps/zephyr/drivers/adc/adc_stm32.c`
- Test: compile with feature on/off (later tasks)

**Interfaces:**
- Consumes: Part 1 `vref_get` / `vref_set`
- Produces: `CONFIG_ADC_STM32_VREFINT_CALIBRATE`; `stm32_adc_vref` cache; INTERNAL-only get/set when enabled

- [ ] **Step 1: Add Kconfig**

```
config ADC_STM32_VREFINT_CALIBRATE
	bool "Measure VREF+ from VREFINT at init/calibrate"
	depends on ADC_STM32
	depends on DT_HAS_ST_STM32_VREF_ENABLED
	default y
	help
	  Measure the ADC reference rail (VREF+/VDDA) using the VREFINT
	  channel and factory calibration described by the st,stm32-vref
	  node. The value is cached SoC-wide and returned by
	  adc_ref_internal() for all STM32 ADC instances. Applications may
	  override it with adc_ref_internal_set(). Re-measure when
	  sequence.calibrate is set on the VREFINT-owning ADC.
```

- [ ] **Step 2: Add shared cache + ops (behind `#ifdef CONFIG_ADC_STM32_VREFINT_CALIBRATE`)**

```c
static struct {
	struct k_mutex lock;
	uint16_t mv;
	bool valid;
} stm32_adc_vref;

static uint16_t adc_stm32_vref_get(const struct device *dev,
				   enum adc_reference reference)
{
	const struct adc_stm32_cfg *cfg = dev->config; /* use real cfg type name */

	if (reference != ADC_REF_INTERNAL) {
		return 0;
	}

	k_mutex_lock(&stm32_adc_vref.lock, K_FOREVER);
	uint16_t out = stm32_adc_vref.valid ? stm32_adc_vref.mv : cfg->vref_mv;
	k_mutex_unlock(&stm32_adc_vref.lock);

	return out;
}

static int adc_stm32_vref_set(const struct device *dev,
			      enum adc_reference reference,
			      uint16_t vref_mv)
{
	ARG_UNUSED(dev);

	if (reference != ADC_REF_INTERNAL) {
		return -ENOTSUP;
	}
	if (vref_mv == 0) {
		return -EINVAL;
	}

	k_mutex_lock(&stm32_adc_vref.lock, K_FOREVER);
	stm32_adc_vref.mv = vref_mv;
	stm32_adc_vref.valid = true;
	k_mutex_unlock(&stm32_adc_vref.lock);

	return 0;
}
```

Initialize the mutex once (`K_MUTEX_DEFINE` or owner init).

- [ ] **Step 3: Per-instance DT fallback + install ops when calibrate Kconfig is on**

Clarify: “feature on” means `CONFIG_ADC_STM32_VREFINT_CALIBRATE=y` (Step 1). That
symbol enables the **full** STM32 dynamic-ref path — install `vref_get`/`vref_set`,
measure at init, and refresh on owner `sequence.calibrate` — not only the
sequence flag.

Today `STM32_ADC_VREF_MV` is `DT_INST_PROP(0, vref_mv)` and a single shared
`DEVICE_API`. Change so each instance's DT `vref-mv` is the fallback
(`vref-mv` remains optional in the binding with default 3300):

- Store `vref_mv` in each instance's config from `DT_INST_PROP(n, vref_mv)`.
- When `CONFIG_ADC_STM32_VREFINT_CALIBRATE=y`: set `.vref_get` / `.vref_set` on
  every instance's API; `.ref_internal` = that instance's DT value (getter uses
  config / cache).
- When the Kconfig is `n`: leave `vref_*` NULL; `.ref_internal` = per-instance DT
  value (fixes the inst-0 quirk for the static path too).

Prefer per-instance `DEVICE_API` macros if a single shared API struct cannot hold
per-instance `ref_internal`.

**Breaking change — must be documented (Task 4 migration guide):** multi-ADC
boards with **divergent** per-instance `vref-mv` previously saw every ADC report
instance 0’s value. After this change, each instance reports its own property
(or default 3300). Same-value / omitted-property boards are unaffected.

- [ ] **Step 4: Commit (no signoff; with Assisted-by)**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git add drivers/adc/Kconfig.stm32 drivers/adc/adc_stm32.c
git commit -m "$(cat <<'EOF'
drivers: adc_stm32: add vref cache and optional get/set ops

Introduce Kconfig-gated shared VREF+ cache wired to the ADC vref API,
with per-instance DT vref-mv as fallback.

Tested with: west build -b <stm32_board_with_vref> for an ADC sample/app

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.5
EOF
)"
```

---

### Task 3: VREFINT measurement + init/calibrate hooks

**Files:**
- Modify: `deps/zephyr/drivers/adc/adc_stm32.c`
- Read-only reference: `deps/zephyr/drivers/sensor/st/stm32_vref/stm32_vref.c`

**Interfaces:**
- Consumes: Task 2 cache; `st,stm32-vref` DT (io-channels, cal, shift, cal-mv)
- Produces: measure helper; seed on owner init; refresh on owner `sequence.calibrate`

- [ ] **Step 1: Resolve owning ADC + cal constants from DT**

Use the same compatible/properties as the sensor (`st,stm32-vref`): `io-channels`, `vrefint-cal-mv`, cal resolution/shift, nvmem or OTP pointer. Prefer existing DT macros / bindings rather than hard-coded addresses.

- [ ] **Step 2: Implement measure without nested `adc_context` deadlock**

Align with sensor (12-bit path):

1. Runtime PM get if applicable.
2. Configure VREFINT channel (`ADC_GAIN_1`, `ADC_REF_INTERNAL`, long acquisition).
3. Enable `LL_ADC_PATH_INTERNAL_VREFINT` + stabilization delay.
4. Perform a **dedicated low-level one-shot conversion** (do not call public `adc_read` from inside an active sequence context).
5. Disable VREFINT path when done (unless injected-mode policy requires otherwise).
6. `mv = (cal_mv * (vrefint_cal >> shift)) / raw` (match sensor numerator style).
7. On success: lock cache, store `mv`, `valid = true`. On failure: log; leave prior valid value or keep invalid.

- [ ] **Step 3: Call measure from owner init**

After HW offset calibration as appropriate. Failure must **not** fail ADC init.

- [ ] **Step 4: Call measure when `sequence.calibrate` on owner**

In addition to existing offset/linearity cal. Non-owner calibrate: existing HW cal only.

- [ ] **Step 5: Stream / RTIO**

Replace compile-time `STM32_ADC_VREF_MV` uses in stream headers with live `adc_ref_internal(dev)` / cache read so frames match the getter.

- [ ] **Step 6: No PM-resume auto re-measure** (explicit non-goal)

- [ ] **Step 7: Commit (no signoff; with Assisted-by)**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git add drivers/adc/adc_stm32.c
git commit -m "$(cat <<'EOF'
drivers: adc_stm32: seed VREF+ cache from VREFINT calibration

Measure the shared rail at init and on calibrate for the VREFINT-owning
ADC so adc_ref_internal() returns a hardware-based scale.

Tested with: west build -b <stm32_board_with_vref>; runtime check when HW available

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.5
EOF
)"
```

---

### Task 4: Bindings, migration guide, sample cross-links

**Files:**
- Modify: `deps/zephyr/dts/bindings/adc/st,stm32-adc.yaml`
- Modify: `deps/zephyr/dts/bindings/sensor/st,stm32-vref.yaml`
- Modify: `deps/zephyr/doc/releases/migration-guide-4.5.rst` (or current)
- Modify: `deps/zephyr/samples/drivers/adc/adc_dt/` (STM32-oriented note or overlay demo)
- Modify: `deps/zephyr/samples/sensor/soc_voltage/README.rst`

**Interfaces:**
- Consumes: Task 2–3 behavior
- Produces: documented fallback semantics + migration warning

- [ ] **Step 1: Binding text**

- `st,stm32-adc` `vref-mv`: board nominal / **fallback** when calibrate disabled or cache invalid.
- `st,stm32-vref`: ADC driver may consume the same node for INTERNAL ref calibration; sensor remains usable.

- [ ] **Step 2: Migration guide — explicit breaking / behavior changes**

Add a dedicated STM32 ADC subsection in `migration-guide-*.rst` that labels both
items clearly (Zephyr style: what changed, who is affected, how to restore old
behavior):

1. **Breaking / behavior: measured INTERNAL mV (default)**  
   With `CONFIG_ADC_STM32_VREFINT_CALIBRATE=y` (default when `st,stm32-vref` is
   present), `adc_ref_internal()` and INTERNAL raw→mV helpers may no longer match
   DT `vref-mv` / 3300 exactly. Disable the Kconfig to keep static DT-only scale
   (also disables get/set and init/`sequence.calibrate` VREFINT refresh).

2. **Breaking: per-instance `vref-mv`**  
   `adc_ref_internal()` no longer forces every STM32 ADC instance to instance 0’s
   `vref-mv`. Each instance uses its own property (optional, default 3300). Affects
   only multi-ADC DTs with divergent `vref-mv` that relied on the old quirk.

Also note in release notes (API/drivers) with a pointer to the migration guide.

- [ ] **Step 3: Sample / soc_voltage README**

Extend adc_dt README: on STM32 with `CONFIG_ADC_STM32_VREFINT_CALIBRATE=y`, printed `adc_ref_internal` should be near sensor VREF. Cross-link soc_voltage: prefer ADC API for conversion helpers; sensor for explicit voltage channels.

- [ ] **Step 4: Commit (no signoff; with Assisted-by)**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git add dts/bindings/adc/st,stm32-adc.yaml \
  dts/bindings/sensor/st,stm32-vref.yaml \
  doc/releases/migration-guide-4.5.rst \
  samples/drivers/adc/adc_dt/ samples/sensor/soc_voltage/README.rst
git commit -m "$(cat <<'EOF'
docs: adc_stm32: document VREFINT-calibrated internal reference

Clarify DT fallback, migration impact, and sample/sensor cross-links.

Tested with: docs/bindings text review; migration guide entries present

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971

Assisted-by: Cursor:grok-4.5
EOF
)"
```

---

### Task 5: STM32 integration tests in Zephyr

**Files:**
- Create or extend under `deps/zephyr/tests/drivers/adc/` (e.g. `adc_stm32_vref/` or extend an existing STM32 ADC test)
- `testcase.yaml` / `tests.yaml` with `platform_allow` for boards that have `vref:` and ADC enabled

**Interfaces:**
- Consumes: Part 1 API + Part 2 driver
- Produces: automated checks for spec §8.2

- [ ] **Step 1: Write tests**

If creating a **new** test directory/files, add SPDX headers on new sources and
put `Origin: Original` in that commit's footer (see Global Constraints).

Cover:

1. After boot, `adc_ref_internal(adc)` ≈ `st,stm32-vref` sensor reading (± tolerance).
2. If two ADCs enabled: both getters return the same cached value.
3. `adc_ref_internal_set` on non-owner updates both getters / INTERNAL mV conversion.
4. `sequence.calibrate` on owner overwrites a prior set.
5. With `CONFIG_ADC_STM32_VREFINT_CALIBRATE=n`: set → `-ENOTSUP`; get uses DT only.

Use ztest + DT fixtures; skip gracefully if sensor or second ADC absent on a board (or split yaml scenarios).

- [ ] **Step 2: Run twister for allowed platforms**

```bash
cd /home/lasse/projects/zephyr-devel
export ZEPHYR_BASE=/home/lasse/projects/zephyr-devel/deps/zephyr
uv run west twister -T deps/zephyr/tests/drivers/adc/<new_or_existing> \
  -p <platform_from_yaml>
```

Expected: PASS on integration platforms (HW may be required; mark build-only vs runtime cases appropriately for CI).

- [ ] **Step 3: Build smoke with feature on/off** for one representative series that has `vref:`.

- [ ] **Step 4: Commit (no signoff; with Assisted-by)**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git add tests/drivers/adc/
git commit -m "$(cat <<'EOF'
tests: adc_stm32: cover VREFINT-calibrated adc_ref_internal

Verify shared cache, set/calibrate overwrite, and Kconfig off behavior.

Tested with: west twister -T tests/drivers/adc/<new_or_existing> -p <platform_from_yaml>

Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971
Origin: Original

Assisted-by: Cursor:grok-4.5
EOF
)"
```

---

### Task 6: Part 2 verification gate

- [ ] **Step 1: Confirm stack**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
git log --oneline adc-vref-runtime-api..adc-stm32-vrefint-calibrate
```

Expected: Part 2 commits only on top of Part 1.

- [ ] **Step 2: Run check_compliance.py on the Part 2 range**

```bash
cd /home/lasse/projects/zephyr-devel/deps/zephyr
./scripts/ci/check_compliance.py -c adc-vref-runtime-api..adc-stm32-vrefint-calibrate
```

Expected: no unexpected failures; fix any real issues before handoff.

- [ ] **Step 3: Confirm commit trailers on Part 2 commits**

```bash
git log --format=%B adc-stm32-vrefint-calibrate --not adc-vref-runtime-api \
  | rg -i 'signed-off-by' || true
git log --format=%B adc-stm32-vrefint-calibrate --not adc-vref-runtime-api \
  | rg -c '^Assisted-by:'
git log --format=%B adc-stm32-vrefint-calibrate --not adc-vref-runtime-api \
  | rg -c '^Link: https://github.com/zephyrproject-rtos/zephyr/issues/113971'
```

Expected: no `Signed-off-by`; every AI-authored Part 2 commit has `Assisted-by:` and the RFC `Link:`. Commits that add new original files should also include `Origin: Original`.

- [ ] **Step 4: Hand off (human DCO / PR)**

Summarize both branches for the human's upstream PR prep (Part 1 PR, then Part 2 stacked PR). Do not push/open PRs unless asked.

Remind the human: add `Signed-off-by:` before upstream push, keep `Assisted-by:` / RFC `Link:`, PR body should summarize behavior/breaking changes and link [#113971](https://github.com/zephyrproject-rtos/zephyr/issues/113971), and watch CI.

---

## Spec coverage (Part 2)

| Spec item | Task |
|---|---|
| Kconfig + install get/set | Task 2 |
| Shared cache / INTERNAL-only | Task 2 |
| Measure formula / init / calibrate | Task 3 |
| Stream live vref / no PM auto refresh | Task 3 |
| Bindings + migration + samples | Task 4 |
| Integration tests §8.2 | Task 5 |
| Common API / emul | Part 1 (prerequisite) |
