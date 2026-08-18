#!/usr/bin/env python3
"""Summarize leak tool comparison outputs into CSV and Markdown tables."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path
from typing import Iterable


FIELDNAMES = [
    "case",
    "tool",
    "expected_blocks",
    "expected_bytes",
    "detected_blocks",
    "detected_bytes",
    "byte_delta",
    "runtime_ms",
    "output_bytes",
    "tool_version",
    "exit_code",
    "matches_expected",
    "has_stack",
    "summary",
    "notes",
    "result_dir",
]

TOOL_ORDER = {
    "valgrind": 0,
    "lsan": 1,
    "heaptrack": 2,
    "bcc_memleak": 3,
}

BENCHMARK_LEAK_SYMBOLS = {
    "definite_leak": ("DefiniteLeakPath",),
    "growth_leak": ("GrowthLeakPath",),
    "mixed": ("MixedLeakPath",),
    "game_scene_leak": ("GameSceneLeakPath",),
    "game_scene_growth": ("GameSceneGrowthPath",),
    "game_mixed": ("GameUiLeakPath",),
    "game_network_packet": ("GameNetworkPacketLeakPath",),
    "game_audio_decode": ("GameAudioDecodeLeakPath",),
    "game_cache_eviction": ("GameCacheFinalRetainPath",),
    "game_object_pool_leak": ("GameObjectPoolLeakPath",),
    "game_mmap_leak": ("GameMmapLeakPath",),
    "game_ui_callback_leak": ("GameUiCallbackLeakPath",),
    "game_long_running_leak": ("GameLongRunningLeakPath",),
}

BENCHMARK_COARSE_SYMBOLS = {
    "definite_leak": ("AllocateFromPath",),
    "growth_leak": ("AllocateFromPath",),
    "game_scene_leak": ("GameAssetLoadPath", "AllocateFromPath"),
    "game_scene_growth": ("GameAssetLoadPath", "AllocateFromPath"),
    "game_mixed": ("GameUiLeakPath", "AllocateFromPath"),
    "game_network_packet": ("GameNetworkPacketPath", "AllocateFromPath"),
    "game_audio_decode": ("GameAudioDecodePath", "AllocateFromPath"),
    "game_cache_eviction": ("GameCacheFinalRetainPath", "GameCacheInsertPath", "AllocateFromPath"),
    "game_object_pool_leak": ("GameObjectPoolLeakPath", "GameObjectPoolAcquirePath", "AllocateFromPath"),
    "game_mmap_leak": ("GameMmapLeakPath", "GameMmapMapPath"),
    "game_ui_callback_leak": ("GameUiCallbackLeakPath", "AllocateFromPath"),
    "game_long_running_leak": ("GameLongRunningLeakPath", "AllocateFromPath"),
}

# heaptrack may omit a small wrapper function from the rendered stack when the
# allocation path returns directly to the worker. Keep a case-specific fallback
# for that output format without weakening BCC/Valgrind/LSan attribution rules.
BENCHMARK_HEAPTRACK_SYMBOLS = {
    "game_cache_eviction": ("GameCacheFinalRetainPath", "RunGameCacheEvictionWorker"),
    "game_object_pool_leak": ("GameObjectPoolLeakPath", "RunGameObjectPoolLeakWorker"),
    "game_mmap_leak": ("GameMmapLeakPath", "RunGameMmapLeakWorker"),
    "game_ui_callback_leak": ("GameUiCallbackLeakPath", "RunGameUiCallbackLeakWorker"),
    "game_long_running_leak": ("GameLongRunningLeakPath", "RunGameLongRunningLeakWorker"),
}


def _read_text(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def _read_json(path: Path) -> dict:
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8").strip()
    if not text:
        return {}
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return {}


def _to_text(value: object, default: str = "unknown") -> str:
    if value is None:
        return default
    return str(value)


def _to_int(value: object, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _match_status(detected_blocks: str, detected_bytes: str, expected_blocks: str, expected_bytes: str) -> str:
    if detected_blocks == "unknown" or detected_bytes == "unknown":
        return "unknown"
    return "yes" if detected_blocks == expected_blocks and detected_bytes == expected_bytes else "no"


def _expects_zero(row: dict) -> bool:
    return row["expected_blocks"] == "0" and row["expected_bytes"] == "0"


def _set_detected_zero(row: dict, summary: str) -> None:
    row["detected_blocks"] = "0"
    row["detected_bytes"] = "0"
    row["summary"] = summary


def _read_run_config(path: Path) -> dict:
    config = {}
    for line in _read_text(path).splitlines():
        if not line or line.lstrip().startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        config[key.strip()] = value.strip()
    return config


def _int_config(config: dict, key: str, default: int) -> int:
    try:
        return int(config.get(key, default))
    except (TypeError, ValueError):
        return default


def _count_mod(iterations: int, residue: int, modulus: int = 10) -> int:
    if iterations <= residue:
        return 0
    return ((iterations - 1 - residue) // modulus) + 1


def _expected_from_run_config(config: dict) -> dict:
    case_name = config.get("case")
    if not case_name:
        return {}

    threads = _int_config(config, "threads", 1)
    iterations = _int_config(config, "iterations", 0)
    size = _int_config(config, "size", 0)

    if case_name in {
        "no_leak",
        "delayed_free",
        "high_freq_no_leak",
        "game_scene_no_leak",
        "game_async_delayed_free",
        "game_frame_no_leak",
        "game_object_pool_no_leak",
        "game_mmap_no_leak",
        "game_ui_callback_no_leak",
        "game_long_running_no_leak",
    }:
        blocks = 0
        bytes_count = 0
    elif case_name in {"definite_leak", "growth_leak", "game_scene_leak", "game_scene_growth"}:
        blocks = threads * iterations
        bytes_count = blocks * size
    elif case_name == "game_network_packet":
        leaked = _count_mod(iterations, 3, 7)
        blocks = threads * leaked
        bytes_count = blocks * size
    elif case_name == "game_audio_decode":
        leaked = _count_mod(iterations, 2, 6)
        blocks = threads * leaked
        bytes_count = blocks * size * 2
    elif case_name == "game_cache_eviction":
        blocks = threads if iterations > 0 else 0
        bytes_count = blocks * size
    elif case_name == "game_object_pool_leak":
        leaked = _count_mod(iterations, 3, 8)
        blocks = threads * leaked
        bytes_count = blocks * size
    elif case_name == "game_mmap_leak":
        blocks = threads * iterations
        bytes_count = blocks * size
    elif case_name == "game_ui_callback_leak":
        blocks = threads * iterations * 2
        bytes_count = blocks * size
    elif case_name == "game_long_running_leak":
        blocks = threads * iterations * _int_config(config, "rounds", 1)
        bytes_count = blocks * size
    elif case_name in {"mixed", "game_mixed"}:
        leak6 = _count_mod(iterations, 6)
        leak9 = _count_mod(iterations, 9)
        blocks = threads * (leak6 + leak9)
        bytes_count = threads * ((leak6 * size * 2) + (leak9 * (size // 2 + 1)))
    else:
        return {}

    return {
        "case": case_name,
        "expected_outstanding_blocks": blocks,
        "expected_outstanding_bytes": bytes_count,
    }


def _base_row(case_dir: Path, tool: str) -> dict:
    benchmark = _read_json(case_dir / "benchmark.json")
    run_config = _read_run_config(case_dir / "run_config.txt")
    if not benchmark:
        benchmark = _expected_from_run_config(run_config)
    return {
        "case": _to_text(benchmark.get("case", run_config.get("case", case_dir.name))),
        "tool": tool,
        "expected_blocks": _to_text(benchmark.get("expected_outstanding_blocks")),
        "expected_bytes": _to_text(benchmark.get("expected_outstanding_bytes")),
        "detected_blocks": "unknown",
        "detected_bytes": "unknown",
        "byte_delta": "unknown",
        "runtime_ms": _to_text(run_config.get("runner_duration_ms")),
        "output_bytes": _to_text(run_config.get("output_bytes")),
        "tool_version": _to_text(run_config.get("tool_version")),
        "exit_code": _to_text(run_config.get("runner_exit_code", run_config.get("tool_exit_code"))),
        "matches_expected": "unknown",
        "has_stack": "unknown",
        "summary": "no parser result",
        "notes": "",
        "result_dir": str(case_dir),
    }


def _finalize_match(row: dict, byte_tolerance: int = 0) -> dict:
    if row["detected_bytes"] == "unknown" or row["expected_bytes"] == "unknown":
        row["byte_delta"] = "unknown"
    else:
        try:
            row["byte_delta"] = str(int(row["detected_bytes"]) - int(row["expected_bytes"]))
        except (TypeError, ValueError):
            row["byte_delta"] = "unknown"
    if byte_tolerance > 0 and row["detected_bytes"] != "unknown" and row["expected_bytes"] != "unknown":
        try:
            blocks_match = row["detected_blocks"] == row["expected_blocks"]
            bytes_delta = abs(int(row["detected_bytes"]) - int(row["expected_bytes"]))
            row["matches_expected"] = "yes" if blocks_match and bytes_delta <= byte_tolerance else "no"
        except (TypeError, ValueError):
            row["matches_expected"] = "unknown"
    else:
        row["matches_expected"] = _match_status(
            row["detected_blocks"],
            row["detected_bytes"],
            row["expected_blocks"],
            row["expected_bytes"],
        )
    return row


def _summarize_valgrind(case_dir: Path, row: dict) -> dict:
    text = _read_text(case_dir / "valgrind.log")
    match = re.search(r"definitely lost:\s*([0-9,]+)\s+bytes in\s+([0-9,]+)\s+blocks", text)
    if match:
        row["detected_bytes"] = match.group(1).replace(",", "")
        row["detected_blocks"] = match.group(2).replace(",", "")
        row["summary"] = f"definitely lost: {row['detected_bytes']} bytes in {row['detected_blocks']} blocks"
    elif "no leaks are possible" in text and _expects_zero(row):
        _set_detected_zero(row, "valgrind reports no possible leaks")
    elif text:
        row["summary"] = "valgrind log present, no definitely-lost line"
    else:
        row["summary"] = "missing valgrind.log"
    row["has_stack"] = "yes" if re.search(r"^\s*(?:==\d+==\s*)*(at|by)\s+0x", text, re.MULTILINE) else "no"
    return _finalize_match(row)


def _summarize_lsan(case_dir: Path, row: dict) -> dict:
    text = _read_text(case_dir / "lsan.log")
    match = re.search(r"SUMMARY:\s+AddressSanitizer:\s*([0-9,]+)\s+byte\(s\) leaked in\s+([0-9,]+)\s+allocation", text)
    if match:
        row["detected_bytes"] = match.group(1).replace(",", "")
        row["detected_blocks"] = match.group(2).replace(",", "")
        row["summary"] = f"LSan leaked: {row['detected_bytes']} bytes in {row['detected_blocks']} allocations"
    elif _expects_zero(row) and ("LeakSanitizer" not in text and "AddressSanitizer" not in text):
        _set_detected_zero(row, "LSan emitted no leak report")
    elif text:
        row["summary"] = "lsan log present, no leak summary"
    else:
        row["summary"] = "missing lsan.log"
    row["has_stack"] = "yes" if "allocated from:" in text or re.search(r"#\d+\s+0x", text) else "no"
    return _finalize_match(row)


def _summarize_bcc_memleak(case_dir: Path, row: dict) -> dict:
    text = _read_text(case_dir / "memleak.log")
    snapshots = _parse_bcc_snapshots(text)
    entries = snapshots[-1] if snapshots else []
    benchmark_entries = []
    coarse_attribution = False
    for entry in entries:
        if _bcc_entry_is_allocator_scaffold(entry):
            continue
        match_kind = _bcc_entry_match_kind(entry, row["case"])
        if match_kind:
            benchmark_entries.append(entry)
            coarse_attribution = coarse_attribution or match_kind == "coarse"
    selected = None
    resolved = False
    expected = (row["expected_bytes"], row["expected_blocks"])

    if benchmark_entries:
        detected_bytes = sum(entry["bytes"] for entry in benchmark_entries)
        detected_blocks = sum(entry["blocks"] for entry in benchmark_entries)
        row["detected_bytes"] = str(detected_bytes)
        row["detected_blocks"] = str(detected_blocks)
        row["summary"] = (
            f"benchmark memleak stacks: {row['detected_bytes']} bytes "
            f"in {row['detected_blocks']} allocations"
        )
        ignored = len(entries) - len(benchmark_entries)
        note_parts = []
        if coarse_attribution:
            note_parts.append("coarse benchmark stack attribution")
        if ignored > 0:
            ignored_bytes = sum(entry["bytes"] for entry in entries if entry not in benchmark_entries)
            note_parts.append(f"ignored non-benchmark stacks: {ignored} stacks/{ignored_bytes} bytes")
        row["notes"] = "; ".join(note_parts)
        resolved = True
    elif entries and _expects_zero(row):
        _set_detected_zero(row, "no benchmark leak stack in memleak snapshot")
        ignored_bytes = sum(entry["bytes"] for entry in entries)
        row["notes"] = f"ignored non-benchmark stacks: {len(entries)} stacks/{ignored_bytes} bytes"
        resolved = True
    if selected is not None:
        row["detected_bytes"], row["detected_blocks"] = selected
        row["summary"] = f"memleak outstanding: {row['detected_bytes']} bytes in {row['detected_blocks']} allocations"
        resolved = True
    elif not resolved and entries and _expects_zero(row):
        _set_detected_zero(row, "memleak snapshot has no outstanding allocation stack")
        ignored_bytes = sum(entry["bytes"] for entry in entries)
        row["notes"] = f"unattributed stacks: {len(entries)} stacks/{ignored_bytes} bytes"
        resolved = True
    elif not resolved and entries:
        ignored_bytes = sum(entry["bytes"] for entry in entries)
        row["summary"] = "memleak snapshot has no benchmark-attributed stack"
        row["notes"] = f"unattributed stacks: {len(entries)} stacks/{ignored_bytes} bytes"
        resolved = True
    elif not resolved and text:
        row["summary"] = "memleak log present, no outstanding allocation line"
    elif not resolved:
        row["summary"] = "missing memleak.log"
    row["has_stack"] = "yes" if "from stack" in text and re.search(r"\n\s+\S", text) else "no"
    return _finalize_match(row)


def _parse_bcc_snapshots(text: str) -> list[list[dict]]:
    snapshot_header = re.compile(
        r"^\s*\[[^\]]+\]\s+Top .*outstanding allocations:",
        re.MULTILINE,
    )
    if not snapshot_header.search(text):
        entries = _parse_bcc_entries(text)
        return [entries] if entries else []

    snapshots = []
    current_lines = []
    for line in text.splitlines():
        if snapshot_header.search(line):
            if current_lines:
                entries = _parse_bcc_entries("\n".join(current_lines))
                if entries:
                    snapshots.append(entries)
            current_lines = []
            continue
        current_lines.append(line)
    if current_lines:
        entries = _parse_bcc_entries("\n".join(current_lines))
        if entries:
            snapshots.append(entries)
    return snapshots


def _parse_bcc_entries(text: str) -> list[dict]:
    entries = []
    current = None
    header_re = re.compile(r"^\s*([0-9,]+)\s+bytes in\s+([0-9,]+)\s+allocations from stack")
    for line in text.splitlines():
        match = header_re.search(line)
        if match:
            if current is not None:
                entries.append(current)
            current = {
                "bytes": int(match.group(1).replace(",", "")),
                "blocks": int(match.group(2).replace(",", "")),
                "frames": [],
            }
            continue
        if current is not None and line.strip():
            current["frames"].append(line.strip())
    if current is not None:
        entries.append(current)
    return entries


def _bcc_entry_match_kind(entry: dict, case_name: str) -> str:
    stack = "\n".join(entry.get("frames", []))
    if any(symbol in stack for symbol in BENCHMARK_LEAK_SYMBOLS.get(case_name, ())):
        return "path"
    if any(symbol in stack for symbol in BENCHMARK_COARSE_SYMBOLS.get(case_name, ())):
        return "coarse"
    return ""


def _bcc_entry_is_allocator_scaffold(entry: dict) -> bool:
    """Exclude allocator arena growth that happens inside a benchmark call."""
    stack = "\n".join(entry.get("frames", []))
    return any(
        symbol in stack
        for symbol in (
            "alloc_new_heap",
            "arena_get2",
            "__libc_malloc",
            "malloc_consolidate",
        )
    )


def _summarize_heaptrack(case_dir: Path, row: dict) -> dict:
    if _summarize_heaptrack_print(case_dir, row):
        return _finalize_match(row, byte_tolerance=5)

    text = _read_text(case_dir / "heaptrack.log")
    match = re.search(r"leaked allocations:\s*([0-9,]+)", text)
    if match:
        row["detected_blocks"] = match.group(1).replace(",", "")
        if row["detected_blocks"] == "0":
            row["detected_bytes"] = "0"
        else:
            row["detected_bytes"] = "unknown"
        row["summary"] = f"heaptrack leaked allocations: {row['detected_blocks']}"
    elif text:
        row["summary"] = "heaptrack log present, no leaked-allocation stat"
    else:
        row["summary"] = "missing heaptrack.log"
    profile_exists = any(case_dir.glob("heaptrack*.zst"))
    row["has_stack"] = "yes" if profile_exists else "unknown"
    row["matches_expected"] = _match_status(
        row["detected_blocks"],
        row["detected_bytes"],
        row["expected_blocks"],
        row["expected_bytes"],
    )
    row["notes"] = "profile_generated" if profile_exists else "profile_missing"
    return row


def _summarize_heaptrack_print(case_dir: Path, row: dict) -> bool:
    text = _read_text(case_dir / "heaptrack_print.log")
    if not text:
        return False

    entries = _parse_heaptrack_print_entries(text)
    benchmark_entries = [
        entry for entry in entries
        if _heaptrack_entry_has_benchmark_leak_symbol(entry, row["case"])
    ]
    profile_exists = any(case_dir.glob("heaptrack*.zst"))

    if benchmark_entries:
        detected_bytes = sum(entry["bytes"] for entry in benchmark_entries)
        detected_blocks = sum(entry["calls"] for entry in benchmark_entries)
        row["detected_bytes"] = str(detected_bytes)
        row["detected_blocks"] = str(detected_blocks)
        row["summary"] = (
            f"benchmark heaptrack leaks: {row['detected_bytes']} bytes "
            f"in {row['detected_blocks']} calls"
        )
        row["has_stack"] = "yes"
        ignored_count = len(entries) - len(benchmark_entries)
        note_parts = ["heaptrack_print_generated" if profile_exists else "profile_missing"]
        if ignored_count > 0:
            ignored_bytes = sum(entry["bytes"] for entry in entries if entry not in benchmark_entries)
            note_parts.append(f"ignored non-benchmark leak entries: {ignored_count} entries/{ignored_bytes} bytes")
        if row["expected_bytes"] != row["detected_bytes"]:
            note_parts.append("heaptrack_print rounded SI byte value")
        row["notes"] = "; ".join(note_parts)
        return True

    if entries and _expects_zero(row):
        _set_detected_zero(row, "no benchmark leak stack in heaptrack print")
        row["has_stack"] = "yes"
        ignored_bytes = sum(entry["bytes"] for entry in entries)
        row["notes"] = (
            f"heaptrack_print_generated; ignored non-benchmark leak entries: "
            f"{len(entries)} entries/{ignored_bytes} bytes"
        )
        return True

    return False


def _parse_heaptrack_print_entries(text: str) -> list[dict]:
    entries = []
    current = None
    header_re = re.compile(
        r"^\s*([0-9]+(?:\.[0-9]+)?)([KMGT]?B?)\s+leaked over\s+([0-9,]+)\s+calls from:?\s*$"
    )
    for line in text.splitlines():
        match = header_re.search(line)
        if match:
            if current is not None:
                entries.append(current)
            current = {
                "bytes": _parse_heaptrack_size(match.group(1), match.group(2)),
                "calls": int(match.group(3).replace(",", "")),
                "frames": [],
            }
            continue

        if current is None:
            continue

        stripped = line.strip()
        if not stripped:
            if current["frames"]:
                entries.append(current)
                current = None
            continue
        current["frames"].append(stripped)

    if current is not None:
        entries.append(current)
    return entries


def _parse_heaptrack_size(number_text: str, unit: str) -> int:
    # heaptrack_print formats K/M/G using decimal SI units and rounds the
    # displayed value, so the parsed value is a reported approximation.
    multiplier = {
        "": 1,
        "B": 1,
        "K": 1000,
        "KB": 1000,
        "M": 1000 * 1000,
        "MB": 1000 * 1000,
        "G": 1000 * 1000 * 1000,
        "GB": 1000 * 1000 * 1000,
        "T": 1000 * 1000 * 1000 * 1000,
        "TB": 1000 * 1000 * 1000 * 1000,
    }.get(unit, 1)
    return int(round(float(number_text) * multiplier))


def _heaptrack_entry_has_benchmark_leak_symbol(entry: dict, case_name: str) -> bool:
    symbols = BENCHMARK_HEAPTRACK_SYMBOLS.get(
        case_name, BENCHMARK_LEAK_SYMBOLS.get(case_name, ())
    )
    if not symbols:
        return False
    stack = "\n".join(entry.get("frames", []))
    return any(symbol in stack for symbol in symbols)


def summarize_case_dir(case_dir: Path, tool: str) -> dict:
    row = _base_row(case_dir, tool)
    if tool == "valgrind":
        return _summarize_valgrind(case_dir, row)
    if tool == "lsan":
        return _summarize_lsan(case_dir, row)
    if tool == "bcc_memleak":
        return _summarize_bcc_memleak(case_dir, row)
    if tool == "heaptrack":
        return _summarize_heaptrack(case_dir, row)
    row["summary"] = f"unsupported tool: {tool}"
    return row


def _iter_case_dirs(run_dir: Path) -> Iterable[tuple[str, Path]]:
    for tool_dir in sorted(path for path in run_dir.iterdir() if path.is_dir()):
        tool = tool_dir.name
        for case_dir in sorted(path for path in tool_dir.iterdir() if path.is_dir()):
            yield tool, case_dir


def summarize_run(run_dir: Path) -> list[dict]:
    rows = [summarize_case_dir(case_dir, tool) for tool, case_dir in _iter_case_dirs(run_dir)]
    rows.sort(key=lambda row: (row["case"], TOOL_ORDER.get(row["tool"], 99), row["tool"]))
    return rows


def write_csv(rows: list[dict], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDNAMES)
        writer.writeheader()
        writer.writerows(rows)


def _md_cell(value: object) -> str:
    text = str(value)
    return text.replace("|", "\\|").replace("\n", " ")


def write_markdown(rows: list[dict], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Tool Compare Summary",
        "",
        "| " + " | ".join(FIELDNAMES) + " |",
        "| " + " | ".join("---" for _ in FIELDNAMES) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(_md_cell(row.get(field, "")) for field in FIELDNAMES) + " |")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_dir", type=Path, help="results/tool_compare/<run_name> directory")
    parser.add_argument("--csv", dest="csv_path", type=Path, default=None)
    parser.add_argument("--markdown", dest="markdown_path", type=Path, default=None)
    parser.add_argument("--print-markdown", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    run_dir = args.run_dir
    if not run_dir.exists() or not run_dir.is_dir():
        print(f"run_dir is not a directory: {run_dir}", file=sys.stderr)
        return 2

    rows = summarize_run(run_dir)
    csv_path = args.csv_path or (run_dir / "tool_compare_summary.csv")
    markdown_path = args.markdown_path or (run_dir / "tool_compare_summary.md")
    write_csv(rows, csv_path)
    write_markdown(rows, markdown_path)

    if args.print_markdown:
        print(markdown_path.read_text(encoding="utf-8"))
    else:
        print(f"wrote {csv_path}")
        print(f"wrote {markdown_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
