# Application Architecture Notes

Target application:

- read a local PMS5003 particulate sensor over UART,
- store compact records on the attached SD card,
- expose current values and simple commands over OPC UA Ethernet,
- optionally display local status on a small I2C OLED/LCD,
- optionally accept one or more local button inputs.

## Proven So Far

- W5500 Ethernet works on the Waveshare ESP32-S3-ETH.
- Modbus TCP works.
- A tuned open62541 OPC UA server works over Ethernet.
- A real Python OPC UA client can open a session and read nodes.
- The onboard TF/microSD card can be mounted and written while OPC UA remains reachable.
- Current flashed OPC UA endpoint:

```text
opc.tcp://192.168.50.2:4840
```

- Current flashed readable nodes:

```text
ns=1;s=simulated_value     Int32, expected 1234
ns=1;s=firmware_status     Int32, expected 1
ns=1;s=sd_status           Int32, expected 1 when SD mount + append worked
```

- A second simple read-only Int32 node did not break client compatibility.
- A third simple read-only Int32 node plus SD mount/write did not break client compatibility.
- The tuned OPC UA + SD firmware remains small enough for the board's flash, with about 64% of the large app partition free.

## Important Cautions

- The application-shaped OPC UA skeleton with several PM/status/command nodes built and flashed, but Python OPC UA client session setup failed with a UTF-8 decode error. The cause is not isolated yet.
- The failure was not caused merely by having two nodes; a two-node build works.
- Do not add UART, SD, display, and command handling all at once. Add one surface at a time and run a real OPC UA client session after each change.
- Do not judge feasibility from build success alone. For this project, the meaningful tests are:
  - full OPC UA client session,
  - node reads,
  - command writes,
  - repeated polling,
  - heap/stack stability.

## Capacity And Feature Budget

Hardware capacity:

```text
Physical flash:       16 MB
Current app partition: 0x177000 bytes = 1,536,000 bytes
PSRAM:                 8 MB
Internal RAM:          scarce resource; exact free budget must be measured per firmware
```

The current partition table uses a single app partition of about 1.5 MB. That app partition is the practical firmware-size limit right now, not the full 16 MB flash chip. The partition table can likely be changed later, but the current probes use the stock large single-app ESP-IDF partition.

Measured flash costs:

| Feature / firmware state | App size | App partition free | Notes |
| --- | ---: | ---: | --- |
| Early heavy OPC UA build | about `0x15e1a0` / 1.43 MB | about 7% | Misleadingly heavy baseline. |
| Tuned OPC UA, MINIMAL namespace, one node | `0x77d00` / 490 KB | 68% | Real client read worked. |
| Tuned OPC UA, MINIMAL namespace, two nodes | `0x77df0` / 491 KB | 68% | Two explicit reads worked. |
| Tuned OPC UA, REDUCED namespace, two nodes | `0x89350` / 562 KB | 63% | Built/flashed, but Python client session failed. |
| Tuned OPC UA, MINIMAL namespace, SD append, three nodes | `0x88d50` / 560 KB | 64% | Currently flashed; three explicit reads work. |

Measured REDUCED namespace cost:

```text
REDUCED minus MINIMAL: 71,008 bytes
Cost as current app partition: about 4.6%
```

Interpretation:

- Flash is not currently the limiting factor for a focused PMS5003 + SD + OPC UA build.
- Internal RAM and open62541 address-space behavior are more important risks than flash.
- The 8 MB PSRAM is useful for large task stacks and buffers, but some networking/open62541 paths still need internal RAM.
- The current OPC UA task uses a 64 KB stack allocated from PSRAM.

Current linked memory report for the working MINIMAL two-node firmware:

```text
Total image size: 490,882 bytes
App partition used: about 491 KB of 1,536 KB
DIRAM linked use: 110,787 bytes of 341,760 bytes
DIRAM linked remaining: 230,973 bytes
IRAM linked use: 16,383 bytes of 16,384 bytes
```

Current linked memory report for the flashed MINIMAL + SD append + three-node firmware:

```text
Total image size: 560,346 bytes
App partition used: about 560 KB of 1,536 KB
DIRAM linked use: 111,239 bytes of 341,760 bytes
DIRAM linked remaining: 230,521 bytes
IRAM linked use: 16,383 bytes of 16,384 bytes
```

Important interpretation:

- The DIRAM number is a link-time section report, not the same as runtime free heap.
- Runtime free internal heap still needs to be measured with a logging/debug build after each added feature.
- IRAM is essentially full according to the linker report, so avoid changes that force more code into IRAM.
- The app already links many ESP-IDF components because of default dependencies. Later optimization may remove unused WiFi/TLS/HTTP pieces if they are not needed.

Estimated feature budget:

