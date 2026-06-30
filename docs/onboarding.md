# Developer Onboarding

This guide gets a new development machine from a fresh clone to a running
firmware build on the Waveshare ESP32-S3-ETH. The existing experiment log and
architecture notes explain why the project uses these settings; this document
focuses on the repeatable workflow.

## What Is In The Repository

The main working firmware is:

```text
firmware/opcua_probe
```

It currently provides:

- W5500 Ethernet with DHCP
- DHCP hostname `esp32-opcua`
- mDNS hostname `esp32-opcua.local`
- OPC UA on TCP port `4840`
- Browse-compatible open62541 namespace zero
- Basic OPC UA data-change subscriptions
- Periodic logging to the onboard microSD card
- Runtime heap telemetry

The generated open62541 source is vendored under
`firmware/opcua_probe/components/open62541`. A normal build does not require
cloning or regenerating open62541.

## Hardware Preparation

Required:

- Waveshare ESP32-S3-ETH
- USB data cable, not a charge-only cable
- Ethernet cable
- A FAT-formatted microSD card for the logging probe

Connect USB to the development machine for power, flashing, and serial output.
Connect Ethernet to a DHCP-capable LAN for normal testing.

The board has BOOT and RESET buttons. Automatic USB flashing normally works. If
it does not, hold BOOT, tap RESET, release RESET, and then release BOOT before
retrying the flash.

## Install ESP-IDF On Debian Or Raspberry Pi OS

The proven toolchain is ESP-IDF v5.4.2. Python 3.9 worked; Python 3.13 is not
required.

Install the common prerequisites:

```bash
sudo apt-get update
sudo apt-get install -y git wget flex bison gperf python3 python3-pip \
  python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
  dfu-util libusb-1.0-0
```

Choose tool locations outside the repository:

```bash
mkdir -p "$HOME/esp"
cd "$HOME/esp"
git clone --recursive --branch v5.4.2 \
  https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

Load ESP-IDF in every new shell:

```bash
export IDF_TOOLS_PATH="$HOME/.espressif"
. "$HOME/esp/esp-idf/export.sh"
```

The original Raspberry Pi installation used these equivalent custom paths:

```bash
export IDF_TOOLS_PATH=/home/pi/IchorGAT/.espressif
. /home/pi/IchorGAT/TOOLS/esp-idf/export.sh
```

Do not mix tool installations accidentally. `idf.py --version` should report
ESP-IDF v5.4.x before building.

## Windows Setup

Use Espressif's ESP-IDF Windows installer and select ESP-IDF v5.4.x, Git,
Python, and the ESP32-S3 tools. Build from the installed **ESP-IDF PowerShell**
or **ESP-IDF Command Prompt**, not an ordinary terminal that has not loaded the
ESP-IDF environment.

The project commands below are otherwise the same. Replace Linux serial paths
with the board's `COM` port, for example `-p COM7`.

## Clone And Build

From the repository root:

```bash
cd firmware/opcua_probe
idf.py build
```

The repository already carries the tested ESP32-S3 configuration. Do not run
`idf.py set-target` during ordinary onboarding because it can regenerate
`sdkconfig`.

The first build downloads the official `espressif/mdns` managed component. The
downloaded `managed_components`, dependency lock, and build output are ignored
by Git and may be recreated.

Expected successful-build characteristics are approximately:

```text
Target:                  esp32s3
App image:               677 KB
App partition remaining: 859 KB / 56%
```

Small differences from compiler or ESP-IDF patch versions are normal. Large
differences deserve investigation.

## Find The Serial Port

Linux:

```bash
ls -l /dev/serial/by-id/
ls -l /dev/ttyACM*
```

The test board appeared as:

```text
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:53:12:20-if00
```

If no device appears, first suspect the USB cable. Confirm enumeration with:

```bash
lsusb
dmesg --ctime | tail -n 30
```

On Linux, serial permission errors may require adding the current user to the
`dialout` group and then logging out and back in:

```bash
sudo usermod -aG dialout "$USER"
```

## Flash And Monitor

Using the stable by-id path is preferable:

```bash
idf.py -p /dev/serial/by-id/YOUR_ESP32_DEVICE flash
idf.py -p /dev/serial/by-id/YOUR_ESP32_DEVICE monitor
```

Or use the current device name:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with `Ctrl+]`.

If configuration changes appear not to take effect:

```bash
idf.py fullclean
idf.py reconfigure
idf.py build
```

Do not delete or casually replace `sdkconfig`; it contains measured board
settings. `sdkconfig.defaults` captures important defaults, but an existing
generated `sdkconfig` remains authoritative.

## Network Verification

The current firmware uses DHCP. It does not use the early test address
`192.168.50.2`.

Find the board in the router's DHCP lease table:

```text
DHCP hostname: esp32-opcua
Ethernet MAC:  2a:84:85:53:12:20
```

The ESP32 base MAC shown by the flashing tool is
`28:84:85:53:12:20`. The W5500 Ethernet interface uses
`2a:84:85:53:12:20`; use the W5500 address for DHCP reservations.

On networks supporting multicast DNS:

```bash
ping esp32-opcua.local
```

The OPC UA endpoint is:

```text
opc.tcp://esp32-opcua.local:4840
```

If `.local` resolution is blocked, substitute the numeric DHCP address.

## OPC UA Verification

Current connection settings:

```text
Security policy: None
Security mode:   None
Authentication:  Anonymous
```

Expected application nodes appear beside the standard `Server` object:

```text
ns=1;s=simulated_value
ns=1;s=firmware_status
ns=1;s=sd_status
ns=1;s=sd_append_count
ns=1;s=reset_command
ns=1;s=last_command_status
ns=1;s=internal_heap_free
ns=1;s=internal_heap_min_free
ns=1;s=psram_free
ns=1;s=psram_min_free
```

The standard `Server` subtree is OPC UA infrastructure. It is expected and is
needed by general-purpose clients.

Known-good client behavior includes:

- Endpoint loading
- Namespace and tree browsing
- Direct reads and writes
- Data-change subscriptions and watch lists
- TwinCAT OPC UA Sample Client browsing and watch operation

For Python testing, install the classic `opcua` client in a virtual environment:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install opcua
```

