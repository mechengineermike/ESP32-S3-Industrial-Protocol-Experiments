# ESP32-S3 Industrial Protocol Experiments

This repository contains feasibility probes for running industrial Ethernet protocols on a Waveshare ESP32-S3-ETH board.

The main result: the board is useful for lightweight Ethernet and Modbus TCP experiments, and a small OPC UA server is feasible when open62541 and ESP-IDF are configured for an embedded target. The earlier pessimistic OPC UA result came from a heavier open62541 profile, debug-oriented settings, and a large task stack placed in internal RAM.

The current OPC UA probe also mounts the onboard TF/microSD card, appends one compact CSV line, and exposes that result through OPC UA. This means the repo now has a small but real proof that Ethernet OPC UA and SD logging can coexist on this board.

The current flashed probe goes one step further: it periodically appends to SD, exposes an append counter, and accepts a simple writable reset command over OPC UA.

For the detailed OPC UA investigation trail, including settings that were proven too heavy or incompatible, see [`docs/opcua-experiment-log.md`](docs/opcua-experiment-log.md).

For notes on shaping this into a PMS5003 + SD logging + OPC UA gateway, see [`docs/application-architecture-notes.md`](docs/application-architecture-notes.md).

For clean-machine setup, build, flash, network, and verification instructions,
see [`docs/onboarding.md`](docs/onboarding.md).

## Hardware Tested

- Waveshare ESP32-S3-ETH
- ESP32-S3, dual core, 240 MHz
- 8 MB PSRAM
- 16 MB flash
- W5500 Ethernet
- USB-C serial/JTAG programming
- Direct RJ45 connection to a Raspberry Pi reTerminal DM
- Onboard TF/microSD slot tested with an 8 GB card

Early direct-link tests used static IP addresses:

- Raspberry Pi Ethernet: `192.168.50.1`
- ESP32 Ethernet: `192.168.50.2`

The current `opcua_probe` firmware uses DHCP and identifies itself as:

```text
DHCP hostname: esp32-opcua
mDNS hostname: esp32-opcua.local
OPC UA URL:    opc.tcp://esp32-opcua.local:4840
Ethernet MAC:  2a:84:85:53:12:20
```

The router's DHCP lease list is the most universal way to find the assigned
address. Search for either `esp32-opcua` or the Ethernet MAC. On networks that
allow multicast DNS, the `.local` URL works without knowing the numeric IP.

## Toolchain Used

- ESP-IDF v5.4.2
- Target: `esp32s3`
- Python 3.9 on Raspberry Pi OS
- Serial device observed as:

```text
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:53:12:20-if00
```

Your serial path will probably be different.

## Firmware Probes

### `firmware/ethernet_status`

Minimal W5500 Ethernet bring-up and HTTP status endpoint.

What worked:

- Static IP Ethernet over direct RJ45.
- Ping from the Raspberry Pi to the ESP32.
- Tiny HTTP status response with heap/PSRAM information.

This is the baseline proof that the board, USB cable, ESP-IDF setup, and W5500 pin configuration are correct.

### `firmware/modbus_tcp_probe`

Minimal Modbus TCP server.

What worked:

- ESP32 accepted TCP connections over Ethernet.
- Basic Modbus TCP register read/write behavior worked from the Pi.
- This protocol looks realistic for the board.

Interpretation: Modbus TCP is a good fit for this ESP32-S3-ETH board when the application remains modest.

### `firmware/opcua_probe`

Experimental OPC UA server using open62541 v1.5.4.

What worked:

- open62541 could be built into an ESP-IDF firmware image.
- The firmware flashed and booted.
- W5500 Ethernet initialized.
- The Pi could ping the ESP32 after increasing the OPC UA task stack.
- `UA_Server_new()` required a much larger FreeRTOS task stack than the first attempt.
- After tuning, the OPC UA firmware built to about `0x77d00` bytes.
- The tuned build left about 68% of the default large app partition free.
- The tuned build answered ping at `192.168.50.2`.
- TCP port `4840` accepted connections.
- A raw OPC UA TCP `HEL` message received a valid `ACKF` response.

Important constraints observed:

