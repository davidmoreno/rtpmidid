"""Golden tests for scripts/dm_json_gen.py (stdlib only, no pytest)."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "dm_json_gen.py"
FIXTURE = Path(__file__).parent / "fixtures" / "minimal.hpp"
EXPECTED_H = Path(__file__).parent / "expected" / "dm_json_generated.hpp"
EXPECTED_CPP = Path(__file__).parent / "expected" / "dm_json_generated.cpp"


class TestDmJsonGenGolden(unittest.TestCase):
    def test_generator_matches_golden(self):
        import tempfile

        self.assertTrue(FIXTURE.is_file(), msg=str(FIXTURE))
        with tempfile.TemporaryDirectory() as td:
            td_path = Path(td)
            subprocess.run(
                [
                    "python3",
                    str(SCRIPT),
                    "--out-dir",
                    str(td_path),
                    "--header",
                    "dm_json_generated.hpp",
                    "--source",
                    "dm_json_generated.cpp",
                    str(FIXTURE),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            got_h = (td_path / "dm_json_generated.hpp").read_text()
            got_cpp = (td_path / "dm_json_generated.cpp").read_text()
        self.assertEqual(got_h, EXPECTED_H.read_text())
        self.assertEqual(got_cpp, EXPECTED_CPP.read_text())


if __name__ == "__main__":
    unittest.main()
