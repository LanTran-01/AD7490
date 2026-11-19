# Software Verification & Unit Test Report (DO-178C §6.4, §11.14)

**Project:** AD7490 16-Channel 12-Bit SAR ADC Bare-Metal Driver & Periodic Monitor
**Design Assurance Level:** DO-178C DAL A
**Verification Standards:**
- Coding Rules & DO-178C Guidelines: [`docs/C_rules_v3_addDO178C.md`](C_rules_v3_addDO178C.md)
- Low-Level Requirements: [`docs/LOW_LEVEL_REQUIREMENTS.md`](LOW_LEVEL_REQUIREMENTS.md)
- Software Design Description: [`docs/SOFTWARE_DESIGN_DESCRIPTION.md`](SOFTWARE_DESIGN_DESCRIPTION.md)
- Bi-Directional Traceability Matrix: [`docs/TRACEABILITY_MATRIX.md`](TRACEABILITY_MATRIX.md)

---

## 1. Verification Environment & Architecture

Verification consists of two independent, automated stages:
1. **Static Rule, Structural Metrics & Bi-Directional Traceability Audit** (`scripts/verify_compliance.py`, `scripts/verify_compliance.ps1`, and `test/check_style.awk`).
2. **Dynamic Host Unit-Test & Structural Coverage Suite** (`test/test_qspi_hal.c`, `test/test_frame_convert.c`, `test/test_device.c`, `test/test_sequencer.c`, `test/test_monitor.c`).

### 1.1 Test Harness & Contract Hook (`test/test_common.h`, `test/test_common.c`)
- `CHECK(expr)` evaluates and records every requirement verification assertion along with its source line number.
- `ASSERT(expr)` inside all production `static` helpers is macro-redirected via compiler flag `-D'ASSERT(expr)=test_assert((expr), (uint32_t)__LINE__)'` so any internal design-contract violation immediately fails the test suite (`g_assert_fail_count > 0`).

### 1.2 Behavioural Hardware Test Doubles
- **AXI Quad SPI Register Model (`test/test_qspi_hal.c`):** Allocates a 32-word (128-byte) aligned host memory window simulating the PG153 register space (`GIER 0x1C`, `SRR 0x40`, `SPICR 0x60`, `SPISR 0x64`, `SPIDTR 0x68`, `SPIDRR 0x6C`, `SPISSR 0x70`, `RXOCY 0x78`).
- **Behavioural AD7490 + QSPI Double (`test/fake_ad7490.h`, `test/fake_ad7490.c`):**
  - **`FAKE_MODE_RAW`**: Records transmitted DIN words (`fake_frame_din()`), byte counts, and dummy bytes while returning programmable RX words and HAL status codes.
  - **`FAKE_MODE_ADC`**: Implements the full AD7490 state machine including the **1-frame conversion pipeline** (Frame $N$ outputs the conversion of the channel selected at the end of Frame $N-1$), 12-bit Control Register decoding (`WRITE`, `SEQ`, `SHADOW`, `ADD3..0`, `CODING`), Shadow Register bit-reversed ring sequencing (`VIN0 = MSB`), `SEQ=1, SHADOW=0` online CR updates without interrupting the sequence, and fault injection (bus timeout on frame $K$, channel echo mismatch on frame $K$, `DOUT` stuck high `0xFFFF`, brown-out register reset glitch, `CODING` bit SEU upset, and configurable sequencer start latency).

---

## 2. Unit-Test Suite Summary

