# OPC UA Experiment Log

This log captures the important discoveries, wrong assumptions, and configuration changes from the ESP32-S3 OPC UA feasibility work. The goal is to make the investigation reproducible instead of presenting only the final working setup.

## Experiment 1: First open62541 ESP-IDF Build

Question: Can open62541 build and run at all on the Waveshare ESP32-S3-ETH?

Result: Partially successful, but misleadingly pessimistic.

Evidence:

- open62541 v1.5.4 built into ESP-IDF firmware.
- Firmware flashed and booted.
- W5500 Ethernet worked.
- `UA_Server_new()` crashed with a 24 KB FreeRTOS task stack.
- Raising the OPC UA task stack to 64 KB allowed the firmware to stay alive.
- Firmware image was about `0x15e1a0` bytes.
- The large app partition had only about 7% free.
- Internal heap after startup was roughly 31 KB free in the observed stable run.

Learnings:

- The 24 KB task stack was too small for open62541 namespace-zero initialization.
- This result did not prove the ESP32-S3 was inadequate. It proved the first configuration was heavy.
- The first conclusion over-weighted one debug/heavy build instead of treating it as a baseline.

## Experiment 2: Configuration Audit After Reopening Feasibility

Question: Were the original settings unfairly expensive for a small sensor OPC UA server?

Result: Yes.

Evidence:

- ESP-IDF was configured with debug optimization:
  - `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y`
  - assertions enabled
  - default log level `INFO`
- open62541 still had features enabled that were not needed for a tiny PMS5003-style server:
  - method calls
  - node management
  - diagnostics
  - JSON encoding
- The 64 KB OPC UA task stack was allocated from internal RAM.
- PSRAM was mostly available while internal RAM was tight.

Learnings:

- The previous build mixed proof-of-concept convenience with feasibility measurement.
- Large task stacks should be moved to PSRAM when the code path permits it.
- Debug settings are useful while bringing up hardware, but they should not be used to judge final feasibility.

## Experiment 3: Minimal open62541 Profile

Question: Can open62541 be regenerated with a smaller server profile suitable for a compact sensor address space?

Result: Yes, with some constraints.

Successful profile:

```text
UA_ENABLE_ENCRYPTION=OFF
UA_ENABLE_SUBSCRIPTIONS=OFF
UA_ENABLE_SUBSCRIPTIONS_EVENTS=OFF
UA_ENABLE_HISTORIZING=OFF
UA_ENABLE_DISCOVERY=OFF
UA_ENABLE_METHODCALLS=OFF
UA_ENABLE_NODEMANAGEMENT=OFF
UA_ENABLE_DIAGNOSTICS=OFF
UA_ENABLE_JSON_ENCODING=OFF
UA_ENABLE_XML_ENCODING=OFF
UA_ENABLE_TYPEDESCRIPTION=ON
UA_ENABLE_STATUSCODE_DESCRIPTIONS=OFF
UA_ENABLE_PUBSUB=OFF
UA_ENABLE_PUBSUB_INFORMATIONMODEL=OFF
UA_NAMESPACE_ZERO=MINIMAL
UA_LOGLEVEL=700
UA_ARCHITECTURE=lwip
UA_MULTITHREADING=0
```

Failed or adjusted settings:

- `UA_NAMESPACE_ZERO=MINIMAL` failed until PubSub and the PubSub information model were explicitly disabled.
- Turning off XML encoding required keeping XML value extraction out of scope.
- `UA_ENABLE_TYPEDESCRIPTION=OFF` caused generated code to reference a missing `UA_DataTypeMember.memberName` field, so typedescription remains enabled.

Learnings:

- `MINIMAL` namespace zero is the right direction for this device class, but open62541 options interact.
- Some options that look optional are required by generated code in this version/profile.
- Keep the generation command with the project so future changes can be compared against a known-good profile.

## Experiment 4: ESP-IDF Memory Placement And Size Tuning

Question: Can the ESP32-S3 run the smaller OPC UA server with more headroom?

