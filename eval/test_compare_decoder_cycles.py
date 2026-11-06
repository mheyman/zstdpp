"""Tests for perf CSV validation; run from the repository root with unittest."""

from pathlib import Path
import tempfile
import unittest

from eval.compare_decoder_cycles import counters


class CounterParsingTests(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "counters.stat"
            path.write_text(text)
            return counters(path)

    @staticmethod
    def sample(value="123", fraction="100.00"):
        return "# perf output\n\n" + "".join(
            f"{value},,{event}:u,100000,{fraction},,\n"
            for event in ("cycles", "instructions", "branches", "branch-misses"))

    def test_valid_csv_with_user_event_suffix(self):
        self.assertEqual(self.parse(self.sample())["cycles"], 123)

    def test_unavailable_counter_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unavailable"):
            self.parse(self.sample("<not supported>"))

    def test_multiplexed_counter_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "multiplexed"):
            self.parse(self.sample(fraction="75.00"))

    def test_missing_counter_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "missing"):
            self.parse("123,,cycles:u,100000,100.00,,\n")


if __name__ == "__main__":
    unittest.main()
