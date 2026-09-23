Import("env")

import hashlib
import os
from pathlib import Path

pin = os.environ.get("DEVICE_PIN")

if not pin:
    raise RuntimeError("DEVICE_PIN environment variable not set")

digest = hashlib.sha256(pin.encode()).digest()

pin_hash = ", ".join(f"0x{byte:02x}" for byte in digest)

output = Path(env["PROJECT_DIR"]) / "include" / "secrets.h"

output.write_text(f"""#pragma once

#include <stdint.h>

static const uint8_t PIN_HASH[32] = {{
    {pin_hash}
}};
""")

print("Generated secrets.h")