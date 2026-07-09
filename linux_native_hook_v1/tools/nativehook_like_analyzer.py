#!/usr/bin/env python3
"""Analyze Plan B verbose records with nativehook-like matching semantics.

Input format:
  VERBOSE,type,tid,addr,size,pid,tv_sec,tv_nsec
  VERBOSE,type,tid,addr,size,pid,tv_sec,tv_nsec,stack_id,callsite

Event type 0 is malloc/apply, event type 1 is free/release.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


MALLOC_TYPE = 0
FREE_TYPE = 1


@dataclass(frozen=True)
class VerboseEvent:
    event_type: int
    tid: int
    addr: int
    size: int
    pid: int
    tv_sec: int
    tv_nsec: int
    stack_id: int = 0
    callsite: str = ""

    @property
    def timestamp_ns(self) -> int:
        return (self.tv_sec * 1_000_000_000) + self.tv_nsec

    @property
    def addr_hex(self) -> str:
        return f"0x{self.addr:x}"


def _parse_int(text: str) -> int:
    value = text.strip()
    if value.lower().startswith("0x"):
        return int(value, 16)
    return int(value, 10)


def parse_verbose_line(line: str) -> VerboseEvent | None:
    text = line.strip()
    if not text or not text.startswith("VERBOSE,"):
        return None

    parts = [part.strip() for part in text.split(",")]
    if len(parts) < 8:
        raise ValueError(f"invalid VERBOSE line with {len(parts)} columns: {line.rstrip()}")

    stack_id = _parse_int(parts[8]) if len(parts) >= 9 and parts[8] else 0
    callsite = parts[9] if len(parts) >= 10 else ""

    return VerboseEvent(
        event_type=_parse_int(parts[1]),
        tid=_parse_int(parts[2]),
        addr=_parse_int(parts[3]),
        size=_parse_int(parts[4]),
        pid=_parse_int(parts[5]),
        tv_sec=_parse_int(parts[6]),
        tv_nsec=_parse_int(parts[7]),
        stack_id=stack_id,
        callsite=callsite,
    )


def parse_stackmap_line(line: str) -> tuple[int, list[str]] | None:
    text = line.strip()
    if not text or not text.startswith("STACKMAP,"):
        return None

    parts = [part.strip() for part in text.split(",")]
    if len(parts) < 3:
        raise ValueError(f"invalid STACKMAP line with {len(parts)} columns: {line.rstrip()}")

    stack_id = _parse_int(parts[1])
    depth = _parse_int(parts[2])
    frames = parts[3:3 + depth]
    return stack_id, frames


def _group_key(event: VerboseEvent) -> str:
    if event.stack_id > 0:
        return f"stack:{event.stack_id}"
    if event.callsite:
        return f"callsite:{event.callsite}"
    return f"tid:{event.tid}"


def _new_stat(event: VerboseEvent, key: str) -> dict:
    return {
        "group_key": key,
        "pid": event.pid,
        "tid": event.tid,
        "stack_id": event.stack_id,
        "callsite": event.callsite,
        "apply_count": 0,
        "release_count": 0,
        "apply_size": 0,
        "release_size": 0,
        "outstanding_count": 0,
        "outstanding_size": 0,
    }


def _event_to_record(event: VerboseEvent, end_ns: int | None = None) -> dict:
    record = {
        "addr": event.addr_hex,
        "size": event.size,
        "pid": event.pid,
        "tid": event.tid,
        "stack_id": event.stack_id,
        "callsite": event.callsite,
        "ts_sec": event.tv_sec,
        "ts_nsec": event.tv_nsec,
    }
    if end_ns is not None:
        record["age_ns"] = max(0, end_ns - event.timestamp_ns)
    return record


def analyze_lines(lines: Iterable[str]) -> dict:
    pending: dict[int, VerboseEvent] = {}
    stats: dict[str, dict] = {}
    stack_maps: dict[int, list[str]] = {}
    unmatched_frees: list[dict] = []
    duplicate_allocations: list[dict] = []
    alloc_count = 0
    free_count = 0
    matched_free_count = 0
    ignored_null_free_count = 0
    last_timestamp_ns = 0

    for line_number, line in enumerate(lines, start=1):
        stack_map = parse_stackmap_line(line)
        if stack_map is not None:
            stack_id, frames = stack_map
            stack_maps[stack_id] = frames
            continue

        event = parse_verbose_line(line)
        if event is None:
            continue
        last_timestamp_ns = max(last_timestamp_ns, event.timestamp_ns)

        if event.event_type == MALLOC_TYPE:
            alloc_count += 1
            if event.addr == 0:
                continue
            key = _group_key(event)
            stat = stats.setdefault(key, _new_stat(event, key))
            stat["apply_count"] += 1
            stat["apply_size"] += event.size

            previous = pending.get(event.addr)
            if previous is not None:
                duplicate_allocations.append({
                    "line": line_number,
                    "previous": _event_to_record(previous),
                    "replacement": _event_to_record(event),
                })
            pending[event.addr] = event
        elif event.event_type == FREE_TYPE:
            free_count += 1
            if event.addr == 0:
                ignored_null_free_count += 1
                continue
            allocation = pending.pop(event.addr, None)
            if allocation is None:
                unmatched_frees.append({"line": line_number, **_event_to_record(event)})
                continue
            matched_free_count += 1
            key = _group_key(allocation)
            stat = stats.setdefault(key, _new_stat(allocation, key))
            stat["release_count"] += 1
            stat["release_size"] += allocation.size

    outstanding = [_event_to_record(event, last_timestamp_ns) for event in pending.values()]
    outstanding.sort(key=lambda item: (-item["size"], item["addr"]))

    for event in pending.values():
        key = _group_key(event)
        stat = stats.setdefault(key, _new_stat(event, key))
        stat["outstanding_count"] += 1
        stat["outstanding_size"] += event.size

    stat_rows = sorted(
        stats.values(),
        key=lambda item: (-item["outstanding_size"], -item["outstanding_count"], item["group_key"]),
    )
    for item in stat_rows:
        if item["stack_id"] > 0:
            item["frames"] = stack_maps.get(item["stack_id"], [])
        else:
            item["frames"] = []

    summary = {
        "alloc_count": alloc_count,
        "free_count": free_count,
        "matched_free_count": matched_free_count,
        "unmatched_free_count": len(unmatched_frees),
        "ignored_null_free_count": ignored_null_free_count,
        "duplicate_alloc_count": len(duplicate_allocations),
        "outstanding_count": len(outstanding),
        "outstanding_bytes": sum(item["size"] for item in outstanding),
        "statistics_group_count": len(stat_rows),
        "stack_map_count": len(stack_maps),
    }

    return {
        "summary": summary,
        "statistics": stat_rows,
        "stack_maps": {str(key): value for key, value in sorted(stack_maps.items())},
        "outstanding_allocations": outstanding,
        "unmatched_frees": unmatched_frees,
        "duplicate_allocations": duplicate_allocations,
    }


def render_markdown_report(report: dict) -> str:
    summary = report["summary"]
    lines = [
        "# nativehook-like leak analysis",
        "",
        "## Summary",
        "",
        f"- Alloc events: {summary['alloc_count']}",
        f"- Free events: {summary['free_count']}",
        f"- Matched frees: {summary['matched_free_count']}",
        f"- Unmatched frees: {summary['unmatched_free_count']}",
        f"- Duplicate allocs: {summary['duplicate_alloc_count']}",
        f"- Outstanding allocations: {summary['outstanding_count']}",
        f"- Outstanding bytes: {summary['outstanding_bytes']}",
        "",
        "## Statistics",
        "",
        "| group | callsite | apply_count | release_count | outstanding_size | outstanding_count |",
        "|---|---:|---:|---:|---:|---:|",
    ]

    for item in report["statistics"]:
        callsite = item["callsite"] or "-"
        lines.append(
            f"| {item['group_key']} | {callsite} | {item['apply_count']} | "
            f"{item['release_count']} | {item['outstanding_size']} | {item['outstanding_count']} |"
        )

    if report.get("stack_maps"):
        lines.extend([
            "",
            "## Stack Maps",
            "",
            "| stack_id | frames |",
            "|---:|---|",
        ])
        for stack_id, frames in report["stack_maps"].items():
            lines.append(f"| {stack_id} | {' -> '.join(frames)} |")

    lines.extend([
        "",
        "## Outstanding Allocations",
        "",
        "| addr | size | pid | tid | stack_id | callsite | age_ns |",
        "|---|---:|---:|---:|---:|---|---:|",
    ])
    for item in report["outstanding_allocations"]:
        callsite = item["callsite"] or "-"
        lines.append(
            f"| {item['addr']} | {item['size']} | {item['pid']} | {item['tid']} | "
            f"{item['stack_id']} | {callsite} | {item.get('age_ns', 0)} |"
        )

    if report["unmatched_frees"]:
        lines.extend([
            "",
            "## Unmatched Frees",
            "",
            "| line | addr | pid | tid | ts_sec | ts_nsec |",
            "|---:|---|---:|---:|---:|---:|",
        ])
        for item in report["unmatched_frees"]:
            lines.append(
                f"| {item['line']} | {item['addr']} | {item['pid']} | {item['tid']} | "
                f"{item['ts_sec']} | {item['ts_nsec']} |"
            )

    return "\n".join(lines) + "\n"


def _read_lines(path: str) -> list[str]:
    if path == "-":
        return sys.stdin.readlines()
    return Path(path).read_text(encoding="utf-8").splitlines()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Analyze Plan B VERBOSE records with nativehook-like alloc/free matching.",
    )
    parser.add_argument("input", help="consumer --verbose log path, or '-' for stdin")
    parser.add_argument("--json", dest="json_path", help="write JSON report to this path")
    parser.add_argument("--markdown", dest="markdown_path", help="write Markdown report to this path")
    args = parser.parse_args(argv)

    try:
        report = analyze_lines(_read_lines(args.input))
    except (OSError, ValueError) as exc:
        print(f"nativehook_like_analyzer: {exc}", file=sys.stderr)
        return 2

    markdown = render_markdown_report(report)

    if args.json_path:
        Path(args.json_path).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.markdown_path:
        Path(args.markdown_path).write_text(markdown, encoding="utf-8")
    if not args.json_path and not args.markdown_path:
        print(markdown, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