Result: Yes.

Changes:

- Switched to size optimization:
  - `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`
  - assertions disabled
  - default log level set to `WARN`
- Changed PSRAM allocation behavior:
  - `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`
  - `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`
  - `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`
- Moved the 64 KB OPC UA task stack to PSRAM:
  - `xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`

Evidence:

- Tuned build succeeded.
- Tuned firmware size was about `0x77d00` bytes.
- The default large app partition had about 68% free.
- Firmware flashed successfully.

Learnings:

- Moving the stack to PSRAM directly addresses the earlier internal-RAM pressure.
- The reduced open62541 profile changed flash feasibility dramatically.
- A small OPC UA server is a plausible ESP32-S3 workload; a broad multi-service platform still needs incremental measurement.

## Experiment 5: Ethernet And OPC UA Transport Check

Question: Is the tuned firmware actually reachable over Ethernet and responding as OPC UA TCP?

Result: Yes.

Evidence:

- Initial ping failed because the Pi direct Ethernet interface was only on a `169.254.x.x` link-local address.
- Adding a temporary Pi-side address made the direct link routable:

```bash
sudo ip addr add 192.168.50.1/24 dev eth0
```

- After that, ping succeeded:
  - 4 packets transmitted
  - 4 packets received
  - 0% packet loss
- TCP port `4840` accepted a connection.
- A raw OPC UA TCP `HEL` message received an `ACKF` response.

Learnings:

- Failed pings on a direct cable are not always firmware failures; confirm the host interface is on the same subnet.
- The OPC UA result is stronger than an open TCP port because the server responded correctly at the OPC UA transport handshake layer.
- A full client session is still unproven and should be the next OPC UA test.

## Experiment 6: Full Python OPC UA Client Session

Question: Can a real OPC UA client open a session and read the exposed node?

Result: Yes.

Evidence:

- Python `opcua` client connected to `opc.tcp://192.168.50.2:4840`.
- Client opened a session successfully.
- Client read `ns=1;s=simulated_value`.
- Returned value was `1234`.
- The client reported that it requested a 3600000 ms secure-channel timeout and the server returned 600000 ms. This did not prevent communication.

Learnings:

- The ESP32 server is usable by a real OPC UA client, not only by a raw transport handshake.
- Current known-good client target:

```text
Endpoint: opc.tcp://192.168.50.2:4840
NodeId: ns=1;s=simulated_value
Security: None
Authentication: Anonymous
```

## Experiment 7: Application-Shaped Node Skeleton

Question: Can we quickly extend the server from one static value into a PMS5003-shaped data surface with simulated PM values, uptime, sample count, and a reset command?

Result: Build and flash succeeded, but full OPC UA client session failed. The firmware was rolled back to the known-good single-node build so the device remains usable for client testing.

Attempted nodes:

```text
ns=1;s=pm1_0_ug_m3
ns=1;s=pm2_5_ug_m3
ns=1;s=pm10_ug_m3
ns=1;s=sample_count
ns=1;s=uptime_ms
ns=1;s=reset_command
ns=1;s=last_command_status
```

Evidence:

- Application-shaped firmware built successfully.
- Image size remained small, about `0x78150` to `0x77fe0`, still leaving about 68% of the app partition free.
- Firmware flashed successfully.
- Python OPC UA client reached session setup but failed decoding the CreateSession response with a UTF-8 decode error.
- Changing node initial values from stack-backed `UA_Variant_setScalar` to copied values did not resolve the client decode failure.
- Changing to static/global backing storage and removing local `UA_Server_writeValue` update calls also did not resolve the client decode failure.
- The board was restored to the previous known-good single-node firmware, and client read of `ns=1;s=simulated_value` succeeded again.

Learnings:

