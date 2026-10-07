Import("env")

import hashlib
import os
import secrets
from pathlib import Path

def c_array(data):
    """Format bytes as C initializer rows, 8 per line."""
    rows = []
    for i in range(0, len(data), 8):
        rows.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 8]))
    return ",\n".join(rows)

pin = env.GetProjectOption("device_pin")

if not pin:
    raise RuntimeError("DEVICE_PIN environment variable not set")

pin_hash = hashlib.sha256(pin.encode()).digest()

aes_key = secrets.token_bytes(32)

output = Path(env["PROJECT_DIR"]) / "include" / "secrets.h"

output.write_text(f"""#pragma once

#include <stdint.h>

static const uint8_t PIN_HASH[32] = {{
    {c_array(pin_hash)}
}};

static const uint8_t AES_KEY[32] = {{
    {c_array(aes_key)}
}};
""")

print("Generated secrets.h")