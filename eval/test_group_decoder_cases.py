"""Run with python3 -m unittest eval.test_group_decoder_cases."""

from pathlib import Path
import tempfile
import unittest

from eval.group_decoder_cases import ARTIFACTS, group_cases


class DecoderGroupingTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def case(self, level):
        case = {"input": "random", "level": level}
        for name in ARTIFACTS:
            path = self.root / f"{name}-{level}"
            path.write_bytes(name.encode())
            case[name] = path
        return case

    def test_identical_content_at_different_paths_groups(self):
        groups = group_cases([self.case(2), self.case(3)])
        self.assertEqual(len(groups), 1)
        self.assertEqual([m["level"] for m in groups[0]["members"]], [2, 3])

    def test_any_artifact_difference_keeps_cases_separate(self):
        for name in ARTIFACTS:
            with self.subTest(artifact=name):
                first, second = self.case(2), self.case(3)
                # Same length, different bytes: sizes alone must not group it.
                second[name].write_bytes(b"X" * len(name))
                self.assertEqual(len(group_cases([first, second])), 2)

    def test_missing_artifact_fails_instead_of_inventing_identity(self):
        case = self.case(2)
        case["encoded"].unlink()
        with self.assertRaises(FileNotFoundError):
            group_cases([case])

    def test_duplicate_case_is_rejected(self):
        case = self.case(2)
        with self.assertRaisesRegex(ValueError, "duplicate"):
            group_cases([case, case])


if __name__ == "__main__":
    unittest.main()
