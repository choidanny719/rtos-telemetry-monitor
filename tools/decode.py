import json
import re
import struct
import sys

FLAGS = ("valid", "overheat", "stale", "sensor_error", "overflow")


def decode_frame(identifier, data):
    if identifier != 0x321 or not isinstance(data, str) or not re.fullmatch(r"[0-9a-fA-F]{16}", data):
        raise ValueError("expected CAN ID 0x321 and eight hexadecimal bytes")
    temperature, flags, sequence, timestamp = struct.unpack(">hBBI", bytes.fromhex(data))
    if not -2048 <= temperature <= 2047 or flags & ~31:
        raise ValueError("reserved flags or temperature outside normal TMP102 range")
    return {
        "timestamp_ms": timestamp,
        "temperature_c": temperature / 16,
        "flags": [name for bit, name in enumerate(FLAGS) if flags & (1 << bit)],
        "sequence": sequence,
    }


def main():
    for number, line in enumerate(sys.stdin, 1):
        try:
            record = json.loads(line)
            if record["type"] == "frame":
                print(json.dumps(decode_frame(record["id"], record["data"])))
            elif record["type"] == "summary":
                print(json.dumps(record))
            else:
                raise ValueError("unknown record type")
        except (KeyError, TypeError, ValueError) as error:
            print(f"Invalid telemetry at line {number}: {error}", file=sys.stderr)
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