- The failure does not appear to be flash-size pressure; image size stayed comfortably small.
- The failure is likely an open62541 profile/modeling interaction, not a basic Ethernet or hardware failure.
- The reduced `UA_ENABLE_NODEMANAGEMENT=OFF` profile may be too aggressive for evolving a richer dynamic address space with `UA_Server_addVariableNode`.
- Before adding PMS5003, SD, display, or button code, the next OPC UA task should isolate address-space growth:
  - add one extra read-only node,
  - test full client session,
  - add one writable command node,
  - test full client session and write behavior,
  - only then add live update logic.
- For embedded C examples, be explicit about value lifetime. `UA_Variant_setScalar` points at caller-owned storage; use storage that outlives the node, or prove the selected open62541 path performs a deep copy.
- Remote writes using common client helpers may include status or timestamps. Writable command nodes may need `UA_ACCESSLEVELMASK_STATUSWRITE` and `UA_ACCESSLEVELMASK_TIMESTAMPWRITE`, or command handling should use a callback/data-source pattern that is tested with the target clients.

## Experiment 8: Two Read-Only Node Isolation Test

Question: Does merely adding a second read-only OPC UA node break client compatibility?

Result: No. The two-node firmware works and is currently flashed on the device.

Added node:

```text
ns=1;s=firmware_status
```

Evidence:

- Firmware built successfully.
- Image size was about `0x77df0`, still leaving about 68% of the app partition free.
- Firmware flashed successfully.
- Python `opcua` client opened a session and read both nodes:
  - `ns=1;s=simulated_value` returned `1234`
  - `ns=1;s=firmware_status` returned `1`

Learnings:

- Simple address-space growth is safe in the current reduced open62541 profile.
- The failed application-shaped skeleton was not caused merely by having more than one node.
- Adding one simple read-only Int32 node changed firmware size by only a small amount compared with the known-good single-node build.
- The next isolation step should add one PMS-style read-only node at a time, then add live update logic, then add command/write behavior.

## Experiment 9: opcuaScout Compatibility With Minimal Namespace Zero

Question: Why did `opcuaScout` appear unable to connect/read when the Python direct-read test worked?

Result: The scout connected successfully, but failed when it tried to read standard discovery nodes omitted by the current `UA_NAMESPACE_ZERO=MINIMAL` firmware.

Evidence:

- `opcuaScout` log showed `~~Connected.` before the failure.
- The exception occurred while reading `client.get_namespace_array()`.
- Error was `BadNodeIdUnknown` for the standard NamespaceArray path.
- Running the scout with an explicit read worked:

```bash
python3 runOpcuaScout.py --endpoint opc.tcp://192.168.50.2:4840 --read 'ns=1;s=simulated_value'
python3 runOpcuaScout.py --endpoint opc.tcp://192.168.50.2:4840 --read 'ns=1;s=firmware_status'
```

- Browsing from the standard Objects folder also produced sparse/unhelpful output because the minimal namespace-zero profile omits some standard nodes/metadata expected by discovery tools.

Learnings:

- `UA_NAMESPACE_ZERO=MINIMAL` saves substantial firmware size but can reduce compatibility with generic browse/discovery workflows.
- For the current embedded profile, explicit NodeId reads are the reliable test path.
- Generic OPC UA tools should treat NamespaceArray and full tree browse failures as nonfatal when probing small embedded servers.
- If rich discovery/browsing is required for the final product, test `UA_NAMESPACE_ZERO=REDUCED` again and measure flash/RAM impact.

## Experiment 10: REDUCED Namespace Zero Cost And Compatibility

Question: What is the cost of `UA_NAMESPACE_ZERO=REDUCED`, and does it fix generic discovery?

Result: It built and flashed, but did not work with the current option mix. The device was rolled back to the known-good `MINIMAL` firmware.

Configuration:

- Same reduced open62541 feature set as the working `MINIMAL` build.
- Changed only namespace zero from `MINIMAL` to `REDUCED`.

Measured flash cost:

```text
Current app partition:       0x177000 bytes = 1,536,000 bytes
MINIMAL two-node firmware:   0x77df0 bytes  =   490,992 bytes, 68% partition free
REDUCED two-node firmware:   0x89350 bytes  =   562,000 bytes, 63% partition free
REDUCED added:               0x11560 bytes  =    71,008 bytes
```

