"""Knock a TinyUSB build into the ROM bootloader before uploading.

esptool resets the board by toggling DTR/RTS on the serial port. The hardware
USB-JTAG bridge implements that; a TinyUSB CDC port does not, so an upload to
the USB-drive build fails with "No serial data received". Opening the port at
1200 baud is the classic Arduino bootloader knock, and the ESP32-S3 core honours
it -- the device reboots and comes back on its ROM port.
"""
import time
from pathlib import Path

Import("env")  # noqa: F821  (injected by PlatformIO)


def _ports():
    return sorted(str(p) for p in Path("/dev").glob("cu.usbmodem*"))


def before_upload(source, target, env):  # noqa: ARG001
    try:
        import serial
    except ImportError:
        print("usb_touch: pyserial unavailable, skipping")
        return

    before = _ports()
    if not before:
        print("usb_touch: no cu.usbmodem* port found, skipping")
        return

    for port in before:
        try:
            s = serial.Serial(port, 1200)
            s.dtr = False
            time.sleep(0.3)
            s.close()
            print(f"usb_touch: knocked {port} at 1200 baud")
        except Exception as exc:  # noqa: BLE001 - a closed port is not fatal
            print(f"usb_touch: {port}: {exc}")

    # The device re-enumerates on its ROM port, which may have a different name.
    for _ in range(20):
        time.sleep(0.5)
        now = _ports()
        if now and now != before:
            print(f"usb_touch: bootloader port is {now[0]}")
            env.Replace(UPLOAD_PORT=now[0])
            return
    if _ports():
        env.Replace(UPLOAD_PORT=_ports()[0])


env.AddPreAction("upload", before_upload)  # noqa: F821
