import argparse
import sys
import time
import serial


def main():
    parser = argparse.ArgumentParser(
        description="Send 'list' command with PIN to ESP32 over serial."
    )
    parser.add_argument("port", help="Serial port (e.g., /dev/ttyACM0 or COM3)")
    parser.add_argument(
        "--baud", type=int, default=115200, help="Baud rate (default: 115200)"
    )
    args = parser.parse_args()

    try:
        with serial.Serial(args.port, args.baud, timeout=1.0) as ser:
            # Short pause to let the connection stabilize
            time.sleep(0.5)

            ser.reset_input_buffer()
            loop_start_time = time.time()
            for pin_int in range(10000):
                pin = f"{pin_int:04d}"
                command = f"list {pin}\n"
            
                ser.write(command.encode("utf-8"))

                response = ""
                start_time = time.time()

                while (time.time() - start_time) < 0.5:
                    if ser.in_waiting:
                        chunk = ser.read(ser.in_waiting).decode(
                            "utf-8", errors="ignore"
                        )
                        response += chunk
                        if "> " in chunk or "Files:" in chunk:
                            break

                print(f"\rTesting PIN: {pin}", end="", flush=True)

                if "Files:" in response:
                    time.sleep(0.2)
                    if ser.in_waiting:
                        response += ser.read(ser.in_waiting).decode(
                            "utf-8", errors="ignore"
                        )

                    print(f"\n\n PIN found: {pin}\n")
                    print("Device Output:")
                    print(response.strip() + "\n")
                    return

    except serial.SerialException as e:
        print(f"Serial Port Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