Interpretation:

- REDUCED costs about 71 KB more app flash than MINIMAL in this two-node probe.
- That is about 4.6% of the current 1.5 MB app partition.
- Against the board's 16 MB physical flash, 71 KB is small, but the active single-app partition is the real short-term constraint unless the partition table is changed.

Runtime/client result:

- REDUCED firmware built successfully.
- REDUCED firmware flashed successfully.
- Python `opcua` client failed during `CreateSessionResponse` decode with a UTF-8 decode error.
- Explicit reads failed because the client could not complete session creation.
- This resembles the failure seen in the larger application-shaped node skeleton.

Learnings:

- REDUCED namespace zero is not an automatic fix for discovery compatibility in this trimmed open62541 profile.
- The failure may be caused by interaction between `REDUCED` namespace zero and one of the aggressive feature cuts, such as discovery, node management, diagnostics, XML, or status-code descriptions.
- Do not spend the REDUCED flash budget unless a specific client requires discovery and the REDUCED profile is proven with that client.
- For this application, a scout/client algorithm based on known explicit NodeIds is currently the better path than forcing generic OPC UA discovery on the ESP32.
- If discovery becomes a product requirement, test REDUCED again with less aggressive open62541 cuts one at a time.

## Experiment 11: SD Append Logging With OPC UA Still Alive

Question: Can the ESP32-S3-ETH mount its onboard TF/microSD card, append a compact log record, and still serve the tuned OPC UA profile?

Result: Yes.

Configuration:

- TF/microSD is wired as SPI, separate from the W5500 Ethernet SPI bus:
  - CS `GPIO4`
  - MISO `GPIO5`
  - MOSI `GPIO6`
  - SCLK `GPIO7`
- SD uses `SPI3_HOST`.
- W5500 Ethernet remains on its existing SPI host/pins.
- FatFS long filename support is disabled in the current `sdkconfig`, so the test log filename uses 8.3 format:
  - `/sdcard/opclog.csv`

Firmware change:

- Mount the SD card during startup.
- Append one compact CSV line containing uptime and heap values.
- Add one read-only OPC UA status node:

```text
ns=1;s=sd_status
```

Status values:

```text
 1  SD mounted and append logging worked
 0  SD probe not started
-1  SD SPI bus init failed
-2  SD mount failed
-3  SD append open/write failed
```

Evidence:

- First SD build flashed and all OPC UA explicit reads still worked.
- First SD build returned `sd_status = -3`.
- Cause was likely the long filename `opcua_probe_log.csv` while `CONFIG_FATFS_LFN_NONE=y`.
- Changing the file to 8.3-compatible `opclog.csv` fixed the append.
- After the filename fix, the Python OPC UA scout read:
  - `ns=1;s=simulated_value` returned `1234`
  - `ns=1;s=firmware_status` returned `1`
  - `ns=1;s=sd_status` returned `1`

Measured flash and linked memory:

```text
Current app partition:        0x177000 bytes = 1,536,000 bytes
MINIMAL two-node firmware:    0x77df0 bytes  =   490,992 bytes, 68% partition free
MINIMAL + SD + 3 nodes:       0x88d50 bytes  =   560,464 bytes, 64% partition free
Added by SD/FatFS/status:     0x10f60 bytes  =    69,472 bytes

DIRAM linked use:             111,239 bytes of 341,760 bytes, 32.55%
DIRAM linked remaining:       230,521 bytes
IRAM linked use:               16,383 bytes of 16,384 bytes
```

Learnings:

- SD append logging can coexist with the current tuned OPC UA server.
- This is stronger than build success: a real OPC UA client session and explicit reads passed after SD mount/write.
- FatFS configuration matters. With long filenames disabled, use 8.3 filenames or enable LFN and measure the cost.
- The current SD proof only appends once at boot. Periodic logging and wear/error handling still need a follow-up stress test.
- The OPC UA status-node pattern is useful for embedded probes because it avoids needing serial logs for every peripheral result.