- The first OPC UA firmware image was large: about `0x15e1a0` bytes.
- The first large single-app partition had only about 7% free after the OPC UA probe.
- A 24 KB OPC UA task stack crashed during open62541 namespace-zero initialization.
- A 64 KB OPC UA task stack allowed the firmware to stay up and heartbeat.
- The first stable run left internal heap very tight, roughly 31 KB free.
- The tuned build moves the 64 KB OPC UA task stack to PSRAM with ESP-IDF `xTaskCreateWithCaps`.
- The current compatibility build uses open62541 `REDUCED` namespace zero with
  node management and basic data-change subscriptions enabled. Encryption,
  subscription events/alarms, historizing, discovery registration, method
  calls, diagnostics, JSON, XML, PubSub, and status-code descriptions remain
  disabled.
- `UA_ENABLE_TYPEDESCRIPTION` had to remain enabled; disabling it caused generated open62541 code to fail compilation.
- The current SD build mounts the onboard TF/microSD card and appends to `/sdcard/opclog.csv`.

Still unproven are long production-duration endurance, security, a real
PMS5003 UART sensor, and coexistence with an HTTP interface. TwinCAT browsing,
reads/writes, watch subscriptions, repeated polling, and periodic SD logging
have been demonstrated.

Interpretation: OPC UA is feasible for a focused ESP32-S3 application such as reading a PMS5003 particulate sensor, writing compact records to local SD, and exposing a small OPC UA address space over Ethernet. A larger product that also needs a rich web UI, reporting APIs, broad protocol support, and complex file/config handling should still be measured carefully because internal RAM and task-stack placement remain the key risks.

## Build And Flash

Set up ESP-IDF first. On the test machine ESP-IDF lived outside this repo:

```bash
export IDF_TOOLS_PATH=/home/pi/IchorGAT/.espressif
. /home/pi/IchorGAT/TOOLS/esp-idf/export.sh
```

Build a probe:

```bash
cd firmware/ethernet_status
idf.py build
```

Flash a probe:

```bash
idf.py -p /dev/serial/by-id/YOUR_ESP32_SERIAL_DEVICE flash monitor
```

For the board tested here, the important W5500 settings were:

```text
SCLK GPIO: 13
MOSI GPIO: 11
MISO GPIO: 12
CS GPIO:   14
INT GPIO:  10
RST GPIO:  9
SPI host:  SPI2 / host 1
Clock:     20 MHz
```

The onboard TF/microSD slot is wired as SPI on a separate bus:

```text
CS GPIO:   4
MISO GPIO: 5
MOSI GPIO: 6
SCLK GPIO: 7
SPI host:  SPI3
```

The board also required octal PSRAM configuration:

```text
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_USE_MALLOC=y
```

## Quick Tests

Current OPC UA target:

```text
Endpoint: opc.tcp://esp32-opcua.local:4840
Readable nodes:
  ns=1;s=simulated_value     expected value: 1234
  ns=1;s=firmware_status     expected value: 1
  ns=1;s=sd_status           expected value: 1
  ns=1;s=sd_append_count     increments every 5 seconds while SD logging works
  ns=1;s=reset_command       expected value: 0 except during command handling
  ns=1;s=last_command_status increments after accepted reset_command writes
  ns=1;s=internal_heap_free  current free internal heap bytes
  ns=1;s=internal_heap_min_free minimum free internal heap bytes since boot
  ns=1;s=psram_free          current free PSRAM bytes
  ns=1;s=psram_min_free      minimum free PSRAM bytes since boot
Security: None
Message security mode: None
Authentication: Anonymous
```

Ping:

```bash
ping -c 3 -W 2 esp32-opcua.local
```

If mDNS is unavailable, replace `esp32-opcua.local` with the address from the
router's DHCP lease table.

Raw TCP reachability test for OPC UA:

```bash
python3 -c 'import socket; s=socket.create_connection(("esp32-opcua.local",4840),timeout=5); print("connected"); s.close()'
```

Raw OPC UA TCP `HEL` / `ACK` handshake test:

```bash
python3 -c 'import socket,struct; url=b"opc.tcp://esp32-opcua.local:4840"; body=struct.pack("<IIIIIi",0,65535,65535,0,0,len(url))+url; msg=b"HEL"+b"F"+struct.pack("<I",8+len(body))+body; s=socket.create_connection(("esp32-opcua.local",4840),timeout=3); s.sendall(msg); print(s.recv(64)[:4]); s.close()'
```

The current `UA_NAMESPACE_ZERO=REDUCED` firmware supports standard namespace
and tree browsing. Explicit reads remain useful for automated clients:

