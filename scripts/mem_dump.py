import argparse
 
import serial
 
SYNC = 0xA5
OP_READ = 2
STATUS_OK = 0
 
 
def read_chunk(ser, pin, slot, length):
    pin_bytes = pin.encode()
    ser.reset_input_buffer()
    ser.write(bytes([SYNC, OP_READ, len(pin_bytes)]) + pin_bytes + bytes([slot, length]))
 
    while True:
        b = ser.read(1)
        if not b:
            raise TimeoutError("no response")
        if b[0] == SYNC:
            break
    status = ser.read(1)[0]
    resp_len = ser.read(1)[0]
    data = ser.read(resp_len)
    return data if status == STATUS_OK else None
 
 
def printable(data):
    return "".join(chr(b) if 32 <= b < 127 else "." for b in data)
 
 
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("pin")
    ap.add_argument("--slots", type=int, default=20,
                     help="how many 'slot' indices to walk past the real SLOT_COUNT")
    ap.add_argument("--chunk", type=int, default=255, help="bytes to request per index (max 255)")
    args = ap.parse_args()
 
    ser = serial.Serial(args.port, 115200, timeout=3)
 
    for slot in range(args.slots):
        data = read_chunk(ser, args.pin, slot, args.chunk)
        if data is None:
            print(f"slot {slot}: rejected")
            continue
        print(f"slot {slot}: {data.hex()}")
        print(f"          {printable(data)}")

if __name__ == "__main__":
    main()