| Test Executable | Source File | Target Module Under Test | Test Functions | Requirements Verified | Status |
|:---|:---|:---|:---:|:---|:---:|
| `test_qspi_hal` | [`test/test_qspi_hal.c`](../test/test_qspi_hal.c) | `hal/axi_qspi_hal.c` | 6 | `LLR-QSPI-001` .. `LLR-QSPI-031` | **PASS** |
| `test_frame_convert` | [`test/test_frame_convert.c`](../test/test_frame_convert.c) | `driver/ad7490_driver.c` (§C, §D) | 7 | `LLR-AD7490-004`..`006`, `009`, `020`..`038` | **PASS** |
| `test_device` | [`test/test_device.c`](../test/test_device.c) | `driver/ad7490_driver.c` (§Public, §A) | 9 | `LLR-AD7490-001`..`003`, `007`..`008`, `010`..`011`, `014`, `040`..`046` | **PASS** |
| `test_sequencer` | [`test/test_sequencer.c`](../test/test_sequencer.c) | `driver/ad7490_driver.c` (§Public, §B) | 7 | `LLR-AD7490-012`..`013`, `015`, `050`..`054` | **PASS** |
| `test_monitor` | [`test/test_monitor.c`](../test/test_monitor.c) | `driver/ad7490_monitor.c` (§Public, §A, §B) | 14 | `LLR-AD7490-060` .. `LLR-AD7490-081` | **PASS** |

---

## 3. Detailed Test Case Breakdown

### 3.1 `test_qspi_hal` (`hal/axi_qspi_hal.c`)
1. **`test_init_params()`**: Exercises every MC/DC boundary of `is_hal_cfg_ok()`: `p_cfg == NULL`, `base_addr == 0U`, unaligned `base_addr` (`+1U`, `+2U`, `+3U`), `clock_mode == 4U`, `poll_timeout_us == 0U`, and `poll_timeout_us == 1000001U`.
2. **`test_init_sequence_and_faults()`**:
   - Verifies normal initialisation across all 4 SPI clock modes (`QSPI_CLOCK_MODE_0..3`), confirming `SRR == 0x0AU`, `GIER == 0U`, `SPISSR == 0xFFFFFFFFU`, and `SPICR` `CPOL`/`CPHA` bit masks.
   - Verifies defensive branch `DEF-QSPI-001` in `build_control_word((qspi_clock_mode_t)4U)`.
   - Verifies `QSPI_HAL_ERR_HW` when `SPICR` fails to settle to `0x00000180U` after `SRR` reset (`wait_reset_done() == false`).
   - Verifies `QSPI_HAL_ERR_HW` when `SPISR.MODF` (`0x10U`) is set after initialisation.
3. **`test_transceive_bytes()`**:
   - Verifies MC/DC parameter checks (`p_cfg == NULL`, `byte_count == 0U`, `byte_count == 257U`, `p_tx_bytes == NULL`, `p_rx_bytes == NULL`).
   - Verifies normal 2-byte full-duplex transfer (`RX_EMPTY = 0`, `RXOCY = 1`), checking `SPIDTR`, `p_rx_bytes`, and `SS0` deassertion.
   - Verifies `QSPI_HAL_ERR_TIMEOUT` when `RX_EMPTY` stays asserted across `poll_timeout_us * 100` poll iterations, confirming `p_rx_bytes` is unmodified.
   - Verifies `QSPI_HAL_ERR_HW` when `SPISR.MODF` asserts during a cycle.
4. **`test_write_bytes()`**: Verifies parameter bounds (`NULL`, `0U`, `257U`), normal transmit cycle, bus timeout, and `MODF` hardware fault.
5. **`test_read_bytes()`**: Verifies parameter bounds (`NULL`, `0U`, `257U`), normal receive cycle driving `dummy_tx_byte = 0xAAU` into `SPIDTR`, and timeout preservation of `p_rx_bytes`.
6. **`test_drain_rx_fault()`**: Directly invokes `drain_rx_bytes()` with `SPISR.RX_EMPTY = 1` to verify the hardware fault abort branch (`QSPI_HAL_ERR_HW`).

