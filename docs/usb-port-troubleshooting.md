# USB And Flashing Notes

The most important USB lesson from this investigation was simple: use a known-good USB data cable.

An inadequate cable produced confusing symptoms:

- the board appeared powered,
- LEDs stayed on,
- button presses did not appear to change USB state,
- the board did not reliably appear as a serial/programming port.

After switching to a good USB data cable, the ESP32-S3 appeared reliably as an Espressif USB serial/JTAG device.

On the Raspberry Pi used for testing, the stable serial path was:

```text
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:53:12:20-if00
```

On another machine, discover the port with:

```bash
ls /dev/serial/by-id/
```

or:

```bash
dmesg -w
```

Typical flash command:

```bash
idf.py -p /dev/serial/by-id/YOUR_ESP32_SERIAL_DEVICE flash monitor
```

The board has BOOT and RESET buttons. Most flashes worked through the normal ESP-IDF reset sequence once the good cable was used. If auto-reset fails, hold BOOT while resetting or reconnecting, then release BOOT when the flash tool starts connecting.

