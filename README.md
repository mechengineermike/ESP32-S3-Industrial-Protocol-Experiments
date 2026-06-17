# ESP32-S3 Industrial Protocol Experiments

This repository contains feasibility probes for running industrial Ethernet protocols on a Waveshare ESP32-S3-ETH board.

The main result: the board is useful for lightweight Ethernet and Modbus TCP experiments. OPC UA Is pushing this device to the limit, it appears that it will work but leaves absolutely 0 room for any other services at all.

## Hardware Tested

- Waveshare ESP32-S3-ETH
- ESP32-S3, dual core, 240 MHz
- 8 MB PSRAM
- 16 MB flash
- W5500 Ethernet
- USB-C serial/JTAG programming
- Direct RJ45 connection to a Raspberry Pi reTerminal DM

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

Important constraints observed:

- The OPC UA firmware image was large: about `0x15e1a0` bytes.
- The default large single-app partition had only about 7% free after the OPC UA probe.
- A 24 KB OPC UA task stack crashed during open62541 namespace-zero initialization.
- A 64 KB OPC UA task stack allowed the firmware to stay up and heartbeat.
- Internal heap after open62541 startup was very tight, roughly 31 KB free in the observed run.
- PSRAM remained mostly free, but the limiting resource appeared to be internal RAM and overall firmware complexity.

What remains unproven:

- A successful OPC UA client connection to port `4840`.
- Reading/writing the sample `SimulatedValue` node from UA Expert, CODESYS, TwinCAT, or a Python OPC UA client.
- Stability under repeated OPC UA polling.
- Coexistence with HTTP, SD card access, JSON parsing, and other application services.

Interpretation: OPC UA may be possible as a focused demonstration, but this board is not a comfortable target for an application that also needs a web UI, file I/O, mission/config parsing, reporting APIs, and multiple protocol stacks.

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

The board also required octal PSRAM configuration:

```text
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_USE_MALLOC=y
```

## Quick Tests

Ping:

```bash
ping -c 3 -W 2 192.168.50.2
```

HTTP status:

```bash
curl http://192.168.50.2/
```

Raw TCP reachability test for OPC UA:

```bash
python3 -c 'import socket; s=socket.create_connection(("192.168.50.2",4840),timeout=5); print("connected"); s.close()'
```

## Lessons Learned

- Use a known-good USB data cable. A bad cable caused misleading board/port symptoms early in the investigation.
- ESP-IDF is the right environment for this class of test. Arduino IDE is fine for sanity checks, but ESP-IDF gives better access to Ethernet, FreeRTOS, memory, and component integration.
- The W5500 config must be present in the generated `sdkconfig`, not only in `sdkconfig.defaults`. ESP-IDF will not automatically override an existing generated config choice just because defaults changed.
- For this board, PSRAM must be set to octal mode. Quad PSRAM configuration caused boot failure.
- Modbus TCP is readily achievable.
- OPC UA via open62541 is possible to build, but it consumes enough flash, stack, and internal heap that it is not a comfortable foundation for a larger multi-service product on this board.
- If OPC UA, web UI, file I/O, and reporting all matter at once, a Raspberry Pi-class Linux target is likely a better fit.

## Suggested Next Steps For Anyone Continuing

- Confirm OPC UA port `4840` accepts TCP connections.
- Test with UA Expert or another real OPC UA client.
- Try an even smaller open62541 configuration.
- Move more allocations to PSRAM if possible.
- Measure long-running heap stability.
- Compare against ESP32-P4 or a Raspberry Pi-based implementation.