```bash
cd /home/pi/IchorGAT/UTILS/opcua-scout
python3 runOpcuaScout.py --endpoint opc.tcp://esp32-opcua.local:4840 --read 'ns=1;s=simulated_value'
python3 runOpcuaScout.py --endpoint opc.tcp://esp32-opcua.local:4840 --read 'ns=1;s=firmware_status'
python3 runOpcuaScout.py --endpoint opc.tcp://esp32-opcua.local:4840 --read 'ns=1;s=sd_status'
python3 runOpcuaScout.py --endpoint opc.tcp://esp32-opcua.local:4840 --read 'ns=1;s=sd_append_count'
python3 runOpcuaScout.py --endpoint opc.tcp://esp32-opcua.local:4840 --read 'ns=1;s=last_command_status'
python3 runOpcuaScout.py --endpoint opc.tcp://esp32-opcua.local:4840 --read 'ns=1;s=internal_heap_free'
python3 runOpcuaScout.py --endpoint opc.tcp://esp32-opcua.local:4840 --read 'ns=1;s=psram_free'
```

If the client OS does not support mDNS or the network blocks multicast, replace
`esp32-opcua.local` with the address shown in the router's DHCP lease list.

`sd_status` values:

```text
 1  SD mounted and append logging worked
 0  SD probe not started
-1  SD SPI bus init failed
-2  SD mount failed
-3  SD append open/write failed
```

`UA_NAMESPACE_ZERO=REDUCED` with node management enabled is the current
recommended profile. It adds about 72 KB of app flash and uses about 74 KB more
internal heap than `MINIMAL`, but restores the standard `Objects` folder,
`NamespaceArray`, reference hierarchy, and browseable application nodes needed
by generic OPC UA clients.

## Lessons Learned

- Use a known-good USB data cable. A bad cable caused misleading board/port symptoms early in the investigation.
- ESP-IDF is the right environment for this class of test. Arduino IDE is fine for sanity checks, but ESP-IDF gives better access to Ethernet, FreeRTOS, memory, and component integration.
- The W5500 config must be present in the generated `sdkconfig`, not only in `sdkconfig.defaults`. ESP-IDF will not automatically override an existing generated config choice just because defaults changed.
- For this board, PSRAM must be set to octal mode. Quad PSRAM configuration caused boot failure.
- Modbus TCP is readily achievable.
- OPC UA via open62541 is feasible for a small server, but it needs an embedded profile and conscious memory choices.
- SD append logging can coexist with the current OPC UA probe. In this build, adding FatFS/SDSPI plus one status node increased app size by about 69 KB, from about 491 KB to about 560 KB, leaving 64% of the current 1,536 KB app partition free.
- Periodic SD logging plus a tiny writable command path also worked. The current command/logging build is about 562 KB and leaves 63% of the current 1,536 KB app partition free.
- Runtime heap telemetry is now exposed over OPC UA. In a 2-minute polling soak, internal heap stayed at about 287 KB free and PSRAM stayed at about 8.3 MB free while SD append count advanced.
- For command nodes, set `AccessLevel` and `UserAccessLevel` for read, write, status-write, and timestamp-write. Common clients may include timestamp/status fields in writes; without those bits, writes can fail with `BadWriteNotSupported`.
- If using `UA_Server_run_iterate()` instead of `UA_Server_run()`, explicitly call `UA_Server_run_startup()` first. Otherwise Ethernet can be alive while TCP port `4840` refuses connections.
- Current FatFS config disables long filenames, so use 8.3 filenames such as `opclog.csv` unless long filename support is enabled and measured.
- Keep OPC UA data models small. Avoid enormous generated nodesets and large in-memory history/config structures.
- Put large FreeRTOS stacks in PSRAM where possible, and keep internal RAM for code paths that truly need it.
- If OPC UA, web UI, file I/O, and reporting all matter at once, build incrementally and measure heap/stack after each service rather than assuming the ESP32-S3 is either impossible or unlimited.

## Suggested Next Steps For Anyone Continuing

- Test with UA Expert or another real OPC UA client.
- Test the current read/write nodes from UA Expert or another third-party OPC UA client.
- Add a PMS5003 UART reader and expose only a compact set of current measurements.
- Run a longer soak test with periodic SD logging and heap telemetry under OPC UA polling load.
- Move more allocations to PSRAM if possible.
- Measure long-running heap stability.
- Compare against ESP32-P4 or a Raspberry Pi-based implementation.
