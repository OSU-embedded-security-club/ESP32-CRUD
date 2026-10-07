import os
import pty
import sys
import threading
 
import serial
 
SYNC = 0xA5
STATUSES = {0: "OK", 1: "BAD_PIN", 2: "LOCKED", 3: "BAD_SLOT", 4: "EMPTY", 5: "BAD_REQUEST"}
 
 
class FdIO:
    """Wraps a raw PTY fd so it looks like a pyserial object (read(n)/write(bytes))."""
 
    def __init__(self, fd):
        self.fd = fd
 
    def read(self, n):
        buf = b""
        while len(buf) < n:
            chunk = os.read(self.fd, n - len(buf))
            if not chunk:
                raise EOFError("fake port closed")
            buf += chunk
        return buf
 
    def write(self, data):
        os.write(self.fd, data)
 
 
def relay(src, dst, n=1):
    """Read n bytes from src, mirror them to dst unchanged, return what was read."""
    data = src.read(n)
    dst.write(data)
    return data
 
 
def sync_up(src, dst):
    """Forward bytes one at a time until a SYNC byte is seen."""
    while True:
        if relay(src, dst, 1)[0] == SYNC:
            return
 
 
def proxy_host_to_board(host, board):
    while True:
        sync_up(host, board)
        op = relay(host, board)[0]
        pin_len = relay(host, board)[0]
        pin = relay(host, board, pin_len).decode(errors="replace")
 
        if op == 3:  # LIST
            print(f"[host->board] LIST  pin={pin!r}")
        elif op == 2:  # READ
            slot = relay(host, board)[0]
            print(f"[host->board] READ  pin={pin!r} slot={slot}")
        elif op == 1:  # WRITE
            slot = relay(host, board)[0]
            length = relay(host, board)[0]
            data = relay(host, board, length) if length else b""
            print(f"[host->board] WRITE pin={pin!r} slot={slot} len={length} data={data!r}")
        else:
            print(f"[host->board] op={op} pin={pin!r} (unrecognized opcode)")
 
 
def proxy_board_to_host(board, host):
    while True:
        sync_up(board, host)
        status = relay(board, host)[0]
        length = relay(board, host)[0]
        data = relay(board, host, length) if length else b""
        print(f"[board->host] status={STATUSES.get(status, status)} len={length} data={data!r}")
 
 
def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} REAL_PORT")
 
    board = serial.Serial(sys.argv[1], 115200, timeout=None)
    master_fd, slave_fd = pty.openpty()
    fake_port = os.ttyname(slave_fd)
    print(f"fake port ready at {fake_port}")
    print(f"point tool.py at {fake_port} instead of {sys.argv[1]}\n")
 
    host = FdIO(master_fd)
 
    threading.Thread(target=proxy_host_to_board, args=(host, board), daemon=True).start()
    threading.Thread(target=proxy_board_to_host, args=(board, host), daemon=True).start()
 
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
 
 
if __name__ == "__main__":
    main()