### 3.2 `test_frame_convert` (`driver/ad7490_driver.c` — Frame & Conversion Layers)
1. **`test_cr_encoding()`**: Verifies exact 16-bit DIN frame encoding (`0x9710U`, `0x83E0U`, `0xFC80U`, `0xC000U`) across all `ad7490_seq_mode_t`, range, coding, power mode, and DOUT mode combinations.
2. **`test_cr_rejects_bad_enum()`**: Injects out-of-range enum values into `power_mode`, `range_mode`, `coding`, and `dout_mode`, confirming `AD7490_ERR_PARAM` and untouched output; directly tests defensive branch `DEF-AD7490-001` in `encode_cr_word()`.
3. **`test_frames()`**: Verifies `write_frame()`, `txrx_frame()`, `read_frame()` (confirming `dummy_tx_byte == 0x00U`), `decode_dout()`, and preservation of `*p_rx_frame` on `QSPI_HAL_ERR_TIMEOUT` and `QSPI_HAL_ERR_HW`.
4. **`test_hal_status_map()`**: Verifies `map_hal_status()` for all defined `qspi_hal_status_t` values plus out-of-range `(qspi_hal_status_t)9U` (`DEF-AD7490-002` $\rightarrow$ `AD7490_ERR_SPI`), and `init_bus()`.
5. **`test_conversion_table()`**: Checks key datasheet boundary codes (`0x800 -> 0 mV`, `0x000 -> 2500 mV`, `0x7FF -> 4999 mV` in twos complement `2x VREF`; `0 -> 0 mV`, `2048 -> 1250 mV`, `4095 -> 2499 mV` in straight binary `1x VREF`; `4095 -> 5049 mV` at `VREF = 2525 mV, 2x`).
6. **`test_conversion_exhaustive()`**: Exhaustively tests all **$51 \times 2 \times 2 \times 4096 = 835,584$** combinations of `vref_mv` (`2475..2525 mV`), `range_mode` (`1x`, `2x`), `coding` (twos complement, straight binary), and `raw_lsb` (`0..4095`) against an independent double-precision floating-point reference formula, confirming **0 mismatches** ($|\text{got} - \text{ref}| \le 0.5\text{ mV}$).
7. **`test_vref_limits()`**: Verifies boundary values `2474 mV` (rejected), `2475 mV` (accepted), `2525 mV` (accepted), and `2526 mV` (rejected).

### 3.3 `test_device` (`driver/ad7490_driver.c` — Lifecycle, Single Read & Power Modes)
1. **`test_uninitialised_handle()`**: Confirms a zero-filled `ad7490_dev_t` handle is rejected with `AD7490_ERR_STATE` by both `ad7490_read_channel()` and `ad7490_set_power_mode()`.
2. **`test_init_rejects_bad_config()`**: Verifies `NULL` pointers, invalid `vref_mv` (`3300 mV`), wrong SPI clock mode (`MODE_3`), `poll_timeout_us < 3U`, and invalid `coding` enum are rejected with `AD7490_ERR_PARAM` and `0` SPI frames sent, leaving `dev.state == AD7490_STATE_FAULT`.
3. **`test_init_sequence()`**: Confirms `ad7490_init()` sends exactly 3 frames (`0xFFFFU`, `0xFFFFU`, `0x8310U`) and sets `dev.state == AD7490_STATE_READY`.
4. **`test_init_bus_errors()`**: Injects QSPI init hardware failure, timeout on dummy frame 2, and timeout on CR frame 3, confirming `AD7490_STATE_FAULT` on every error path.
5. **`test_read_channel()`**: Verifies 2-frame pipelined single-channel read on `VIN5` (`DIN = 0x9710U` twice, `raw_lsb = 0x510`, `voltage_mv = 1582 mV`) followed immediately by `VIN12`.
6. **`test_read_errors()`**: Verifies `NULL` arguments, `channel_id == 16U`, channel echo mismatch (`AD7490_ERR_CHAN_MISMATCH`), timeout on Frame 1, and timeout on Frame 2, confirming `p_sample` is untouched on all failures.
7. **`test_power_modes()`**: Verifies transition to `FULL_SHUTDOWN` (1 frame `0x8210U`, subsequent reads blocked with `AD7490_ERR_STATE`), wake to `NORMAL` (2 frames: CR `0x8310U` + dummy read frame with `DIN = 0x00U`), and `AUTO_STANDBY`.
8. **`test_power_mode_errors()`**: Verifies invalid enum (`4U`), `NULL` handle, timeout on the wakeup dummy frame transitions `dev.state` to `AD7490_STATE_FAULT`, and recovery via `ad7490_init()`.
9. **`test_corrupted_handle()`**: Simulates Single-Event Upsets (SEU) in RAM: flipping 1 bit in `dev.state` (`^= 0x100U`), corrupting `range_mode = 5U`, and corrupting `vref_mv = 1U` inside a `READY` handle.

