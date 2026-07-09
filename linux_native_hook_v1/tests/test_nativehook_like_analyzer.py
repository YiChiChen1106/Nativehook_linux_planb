import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from nativehook_like_analyzer import analyze_lines, render_markdown_report


class NativehookLikeAnalyzerTest(unittest.TestCase):
    def test_matches_alloc_free_and_reports_outstanding_by_stack(self):
        lines = [
            "VERBOSE,0,101,0x1000,64,7,10,100,11,leak_case_A",
            "VERBOSE,0,102,0x2000,32,7,11,200,12,leak_case_B",
            "VERBOSE,1,103,0x1000,0,7,12,300,0,",
            "VERBOSE,1,104,0x3000,0,7,13,400,0,",
            "VERBOSE,0,101,0x4000,128,7,14,500,11,leak_case_A",
        ]

        report = analyze_lines(lines)

        self.assertEqual(report["summary"]["alloc_count"], 3)
        self.assertEqual(report["summary"]["free_count"], 2)
        self.assertEqual(report["summary"]["matched_free_count"], 1)
        self.assertEqual(report["summary"]["unmatched_free_count"], 1)
        self.assertEqual(report["summary"]["outstanding_count"], 2)
        self.assertEqual(report["summary"]["outstanding_bytes"], 160)

        by_key = {item["group_key"]: item for item in report["statistics"]}
        self.assertEqual(by_key["stack:11"]["apply_count"], 2)
        self.assertEqual(by_key["stack:11"]["release_count"], 1)
        self.assertEqual(by_key["stack:11"]["outstanding_count"], 1)
        self.assertEqual(by_key["stack:11"]["outstanding_size"], 128)
        self.assertEqual(by_key["stack:12"]["outstanding_size"], 32)

        outstanding = {item["addr"]: item for item in report["outstanding_allocations"]}
        self.assertEqual(outstanding["0x4000"]["callsite"], "leak_case_A")
        self.assertEqual(outstanding["0x2000"]["tid"], 102)

    def test_parses_stackmap_and_attaches_frames_to_statistics(self):
        report = analyze_lines([
            "STACKMAP,42,3,0x401000,0x402000,0x403000",
            "VERBOSE,0,101,0x1000,64,7,10,0,42,0x401000",
            "VERBOSE,0,101,0x2000,32,7,11,0,42,0x401000",
            "VERBOSE,1,101,0x1000,0,7,12,0,42,0x401000",
        ])

        self.assertEqual(report["summary"]["stack_map_count"], 1)
        self.assertEqual(report["stack_maps"]["42"], ["0x401000", "0x402000", "0x403000"])
        by_key = {item["group_key"]: item for item in report["statistics"]}
        self.assertEqual(by_key["stack:42"]["frames"], ["0x401000", "0x402000", "0x403000"])
        self.assertEqual(by_key["stack:42"]["outstanding_size"], 32)

    def test_legacy_verbose_without_stack_falls_back_to_tid_grouping(self):
        lines = [
            "consumer listening on /tmp/nativehook.sock",
            "VERBOSE,0,201,4096,16,9,20,0",
            "VERBOSE,1,201,4096,0,9,21,0",
            "VERBOSE,0,202,8192,24,9,22,0",
        ]

        report = analyze_lines(lines)

        self.assertEqual(report["summary"]["outstanding_count"], 1)
        self.assertEqual(report["summary"]["outstanding_bytes"], 24)
        self.assertEqual(report["statistics"][0]["group_key"], "tid:202")
        self.assertEqual(report["statistics"][0]["outstanding_size"], 24)

    def test_markdown_contains_nativehook_like_sections(self):
        report = analyze_lines([
            "VERBOSE,0,101,0x1000,64,7,10,0,5,case_one",
        ])

        markdown = render_markdown_report(report)

        self.assertIn("# nativehook-like leak analysis", markdown)
        self.assertIn("Outstanding allocations: 1", markdown)
        self.assertIn("| stack:5 | case_one | 1 | 0 | 64 |", markdown)

    def test_cli_writes_json_and_markdown_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            log_path = tmp_path / "verbose.log"
            json_path = tmp_path / "report.json"
            md_path = tmp_path / "report.md"
            log_path.write_text("VERBOSE,0,1,0x10,8,2,1,0,3,cli_case\n", encoding="utf-8")

            result = subprocess.run(
                [
                    sys.executable,
                    str(TOOLS / "nativehook_like_analyzer.py"),
                    str(log_path),
                    "--json",
                    str(json_path),
                    "--markdown",
                    str(md_path),
                ],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads(json_path.read_text(encoding="utf-8"))
            self.assertEqual(payload["summary"]["outstanding_bytes"], 8)
            self.assertIn("cli_case", md_path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