## Experiment 12: Periodic SD Logging, Live OPC UA Values, And Command Writes

Question: Can the firmware keep a client session alive while periodically appending to SD, updating live OPC UA status values, and accepting a simple command write?

Result: Yes.

Firmware change:

- Replace `UA_Server_run()` with explicit startup/iterate/shutdown calls so the server task can update live nodes:

```text
UA_Server_run_startup(server)
UA_Server_run_iterate(server, true)
UA_Server_run_shutdown(server)
```

- Add a background SD logger task that appends to `/sdcard/opclog.csv` every 5 seconds.
- Add live/status nodes:

```text
ns=1;s=sd_append_count
ns=1;s=reset_command
ns=1;s=last_command_status
```

- Expand writable node access flags to include status and timestamp writes:

```text
UA_ACCESSLEVELMASK_READ
UA_ACCESSLEVELMASK_WRITE
UA_ACCESSLEVELMASK_STATUSWRITE
UA_ACCESSLEVELMASK_TIMESTAMPWRITE
```

Important failed intermediate:

- The first manual iterate-loop build skipped `UA_Server_run_startup()`.
- Ethernet still pinged, but TCP port `4840` refused connections.
- This proved that `UA_Server_run()` was previously doing listener startup for us.

Evidence:

- Corrected periodic-logging firmware built and flashed.
- A 30-second single-session Python OPC UA poll read four nodes once per second while `sd_append_count` advanced from `8` to `14`.
- Normal Python helper write initially failed with `BadWriteNotSupported`.
- Low-level value-only write also failed until status/timestamp write access bits and `userAccessLevel` were set.
- After adding those access bits, both writes succeeded:
  - helper write changed `simulated_value` from `1234` to `2222`
  - low-level write changed it from `2222` to `3333`
- Command-node transaction succeeded:
  - wrote `7777` to `simulated_value`
  - wrote `1` to `reset_command`
  - firmware reset `simulated_value` to `1234`
  - firmware cleared `reset_command` to `0`
  - firmware incremented `last_command_status` from `0` to `1`
- A post-command 15-second polling run stayed connected while `sd_append_count` advanced from `13` to `16`.

Measured flash and linked memory for the command/logging build:

```text
Current app partition:         0x177000 bytes = 1,536,000 bytes
MINIMAL + SD + command nodes:  0x89360 bytes  =   562,016 bytes, 63% partition free

Total image size:              561,898 bytes
DIRAM linked use:              111,255 bytes of 341,760 bytes, 32.55%
DIRAM linked remaining:        230,505 bytes
IRAM linked use:                16,383 bytes of 16,384 bytes
```

Learnings:

- Periodic SD append logging can coexist with a small OPC UA server and sustained client reads.
- A tiny command-node pattern is feasible in the current `MINIMAL` open62541 profile.
- Common OPC UA clients may include status or timestamp fields in writes. Writable command nodes should include status/timestamp write access bits unless the final client is known not to send them.
- Live OPC UA values can be updated safely from the OPC UA server task using explicit `run_startup` / `run_iterate` / `run_shutdown`.
- Do not call open62541 server APIs casually from unrelated tasks unless thread-safety is deliberately designed; this experiment keeps server node updates in the OPC UA task and lets the SD task communicate through small globals.

## Experiment 13: Runtime Heap Telemetry And Short Soak

Question: What runtime heap headroom remains while OPC UA polling and periodic SD logging are active?

Result: A 2-minute single-client polling soak completed cleanly with stable runtime heap readings.

Firmware change:

- Added read-only runtime telemetry nodes:

```text
ns=1;s=internal_heap_free
ns=1;s=internal_heap_min_free
ns=1;s=psram_free
ns=1;s=psram_min_free
```

- These are updated from the OPC UA server task using:

```text
heap_caps_get_free_size(MALLOC_CAP_INTERNAL)
heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)
heap_caps_get_free_size(MALLOC_CAP_SPIRAM)
heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)
```