### 3.4 `test_sequencer` (`driver/ad7490_driver.c` — Shadow Register Sequencer)
1. **`test_start_and_scan(0U)` & `test_start_and_scan(1U)`**: Verifies 3-frame startup (`0x83B0U`, Shadow word `0xA001U` for `VIN0, VIN2, VIN15`, priming frame), 5 consecutive scan passes (confirming Frame 1 sends CR scrub word `0xC330U` and Frames 2..3 send `0x0000U`), and `ad7490_sequencer_stop()` returning to `AD7490_STATE_READY` for both `0` and `1` extra start-latency frames.
2. **`test_scan_mismatch()`**: Verifies `AD7490_ERR_CHAN_MISMATCH` and `AD7490_STATE_FAULT` both when a sequenced channel is echoed twice in one pass and when an unsequenced channel is echoed.
3. **`test_bus_errors()`**: Injects timeouts on each of the 3 start frames, during a scan pass, and on the stop frame, confirming `AD7490_STATE_FAULT` in all cases.
4. **`test_refused_calls()`**: Verifies parameter and state guards (`NULL` pointers, `channel_mask == 0U`, `sample_count < 16U`, calling `read_channel` or `sequencer_start` while `SEQUENCING`, calling `sequencer_start` in `AUTO_STANDBY` power mode).
5. **`test_corrupted_handle()`**: Simulates RAM corruption of `channel_mask = 0U`, `coding = 9U`, and `vref_mv = 1U` while in `AD7490_STATE_SEQUENCING`.
6. **`test_scrub_repairs_coding()`**: Simulates an EMI/SEU bit-flip of the `CODING` bit inside the ADC mid-sequence (`fake_adc_upset_coding()`) and verifies that the Frame 1 CR scrub word automatically restores `CODING = 1` within a single scan pass without re-initializing or stopping the sequencer.