| Feature | Expected flash cost | Expected RAM risk | Confidence | Recommendation |
| --- | ---: | --- | --- | --- |
| Add a few read-only OPC UA Int32 nodes | Tiny | Low | Measured for second node | Add one at a time. |
| PMS5003 UART parser | Small | Low | Inference | Use fixed 32-byte frame buffer. |
| SD append logging | About +69 KB in current build | Medium | Boot-time append measured | Next test is periodic logging under OPC UA polling. |
| Simple command node | Small | Medium | Write behavior not proven | Test one writable node in isolation. |
| I2C button input | Tiny | Low | Inference | Safe after OPC UA model stabilizes. |
| Simple I2C OLED text display | Small to moderate | Low to medium | Inference | Use tiny driver, not LVGL first. |
| Small HTTP status/config page | Moderate | Medium to high | Inference | Feasible, but test after OPC UA + SD. |
| WiFi plus Ethernet services | Moderate | Medium to high | Not measured | Prefer Ethernet-only first. |
| Rich web UI/assets from SD | Moderate flash, SD-heavy | Medium | Not measured | Serve static files carefully; avoid large dynamic pages. |
| OPC UA discovery-friendly namespace | +71 KB flash for REDUCED attempt | Unknown | Measured flash, failed client | Do not use yet; explicit NodeId reads are better for now. |

Scout/client strategy:

- For a constrained embedded OPC UA server, explicit known NodeIds are a valid product strategy.
- Generic tree browsing and NamespaceArray discovery are nice-to-have, not mandatory for a controlled sensor gateway.
- If the client is your own tool, prefer a configured list of expected NodeIds and types.
- Only pay the namespace/discovery cost if third-party tools must browse the device without prior knowledge.

## Recommended Firmware Shape

Use small, fixed data structures:

```text
current_pm1_0_ug_m3    int32
current_pm2_5_ug_m3    int32
current_pm10_ug_m3     int32
sample_count           uint32 or int32
sensor_status          int32 enum
last_command_status    int32 enum
```

Avoid:

- large generated OPC UA nodesets,
- dynamic allocation in the sensor hot path,
- large JSON documents in RAM,
- long in-memory history buffers,
- enabling subscriptions/history/security until the basic server is proven stable.

## PMS5003 UART

Expected fit: likely good.

Reasoning:

- PMS5003 packets are small.
- UART parsing can be done with a fixed-size frame buffer.
- Only current values need to live in RAM.

Recommended experiment:

1. Add UART receive task with fixed 32-byte PMS frame parser.
2. Keep the OPC UA surface unchanged.
3. Verify the existing two OPC UA nodes still read correctly.
4. Add one read-only PM node and verify client session.
5. Add remaining PM nodes one at a time.

## SD Logging

Expected fit: proven for boot-time append; periodic logging still needs measurement.

Reasoning:

- ESP-IDF FatFS/SDSPI mounted the card and appended a compact line while OPC UA still accepted full client sessions.
- Logging compact CSV or binary records should be much lighter than storing large JSON documents.
- Current FatFS config has long filenames disabled, so use 8.3 filenames or explicitly enable/test LFN.

Recommended record shape:

```text
unix_or_uptime_ms,pm1_0,pm2_5,pm10,status
```

Recommended experiment:

1. Extend the boot-time append into periodic sample logging.
2. Flush periodically, not on every byte.
3. Measure heap before mount, after mount, and during polling.
4. Test behavior when the card is absent, full, or has a dirty filesystem.

## OPC UA Commands

Expected fit: feasible, but not proven yet.

Use simple command variables first:

```text
reset_command          write 1 to request reset
last_command_status    read result
```

Open question:

- The first writable-node test returned `BadWriteNotSupported` from common Python client helpers.
- This may require access-level bits for status/timestamp writes, a callback value source, or a less aggressive open62541 profile.

Recommended experiment:

1. Add one writable Int32 command node.
2. Test write from Python `opcua`.
3. Test write from the intended OPC UA scout/client.
4. Only then wire the command to firmware behavior.

## I2C OLED/LCD

Expected fit: likely fine if display scope stays small.

Reasoning:

- I2C status displays are usually light compared with OPC UA.
- The risk is not I2C itself; the risk is pulling in a large graphics/UI stack.

Recommended approach:

- Prefer a tiny text/status driver first.
- Avoid LVGL until OPC UA + UART + SD are proven stable together.
- Update display at a low rate, such as 1 Hz.

## Buttons

Expected fit: very good.

Reasoning:

- GPIO input with debounce is tiny.
- Button state can be exposed as one read-only OPC UA variable or used locally for reset/menu behavior.

Recommended approach:

- Use GPIO interrupt or polling with debounce.
- Keep command behavior identical whether triggered by OPC UA or local button.

## Current Product Architecture Recommendation

The ESP32-S3 remains a valid candidate for the focused sensor gateway if the product is kept small:

- OPC UA current values,
- compact SD append logging,
- UART sensor parser,
- optional simple local display,
- optional button.

The riskiest feature is not UART, SD, I2C, or buttons individually. The riskiest feature is the OPC UA address-space and command model. Prove that incrementally before adding the remaining peripherals.