Measured flash and linked memory:

```text
Current app partition:                 0x177000 bytes = 1,536,000 bytes
MINIMAL + SD + command + heap nodes:   0x892d0 bytes  =   561,872 bytes, 63% partition free

Total image size:                      561,754 bytes
DIRAM linked use:                      111,271 bytes of 341,760 bytes, 32.56%
DIRAM linked remaining:                230,489 bytes
IRAM linked use:                        16,383 bytes of 16,384 bytes
```

2-minute polling soak:

- One Python OPC UA client session.
- Read telemetry and functional nodes once per second for 120 seconds.
- SD logger continued appending every 5 seconds.
- Session completed without disconnect.

Runtime readings:

```text
sd_append_count:        10 -> 35
internal_heap_free:     287,515 bytes stable
internal_heap_min_free: 286,159 -> 286,135 bytes
psram_free:             8,300,392 bytes stable
psram_min_free:         8,234,852 -> 8,234,800 bytes
```

Interpretation:

- Internal heap is the important runtime limit, but this small application has roughly 287 KB free internal heap during the short soak.
- Minimum internal heap drifted by only 24 bytes during the 2-minute run.
- PSRAM has roughly 8.3 MB free at runtime, with the 64 KB OPC UA task stack already allocated from PSRAM.
- This is a short soak, not a production endurance test. The next useful stress test is longer duration plus more client activity.

## Current Feasibility Interpretation

The ESP32-S3-ETH appears feasible for a focused application:

- read a PMS5003 or similar local sensor,
- keep a small current-value data model in RAM,
- append compact records to SD,
- expose a small OPC UA address space over Ethernet.

The ESP32-S3-ETH is still a constrained target. Avoid:

- large generated OPC UA nodesets,
- large in-memory history buffers,
- rich web UI plus OPC UA plus multiple industrial protocols all at once,
- default/debug configs when judging production feasibility.

## Next OPC UA Experiments

- Add PMS-style nodes one at a time.
- Retest the browse-compatible profile with TwinCAT and another third-party client.
- Expand the proven command pattern into the final command set only after deciding the actual product commands.
- Add PMS5003 UART parsing with only current values stored in RAM.
- Run a longer soak test with periodic SD append logging under OPC UA polling and heap telemetry.
- Re-enable logs temporarily when measuring runtime heap, then turn logs back down.

## Experiment 14: DHCP And Network-Service Discovery

Question: Can the device move from the isolated Pi link to an ordinary LAN and
be found without a hard-coded address?

Result: Yes. DHCP lease acquisition, hostname publication, mDNS resolution, and
an OPC UA read through the mDNS name all worked on a temporary Pi-hosted DHCP
network.

Firmware changes:

- Removed the hard-coded `192.168.50.2` address and enabled the Ethernet DHCP client.
- Set the DHCP client hostname to `esp32-opcua`.
- Added mDNS hostname `esp32-opcua.local`.
- Advertised `_opcua-tcp._tcp` on port `4840`.
- Deferred creation of the OPC UA server task until `IP_EVENT_ETH_GOT_IP`.
- Left SD mounting and logging independent of network availability.

Measured test:

```text
client hostname: esp32-opcua
Ethernet MAC:    2a:84:85:53:12:20
test lease:      192.168.50.110
```

- Ping and TCP port `4840` checks succeeded.
- `esp32-opcua.local` resolved to the leased address.
- `opcuaScout` connected through the `.local` name.
- Reads returned `firmware_status=1`, an advancing SD append count, and
  approximately 280 KB free internal heap.

Measured firmware cost:

```text
Previous telemetry image: 0x892d0 bytes
DHCP + mDNS image:         0x90a60 bytes
Increase:                  0x7790 bytes = 30,608 bytes
App partition free:        61%
```

Learnings:

- The failed first main-LAN test was expected from the old static-IP firmware:
  it never sent a DHCP request.
