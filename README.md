# ESP32-S3 Industrial Protocol Experiments

This repository contains feasibility probes for running industrial Ethernet protocols on a Waveshare ESP32-S3-ETH board.

The main result: the board is useful for lightweight Ethernet and Modbus TCP experiments, and a small OPC UA server is feasible when open62541 and ESP-IDF are configured for an embedded target. The earlier pessimistic OPC UA result came from a heavier open62541 profile, debug-oriented settings, and a large task stack placed in internal RAM.

The current OPC UA probe also mounts the onboard TF/microSD card, appends one compact CSV line, and exposes that result through OPC UA. This means the repo now has a small but real proof that Ethernet OPC UA and SD logging can coexist on this board.

For the detailed OPC UA investigation trail, including settings that were proven too heavy or incompatible, see [`docs/opcua-experiment-log.md`](docs/opcua-experiment-log.md).

For notes on shaping this into a PMS5003 + SD logging + OPC UA gateway, see [`docs/application-architecture-notes.md`](docs/application-architecture-notes.md).

## Hardware Tested

- Waveshare ESP32-S3-ETH
- ESP32-S3, dual core, 240 MHz
- 8 MB PSRAM
- 16 MB flash
- W5500 Ethernet
- USB-C serial/JTAG programming
- Direct RJ45 connection to a Raspberry Pi reTerminal DM
- Onboard TF/microSD slot tested with an 8 GB card

The test network used static IP addresses:

- Raspberry Pi Ethernet: `192.168.50.1`
- ESP32 Ethernet: `192.168.50.2`

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
- The tuned build uses open62541 `MINIMAL` namespace zero and disables features that are not needed for a tiny sensor server: encryption, subscriptions, historizing, discovery, method calls, node management, diagnostics, JSON, XML, PubSub, and status-code descriptions.
- `UA_ENABLE_TYPEDESCRIPTION` had to remain enabled; disabling it caused generated open62541 code to fail compilation.
- The current SD build mounts the onboard TF/microSD card and appends to `/sdcard/opclog.csv`.

What remains unproven:

- Reading/writing from UA Expert, CODESYS, TwinCAT, or other third-party OPC UA clients.
- Stability under repeated OPC UA polling.
- Periodic SD logging under polling load.
- Coexistence with HTTP, JSON parsing, and other application services.

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

Current OPC UA scout target:

```text
Endpoint: opc.tcp://192.168.50.2:4840
Readable nodes:
  ns=1;s=simulated_value     expected value: 1234
  ns=1;s=firmware_status     expected value: 1
  ns=1;s=sd_status           expected value: 1
Security: None
Message security mode: None
Authentication: Anonymous
```

Ping:

```bash
ping -c 3 -W 2 192.168.50.2
```

For a direct cable test, make sure the Pi-side Ethernet interface is also on the probe subnet. On the test Pi, `eth0` initially only had a `169.254.x.x` link-local address. This temporary command made the ESP32 reachable:

```bash
sudo ip addr add 192.168.50.1/24 dev eth0
```

HTTP status:

```bash
curl http://192.168.50.2/
```

Raw TCP reachability test for OPC UA:

```bash
python3 -c 'import socket; s=socket.create_connection(("192.168.50.2",4840),timeout=5); print("connected"); s.close()'
```

Raw OPC UA TCP `HEL` / `ACK` handshake test:

```bash
python3 -c 'import socket,struct; url=b"opc.tcp://192.168.50.2:4840"; body=struct.pack("<IIIIIi",0,65535,65535,0,0,len(url))+url; msg=b"HEL"+b"F"+struct.pack("<I",8+len(body))+body; s=socket.create_connection(("192.168.50.2",4840),timeout=3); s.sendall(msg); print(s.recv(64)[:4]); s.close()'
```

Using `opcuaScout` with the current `UA_NAMESPACE_ZERO=MINIMAL` firmware, prefer explicit reads. Namespace and tree discovery are sparse in this embedded profile:

```bash
cd /home/pi/IchorGAT/UTILS/opcua-scout
python3 runOpcuaScout.py --endpoint opc.tcp://192.168.50.2:4840 --read 'ns=1;s=simulated_value'
python3 runOpcuaScout.py --endpoint opc.tcp://192.168.50.2:4840 --read 'ns=1;s=firmware_status'
python3 runOpcuaScout.py --endpoint opc.tcp://192.168.50.2:4840 --read 'ns=1;s=sd_status'
```

`sd_status` values:

```text
 1  SD mounted and append logging worked
 0  SD probe not started
-1  SD SPI bus init failed
-2  SD mount failed
-3  SD append open/write failed
```

`UA_NAMESPACE_ZERO=REDUCED` was tested as a possible discovery-friendly profile. It added about 71 KB of app flash compared with `MINIMAL`, but the Python OPC UA client failed during session creation with the current aggressive feature cuts. For now, explicit NodeId reads are the recommended client strategy.

## Lessons Learned

- Use a known-good USB data cable. A bad cable caused misleading board/port symptoms early in the investigation.
- ESP-IDF is the right environment for this class of test. Arduino IDE is fine for sanity checks, but ESP-IDF gives better access to Ethernet, FreeRTOS, memory, and component integration.
- The W5500 config must be present in the generated `sdkconfig`, not only in `sdkconfig.defaults`. ESP-IDF will not automatically override an existing generated config choice just because defaults changed.
- For this board, PSRAM must be set to octal mode. Quad PSRAM configuration caused boot failure.
- Modbus TCP is readily achievable.
- OPC UA via open62541 is feasible for a small server, but it needs an embedded profile and conscious memory choices.
- SD append logging can coexist with the current OPC UA probe. In this build, adding FatFS/SDSPI plus one status node increased app size by about 69 KB, from about 491 KB to about 560 KB, leaving 64% of the current 1,536 KB app partition free.
- Current FatFS config disables long filenames, so use 8.3 filenames such as `opclog.csv` unless long filename support is enabled and measured.
- Keep OPC UA data models small. Avoid enormous generated nodesets and large in-memory history/config structures.
- Put large FreeRTOS stacks in PSRAM where possible, and keep internal RAM for code paths that truly need it.
- If OPC UA, web UI, file I/O, and reporting all matter at once, build incrementally and measure heap/stack after each service rather than assuming the ESP32-S3 is either impossible or unlimited.

## Suggested Next Steps For Anyone Continuing

- Test with UA Expert or another real OPC UA client.
- Read/write the `SimulatedValue` node from a real OPC UA client.
- Add a PMS5003 UART reader and expose only a compact set of current measurements.
- Extend SD logging from boot-time append to periodic sample logging and measure heap/stack again under polling load.
- Move more allocations to PSRAM if possible.
- Measure long-running heap stability.
- Compare against ESP32-P4 or a Raspberry Pi-based implementation.
