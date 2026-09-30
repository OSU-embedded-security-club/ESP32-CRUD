#!/usr/bin/env python3
"""
Host tool for the ESP32-C3 slot store. This is the only way to talk to the board.

  python tool.py PORT list  PIN
  python tool.py PORT write PIN SLOT infile
  python tool.py PORT read  PIN SLOT outfile

Requires: pip install pyserial
"""
import argparse
import sys
import time

import serial

# Message Protocol
SYNC = 0xA5

OP_WRITE = 1
OP_READ = 2
OP_LIST = 3

STATUS_MESSAGES = {
    0: "OK",
    1: "Incorrect PIN",
    2: "Too many failed attempts",
    3: "Bad slot",
    4: "Slot is empty",
    5: "Bad request",
}

SLOT_COUNT = 5
MAX_DATA = 255
PIN_MAX = 15


class DeviceError(Exception):
    pass


class Board:
    def __init__(self, port, timeout=3):
        self.ser = serial.Serial(port, 115200, timeout=timeout)
        time.sleep(0.2)
        self.ser.reset_input_buffer()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.ser.close()

    def _read_exact(self, n):
        data = self.ser.read(n)
        if len(data) != n:
            raise DeviceError("timeout waiting for device")
        return data

    def _wait_for_sync(self):
        while True:
            b = self.ser.read(1)
            if not b:
                raise DeviceError("timeout waiting for device")
            if b[0] == SYNC:
                return

    def request(self, op, pin, payload=b""):
        """Send one request, return the response data. Raises DeviceError on failure."""
        pin_bytes = pin.encode()
        if not 1 <= len(pin_bytes) <= PIN_MAX:
            raise DeviceError(f"PIN must be 1-{PIN_MAX} characters")

        self.ser.reset_input_buffer()
        self.ser.write(bytes([SYNC, op, len(pin_bytes)]) + pin_bytes + payload)

        self._wait_for_sync()
        status, length = self._read_exact(2)
        data = self._read_exact(length)

        if status != 0:
            raise DeviceError(STATUS_MESSAGES.get(status, f"unknown status {status}"))
        return data

    # ---- commands ----

    def list(self, pin):
        return list(self.request(OP_LIST, pin))

    def write(self, pin, slot, data):
        if len(data) > MAX_DATA:
            raise DeviceError(f"data is {len(data)} bytes; max is {MAX_DATA}")
        self.request(OP_WRITE, pin, bytes([slot, len(data)]) + data)

    def read(self, pin, slot):
        return self.request(OP_READ, pin, bytes([slot]))


# ---- CLI ----

def slot_arg(value):
    try:
        slot = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError(f"invalid slot '{value}'")
    if not 0 <= slot < SLOT_COUNT:
        raise argparse.ArgumentTypeError(f"slot must be 0-{SLOT_COUNT - 1}")
    return slot


def build_parser():
    p = argparse.ArgumentParser(description="ESP32-C3 slot store tool")
    p.add_argument("port", help="serial port, e.g. /dev/cu.usbmodem101")
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("list", help="show slot usage")
    s.add_argument("pin")

    s = sub.add_parser("write", help="write a file into a slot")
    s.add_argument("pin")
    s.add_argument("slot", type=slot_arg)
    s.add_argument("file")

    s = sub.add_parser("read", help="read a slot into a file")
    s.add_argument("pin")
    s.add_argument("slot", type=slot_arg)
    s.add_argument("file")

    return p


def main():
    args = build_parser().parse_args()

    try:
        with Board(args.port) as board:
            if args.cmd == "list":
                print("Files:")
                for i, length in enumerate(board.list(args.pin)):
                    desc = f"slot_{i} ({length} bytes)" if length else "<empty>"
                    print(f"  [{i}] {desc}")

            elif args.cmd == "write":
                with open(args.file, "rb") as f:
                    data = f.read()
                board.write(args.pin, args.slot, data)
                if data:
                    print(f"Wrote {len(data)} bytes to slot {args.slot}")
                else:
                    print(f"Cleared slot {args.slot}")

            elif args.cmd == "read":
                data = board.read(args.pin, args.slot)
                with open(args.file, "wb") as f:
                    f.write(data)
                print(f"Read {len(data)} bytes from slot {args.slot} -> {args.file}")

    except (DeviceError, serial.SerialException, OSError) as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()