- The W5500 MAC observed by DHCP is `2a:84:85:53:12:20`. The ESP32 base MAC
  printed while flashing is `28:84:85:53:12:20`; use the W5500 MAC for a router
  DHCP reservation.
- Network location discovery does not require a larger OPC UA namespace-zero
  profile. mDNS can locate the endpoint while clients use explicit NodeIds.
- Router DHCP lease tables remain the best fallback because some managed or
  segmented networks suppress mDNS multicast.
- The first mDNS build used internal RAM for its task and allocations and
  reported about 280 KB free internal heap. Moving the supported mDNS
  allocations to PSRAM raised free internal heap to about 284 KB. The final
  verified readings were 284,379 bytes free internal heap and 8,295,420 bytes
  free PSRAM while OPC UA and SD logging were active.

## Experiment 15: Browse-Compatible REDUCED Namespace

Question: Can standard OPC UA clients browse the ESP32 without sacrificing the
memory budget?

Result: Yes. `UA_NAMESPACE_ZERO=REDUCED` with
`UA_ENABLE_NODEMANAGEMENT=ON` passed endpoint discovery, namespace discovery,
tree browsing, explicit reads, and concurrent SD logging.

The original REDUCED attempt had node management disabled and produced malformed
session data. A broad diagnostic build first proved that REDUCED could work, but
left only about 32 KB internal heap. XML, diagnostics, and status-code
descriptions were then disabled again while node management remained enabled.
The lean profile continued to pass.

Final open62541 profile:

```text
UA_NAMESPACE_ZERO=REDUCED
UA_ENABLE_NODEMANAGEMENT=ON
UA_ENABLE_DIAGNOSTICS=OFF
UA_ENABLE_XML_ENCODING=OFF
UA_ENABLE_STATUSCODE_DESCRIPTIONS=OFF
UA_ENABLE_DISCOVERY=OFF
UA_ENABLE_SUBSCRIPTIONS=OFF
UA_ENABLE_ENCRYPTION=OFF
```

Measured results:

```text
Firmware image:          0xa2180 = 663,936 bytes
App partition remaining: 0xd4e80 = 872,064 bytes (57%)
Internal heap free:      210,303 bytes
PSRAM free:            8,294,360 bytes
```

The standard `Objects` folder, `Server`, `NamespaceArray`, and all application
nodes were browseable. The SD append counter continued advancing.

Compared with the prior MINIMAL + mDNS build, browse compatibility costs about
72 KB flash and 74 KB runtime internal heap. This is acceptable for the focused
sensor, SD, OPC UA, and small-display design, but internal heap should continue
to be measured as features are added.

## Experiment 16: Basic Data-Change Subscriptions

Question: Can TwinCAT/CODESYS-style monitored-item workflows coexist with the
browse-compatible server, SD logging, and available memory?

Result: Yes, after correcting the open62541 platform definition.

Basic subscriptions were enabled while events, alarms, and historizing remained
disabled. Subscription and monitored-item creation initially succeeded, but no
`Publish` notifications arrived. The vendored open62541 component defined
`UA_ARCHITECTURE_LWIP` without `UA_ARCHITECTURE_FREERTOS`, selecting a generic
monotonic-clock path that did not advance cyclic subscription timers correctly
on ESP-IDF. Defining both platform macros routed timer calls through the
ESP-specific `UA_DateTime_nowMonotonic()` implementation.

Measured result:

```text
Firmware image:          0xa54d0 = 677,072 bytes
App partition remaining: 0xd1b30 = 858,928 bytes (56%)
Internal heap free:      203,051 bytes with one subscription
PSRAM free:            8,294,220 bytes
```

A Python OPC UA client subscribed to `sd_append_count` and received values
`6`, `7`, and `8` during a 12-second test. Standard tree browsing and direct
polling reads continued to work afterward.

Basic subscriptions cost about 13 KB flash and roughly 7 KB internal heap in
this single-client test. This is a reasonable cost for compatibility with watch
lists and cyclic PLC client behavior.
