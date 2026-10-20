import json
import subprocess
import sys

result = subprocess.run(
    [sys.argv[1], "--duration-ms", "500"], capture_output=True, text=True, timeout=10, check=True
)
records = [json.loads(line) for line in result.stdout.splitlines()]
assert all(record["id"] == 0x321 and len(record["data"]) == 16 for record in records[:-1])
summary = records[-1]
assert summary["type"] == "summary"
assert summary["acquired"] == summary["processed"] >= 3
assert summary["dropped"] == summary["rejected"] == summary["read_errors"] == 0
assert summary["frames"] == len(records) - 1 >= 5
assert summary["processing_cycles"] >= summary["processed"]
