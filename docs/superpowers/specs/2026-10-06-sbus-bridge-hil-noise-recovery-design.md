# sbus_bridge HIL noise recovery — Design

- **Date:** 2026-10-06
- **Status:** Approved (implement A + C)
- **Scope:** Two new ZTest cases in `tests/sbus_bridge_hil` that flood the DUT
  UART input with a fast pseudo-random bytestream, then prove a single valid
  HIL frame still reaches S.BUS.

## 1. Purpose

Show that garbage on the 115200 input does not permanently brick the DUT
assembler: after noise, a well-formed frame (header + payload + CRC + footer)
is cut through and logged on the tester’s S.BUS RX path.

## 2. Cases

### C — `test_noise_no_header` (no false headers)

1. Settle, `hil_wire_reset`, disarm capture.
2. Send `HIL_NOISE_N` pseudo-random bytes with **no `0x0F`** (rewrite any
   drawn header byte). DUT stays in HUNT and should not TX.
3. Settle, reset RX counters.
4. Send one valid HIL frame (`seq = HIL_NOISE_SEQ`).
5. Assert exactly one logged frame with that seq and `corrupt_count == 0`.

### A — `test_noise_resync` (false headers + drain)

1. Settle, `hil_wire_reset`, disarm capture.
2. Send `HIL_NOISE_N` unrestricted pseudo-random bytes (may contain `0x0F`).
   DUT may open COLLECT windows and emit junk S.BUS.
3. `hil_wire_drain()` (24× footer) to close any stuck COLLECT, then settle.
4. Reset RX counters (discard junk/corrupt from the flood).
5. Send one valid HIL frame (`seq = HIL_NOISE_SEQ`).
6. Assert exactly one logged frame with that seq and `corrupt_count == 0`.

## 3. Wire API

Add `hil_wire_send_bytes(const uint8_t *data, size_t len)` that chunk-sends
through the existing ≤25-byte TX path. Reuse `hil_wire_drain` for A.

## 4. Parameters

| Symbol | Value | Role |
| --- | --- | --- |
| `HIL_NOISE_N` | 512 | Flood length (bytes) |
| `HIL_NOISE_SEED` | fixed | Reproducible xorshift32 |
| `HIL_NOISE_SEQ` | `0x4E01u` | Distinct seq for the recovery frame |

## 5. Non-goals

- Measuring RTT during the flood.
- Asserting zero DUT TX during unrestricted noise.
- Changing DUT `sbus_pipe` idle-gap abort behavior.