### 3.5 `test_monitor` (`driver/ad7490_monitor.c` — Moving Average & 3-Attempt Recovery)
1. **`test_window()`**: Verifies empty window returns `AD7490_ERR_STATE` (never a false `0 mV`), single-sample mean is undiluted (`2500 mV`, `fill_count = 1`), half-millivolt rounding (`2500, 2501 -> 2501 mV`), and 20-sample FIFO wrap-around (`5..20 -> mean 13 mV`, `fill_count = 16`).
2. **`test_window_corruption()`**: Simulates SEU corruption of `next_idx = 16`, `fill_count = 17`, `next_idx != fill_count` when partially full, and `window_mv[0] = 0xFFFFFFF0U`, confirming `AD7490_ERR_STATE` and zero overflow.
3. **`test_ramp_average()`**: Runs 40 acquisition cycles with dynamic input waveforms on `VIN0` and `VIN1`, comparing `ad7490_monitor_get_avg()` at every step against an independent sliding-window accumulator.
4. **`test_parameters()`**: Verifies all `NULL` pointer, `channel_id >= 16`, unmonitored channel, and `channel_mask == 0U` checks across all 5 public monitor functions.
5. **`test_bad_config_disables()`**: Verifies that an invalid `vref_mv` (`3000 mV`) or non-`NORMAL` power mode disables the monitor (`channel_mask = 0U`) so subsequent `step()` calls return `AD7490_ERR_STATE`.
6. **`test_init_bus_error_retried()`**: Verifies that a transient SPI timeout during `ad7490_monitor_init()` leaves the monitor armed, and subsequent `ad7490_monitor_step()` calls automatically re-configure the ADC and begin sampling.
7. **`test_scan_fault_and_recovery()`**: Injects a brown-out register loss (`fake_adc_glitch()`) after 20 good steps; verifies `AD7490_ERR_CHAN_MISMATCH`, confirms all averaging windows are emptied, verifies automatic recovery on the next `step()` (`reconfig_attempts = 1`), and confirms `reconfig_attempts` clears to `0` after exactly 16 consecutive good scans (`reconfig_total` remains `1`).
8. **`test_coding_upset_scrubbed()`**: Verifies that a silent `CODING` bit upset inside the ADC is repaired online by CR scrubbing with `0` re-configuration attempts triggered.
9. **`test_dead_adc_latches_failed()`**: Simulates `DOUT` stuck high (`fake_adc_set_dead(true)`), verifying 3 failed re-configuration attempts followed by `AD7490_ERR_DEVICE_FAILED` on the 4th step, confirming zero bus traffic once latched and recovery only via an explicit `ad7490_monitor_init()`.
10. **`test_intermittent_fault()`**: Injects a recurring fault every 3 good scans (< 16 stability window), confirming attempts accumulate across episodes (`1 -> 2 -> 3 -> AD7490_ERR_DEVICE_FAILED`).
11. **`test_rare_fault()`**: Injects 10 faults spaced by 28 good scans (> 16 stability window), confirming every fault is recovered (`is_failed == false`, `reconfig_total == 10`).
12. **`test_app_requested_reconfig()`**: Verifies `ad7490_monitor_reconfig()` called directly by the application.
13. **`test_timeouts_inside_attempts()`**: Injects bus timeouts inside Attempt 1 (init frame 1), Attempt 2 (verification scan frame 7), and Attempt 3 (sequencer start frame 4), confirming `AD7490_ERR_DEVICE_FAILED` latches on the failed 3rd attempt.
14. **`test_corrupted_state()`**: Simulates SEU corruption of `dev.channel_mask`, `avg[0].next_idx`, `mon.health` (single-bit flip fails safe to `AD7490_ERR_DEVICE_FAILED`), and saturation of `reconfig_total` at `0xFFFFFFFFU`.

---

## 4. Structural Coverage & Rule Compliance Summary

| Verification Metric | Target (DO-178C DAL A) | Achieved Result | Evidence / Tool |
|:---|:---:|:---:|:---|
| **Rule §0 Formatting (Allman, $\le 80$ cols, no `//`, LF)** | 100% | **100% (0 violations across 15 files)** | `make style` (`test/check_style.awk`) & `scripts/verify_compliance.*` |
| **Rule §2 Naming (3..31 chars, no vague stems, no prefixes)** | 100% | **100% (0 violations)** | `scripts/verify_compliance.py` / `.ps1` |
| **Rule §4 Function Metrics ($\le 100$ lines, $\le 5$ params, 1 `return`)** | 100% | **100% (max length: 84 lines)** | `scripts/verify_compliance.py` / `.ps1` |
| **Bi-Directional Traceability (`@req` $\leftrightarrow$ `@design` $\leftrightarrow$ Docs)** | 100% | **100% (63 LLR IDs, 63 SDD IDs)** | `docs/TRACEABILITY_MATRIX.md` & `scripts/verify_compliance.*` |
| **Statement / Line Coverage (`driver/`, `hal/`)** | 100% | **100%** | `make coverage` (`gcovr`) |
| **Decision / Branch & MC/DC Coverage (`driver/`, `hal/`)** | 100% (excluding `ASSERT` failure branches & hardware-stuck-SS0 fault) | **100%** | `make coverage` (`gcovr --txt-metric branch`) |
