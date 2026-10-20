import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(sys.argv.pop(1)).resolve()
SPEC = importlib.util.spec_from_file_location("decoder", ROOT / "tools" / "decode.py")
decoder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(decoder)


def run_demo(*arguments):
    result = subprocess.run([str(BINARY), *arguments], capture_output=True, text=True, timeout=15)
    if result.returncode:
        raise AssertionError(f"demo exited with {result.returncode}: {result.stderr}")
    records = [json.loads(line) for line in result.stdout.splitlines()]
    summary = records[-1]
    frames = []
    for record in records[:-1]:
        if record["type"] != "frame" or record["id"] != 0x321:
            raise AssertionError(record)
        temperature, flags, sequence, timestamp = struct.unpack(">hBBI", bytes.fromhex(record["data"]))
        frames.append({"temperature": temperature, "flags": flags, "sequence": sequence, "at": timestamp})
    return frames, summary, result.stdout


class SchedulerTests(unittest.TestCase):
    def assert_run_consistent(self, frames, summary):
        self.assertEqual(summary["type"], "summary")
        self.assertEqual(summary["acquired"], summary["processed"] + summary["dropped"])
        self.assertGreaterEqual(summary["processed"], summary["rejected"])
        self.assertGreaterEqual(summary["processing_cycles"], summary["processed"])
        self.assertEqual(summary["frames"], len(frames))
        self.assertLessEqual(summary["max_queue_depth"], 4)
        self.assertEqual(summary["history_count"], min(16, len(frames)))
        self.assertEqual(summary["history_oldest_ms"], frames[-min(16, len(frames))]["at"])
        self.assertGreater(len(frames), 3)
        for index, frame in enumerate(frames):
            self.assertEqual(frame["sequence"], index % 256)
            self.assertEqual(frame["flags"] & ~31, 0)
            if index:
                self.assertGreaterEqual(frame["at"], frames[index - 1]["at"])

    def test_normal_readings_cross_the_entire_pipeline(self):
        frames, summary, output = run_demo("--scenario", str(ROOT / "scenarios/normal.csv"), "--duration-ms", "2000")
        self.assert_run_consistent(frames, summary)
        self.assertEqual({400, 480, -160}, {frame["temperature"] for frame in frames})
        self.assertTrue(all(frame["flags"] == 1 for frame in frames))
        self.assertEqual(summary["dropped"], 0)
        self.assertEqual(summary["rejected"], 0)
        self.assertEqual(summary["read_errors"], 0)
        decoded = subprocess.run([sys.executable, str(ROOT / "tools/decode.py")], input=output,
                                 capture_output=True, text=True, timeout=5, check=True)
        self.assertIn('"temperature_c": -10.0', decoded.stdout)
        self.assertEqual(json.loads(decoded.stdout.splitlines()[-1]), summary)

    def test_short_spike_does_not_latch_overheat(self):
        frames, summary, _ = run_demo("--scenario", str(ROOT / "scenarios/spike.csv"), "--duration-ms", "1200")
        self.assert_run_consistent(frames, summary)
        self.assertTrue(any(frame["temperature"] == 1440 for frame in frames))
        self.assertTrue(all(frame["flags"] == 1 for frame in frames))

    def test_sustained_heat_trips_then_recovers(self):
        frames, summary, _ = run_demo("--scenario", str(ROOT / "scenarios/overheat.csv"))
        self.assert_run_consistent(frames, summary)
        alarms = [frame for frame in frames if frame["flags"] & 2]
        self.assertTrue(alarms)
        self.assertGreaterEqual(alarms[0]["at"], 800)
        self.assertTrue(any(frame["temperature"] == 1120 for frame in alarms))
        self.assertEqual(frames[-1]["flags"], 1)
        self.assertEqual(frames[-1]["temperature"], 1120)

    def test_bus_faults_become_stale_and_recover(self):
        frames, summary, _ = run_demo("--scenario", str(ROOT / "scenarios/disconnected.csv"), "--duration-ms", "2100")
        self.assert_run_consistent(frames, summary)
        self.assertTrue(any(frame["flags"] & 8 for frame in frames))
        self.assertTrue(any(frame["flags"] & 4 for frame in frames))
        self.assertTrue(all(frame["temperature"] == 400 for frame in frames))
        self.assertGreater(summary["read_errors"], 0)
        self.assertEqual(frames[-1]["flags"], 1)

    def test_failed_sensor_from_start_never_reports_valid_data(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "failed.csv"
            path.write_text("at_ms,temperature_q4,status\n0,0,timeout\n")
            frames, summary, _ = run_demo("--scenario", str(path), "--duration-ms", "800")
        self.assert_run_consistent(frames, summary)
        self.assertTrue(all(frame["flags"] & 1 == 0 for frame in frames))
        self.assertEqual(frames[-1]["flags"], 12)
        self.assertEqual(summary["read_errors"], summary["acquired"])

    def test_queue_overload_counts_drops_and_rejects_stale_backlog(self):
        frames, summary, _ = run_demo("--duration-ms", "1500", "--processing-delay-ms", "500")
        self.assert_run_consistent(frames, summary)
        self.assertGreater(summary["dropped"], 0)
        self.assertGreater(summary["rejected"], 0)
        self.assertEqual(summary["max_queue_depth"], 4)
        self.assertTrue(any(frame["flags"] & 16 for frame in frames))
        self.assertTrue(any(frame["flags"] & 4 for frame in frames))
        self.assertEqual(frames[-1]["flags"] & 16, 16)


class CommandLineTests(unittest.TestCase):
    def test_help_is_available_without_starting_the_scheduler(self):
        result = subprocess.run([str(BINARY), "--help"], capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 0)
        self.assertIn("Usage:", result.stdout)
        self.assertNotIn('"type":"frame"', result.stdout)

    def test_invalid_arguments_fail_before_starting_tasks(self):
        cases = [
            ["--unknown"], ["--duration-ms"], ["--scenario"], ["--processing-delay-ms"],
            ["--duration-ms", "499"], ["--duration-ms", "60001"], ["--duration-ms", "-1"],
            ["--duration-ms", "0"], ["--duration-ms", "1000ms"], ["--duration-ms", "1000.5"],
            ["--duration-ms", "99999999999999999999999999"], ["--duration-ms", "+1000"],
            ["--processing-delay-ms", "1001"], ["--processing-delay-ms", "-1"],
            ["--duration-ms", "500", "--duration-ms", "500"],
            ["--processing-delay-ms", "0", "--processing-delay-ms", "0"],
            ["--scenario", str(ROOT / "scenarios/normal.csv"), "--scenario", "other"],
            ["--scenario", "/missing-directory-for-telemetry-test/input.csv"],
            ["--scenario", str(ROOT)], ["--help", "extra"], ["--unknown", "value"]
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(BINARY), *arguments], capture_output=True, text=True, timeout=3)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertTrue(result.stderr)

    def test_invalid_csv_is_rejected_by_the_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.csv"
            path.write_text("at_ms,temperature_q4,status\n0,400,ok\n0,400,ok\n")
            result = subprocess.run([str(BINARY), "--scenario", str(path)], capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("Invalid scenario:", result.stderr)


class DecoderTests(unittest.TestCase):
    def test_golden_vector(self):
        self.assertEqual(decoder.decode_frame(0x321, "fe7003ff01020304"), {
            "timestamp_ms": 0x01020304, "temperature_c": -25.0,
            "flags": ["valid", "overheat"], "sequence": 255,
        })

    def test_limits_and_all_flags(self):
        self.assertEqual(decoder.decode_frame(0x321, "f8001f00ffffffff"), {
            "timestamp_ms": 0xffffffff, "temperature_c": -128.0,
            "flags": list(decoder.FLAGS), "sequence": 0,
        })
        self.assertEqual(decoder.decode_frame(0x321, "07ff000000000000")["temperature_c"], 127.9375)

    def test_malformed_frames(self):
        for identifier, data in [(0x322, "0000000000000000"), ("801", "0000000000000000"),
                                 (0x321, ""), (0x321, "00"), (0x321, "g" * 16),
                                 (0x321, "00 " * 8), (0x321, None), (0x321, "0000200000000000"),
                                 (0x321, "0800000000000000"), (0x321, "f7ff000000000000")]:
            with self.subTest(identifier=identifier, data=data):
                with self.assertRaises(ValueError):
                    decoder.decode_frame(identifier, data)

    def test_decoder_reports_bad_json_records(self):
        cases = ["not json\n", '{}\n', '[]\n', '{"type":"frame"}\n', '{"type":"unknown"}\n']
        for content in cases:
            with self.subTest(content=content):
                result = subprocess.run([sys.executable, str(ROOT / "tools/decode.py")],
                                        input=content, capture_output=True, text=True, timeout=3)
                self.assertEqual(result.returncode, 2)
                self.assertIn("Invalid telemetry at line 1:", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