A minimal direct read:

```python
from opcua import Client

client = Client("opc.tcp://esp32-opcua.local:4840")
client.connect()
print(client.get_node("ns=1;s=simulated_value").get_value())
client.disconnect()
```

## SD Verification

The firmware mounts the onboard card at `/sdcard` and appends a compact record
to:

```text
/sdcard/opclog.csv
```

Because FatFS long filenames are disabled, keep log filenames in 8.3 form.

OPC UA status values:

```text
sd_status = 1   mounted and logging
sd_status = 0   not started
sd_status = -1  SPI bus initialization failed
sd_status = -2  mount failed
sd_status = -3  append failed
```

`sd_append_count` should increase every five seconds.

## Board-Specific Settings

W5500 Ethernet:

```text
SCLK GPIO 13
MOSI GPIO 11
MISO GPIO 12
CS   GPIO 14
INT  GPIO 10
RST  GPIO 9
SPI2 / host 1 at 20 MHz
```

Onboard microSD:

```text
CS   GPIO 4
MISO GPIO 5
MOSI GPIO 6
SCLK GPIO 7
SPI3 at 10 MHz
```

PSRAM must use octal mode. The 64 KB OPC UA task stack is deliberately
allocated from PSRAM.

The open62541 component must define both:

```text
UA_ARCHITECTURE_LWIP
UA_ARCHITECTURE_FREERTOS
```

Omitting the FreeRTOS definition breaks cyclic subscription timers even though
ordinary reads and writes continue to work.

## What To Measure After Changes

Build success alone is not enough. After adding a meaningful feature:

1. Record firmware size and app-partition space.
2. Read current and minimum internal heap through OPC UA.
3. Read current and minimum PSRAM.
4. Confirm SD append count advances.
5. Browse the OPC UA tree.
6. Perform a direct read and write.
7. Confirm a monitored-item subscription receives updates.
8. Run a longer soak for changes involving networking, storage, or concurrency.

Internal heap is the main constrained resource. The current subscription build
has about 203 KB free internal heap and 8.29 MB free PSRAM in a short
single-client test.

## Further Reading

- `opcua-experiment-log.md`: chronological experiments, failures, and proven fixes
- `open62541-build-notes.md`: exact open62541 generation profile
- `application-architecture-notes.md`: capacity estimates and product guidance
- Repository `README.md`: protocol overview and quick tests
