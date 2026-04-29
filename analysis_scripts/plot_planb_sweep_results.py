from __future__ import annotations

import csv
from pathlib import Path
from typing import Iterable

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Patch


ROOT_DIR = Path(__file__).resolve().parent
DEFAULT_SAMPLE_CSV = ROOT_DIR / "linux_native_hook_v1" / "results" / "sample_interval_sweep_2026-04-27.csv"
DEFAULT_FILTER_CSV = ROOT_DIR / "linux_native_hook_v1" / "results" / "filter_size_sweep_2026-04-27.csv"
DEFAULT_OUTPUT_PNG = ROOT_DIR / "charts" / "planb_sweep_summary_ppt_clean.png"
DEFAULT_OUTPUT_SVG = ROOT_DIR / "charts" / "planb_sweep_summary_ppt_clean.svg"
DEFAULT_OUTPUT_MINIMAL_PNG = ROOT_DIR / "charts" / "planb_sweep_summary_ppt_minimal.png"
DEFAULT_OUTPUT_MINIMAL_SVG = ROOT_DIR / "charts" / "planb_sweep_summary_ppt_minimal.svg"


SAMPLE_COLOR = "#1f5aa6"
RECORD_COLOR = "#8fd3d1"
BASELINE_COLOR = "#9aa3ad"
SIZE_COLORS = {
    32: "#1f5aa6",
    64: "#2a9d8f",
    256: "#f4a261",
}


def ensure_output_dir(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def apply_style() -> None:
    plt.style.use("seaborn-v0_8-whitegrid")
    plt.rcParams.update(
        {
            "figure.dpi": 150,
            "savefig.dpi": 200,
            "axes.titlesize": 18,
            "axes.labelsize": 14,
            "xtick.labelsize": 12,
            "ytick.labelsize": 12,
            "legend.fontsize": 12,
            "font.size": 12,
        }
    )


def load_rows(path: Path) -> list[dict[str, float]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        rows: list[dict[str, float]] = []
        for row in reader:
            parsed: dict[str, float] = {}
            for key, value in row.items():
                assert value is not None
                if key in {"threads", "size", "duration", "flush_threshold", "sample_interval", "filter_size", "records", "alloc", "free", "thread_name", "flush", "dropped"}:
                    parsed[key] = int(float(value))
                else:
                    parsed[key] = float(value)
            rows.append(parsed)
        return rows


def mops(value: float) -> float:
    return value / 1_000_000.0


def pct_recovered(row: dict[str, float]) -> float:
    baseline = row["baseline_ops"]
    if baseline == 0:
        return 0.0
    return row["with_hook_ops"] * 100.0 / baseline


def sort_filter_values(values: Iterable[int]) -> list[int]:
    return sorted(set(values), key=lambda item: (item != -1, item))


def format_filter_label(value: int) -> str:
    return "off" if value == -1 else str(value)


def plot_sample_interval_panel(ax: plt.Axes, rows: list[dict[str, float]], annotate_values: bool = True) -> None:
    ordered = sorted(rows, key=lambda row: row["sample_interval"])
    x = np.arange(len(ordered))
    labels = [str(int(row["sample_interval"])) for row in ordered]
    records_k = [row["records"] / 1000.0 for row in ordered]
    recovered = [pct_recovered(row) for row in ordered]

    ax2 = ax.twinx()
    bar_container = ax2.bar(x, records_k, width=0.62, color=RECORD_COLOR, alpha=0.55, label="records / run", zorder=1)
    ax.plot(x, recovered, color=SAMPLE_COLOR, marker="o", linewidth=2.6, markersize=7, label="recovered throughput", zorder=3)
    ax.axhline(100.0, color=BASELINE_COLOR, linestyle="--", linewidth=1.6, label="baseline = 100%", zorder=2)

    if annotate_values:
        for xi, yi in zip(x, recovered):
            ax.annotate(f"{yi:.1f}%", (xi, yi), textcoords="offset points", xytext=(0, 7), ha="center", color=SAMPLE_COLOR)

    ax.set_title("Sample Interval Sweep")
    ax.set_xlabel("sample_interval")
    ax.set_ylabel("Recovered Throughput (% of baseline)")
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    ax.set_ylim(0, 110)

    ax2.set_ylabel("Records per run (K)")
    ax2.set_ylim(0, max(records_k) * 1.18 if records_k else 1)

    handles1, labels1 = ax.get_legend_handles_labels()
    handles2, labels2 = ax2.get_legend_handles_labels()
    ax.legend(handles1 + handles2, labels1 + labels2, loc="upper right")

    if annotate_values:
        for idx in (0, len(records_k) - 1):
            rect = bar_container[idx]
            value = records_k[idx]
            ax2.annotate(
                f"{value:.1f}K",
                (rect.get_x() + rect.get_width() / 2, rect.get_height()),
                textcoords="offset points",
                xytext=(0, 5),
                ha="center",
                fontsize=11,
                color="#355c7d",
            )

def plot_filter_size_panel(ax: plt.Axes, rows: list[dict[str, float]], annotate_values: bool = True) -> None:
    filters = sort_filter_values(int(row["filter_size"]) for row in rows)
    sizes = sorted(set(int(row["size"]) for row in rows))
    x = np.arange(len(filters))
    width = 0.24

    for idx, size in enumerate(sizes):
        size_rows = sorted(
            (row for row in rows if int(row["size"]) == size),
            key=lambda row: (int(row["filter_size"]) != -1, int(row["filter_size"])),
        )
        offsets = x + (idx - (len(sizes) - 1) / 2) * width
        values = [pct_recovered(row) for row in size_rows]
        zero_record_mask = [int(row["records"]) == 0 for row in size_rows]
        bars = ax.bar(
            offsets,
            values,
            width=width,
            color=SIZE_COLORS.get(size, "#4c72b0"),
            label=f"size={size}B",
            zorder=3,
        )
        for bar, recovered, zero_records in zip(bars, values, zero_record_mask):
            if zero_records:
                bar.set_hatch("//")
                bar.set_edgecolor("#374151")
                bar.set_linewidth(1.0)
            if annotate_values and zero_records:
                ax.annotate(
                    f"{recovered:.1f}%",
                    (bar.get_x() + bar.get_width() / 2, bar.get_height()),
                    textcoords="offset points",
                    xytext=(0, 4),
                    ha="center",
                    fontsize=10,
                    color="#374151",
                )

    ax.set_title("Filter Size Sweep")
    ax.set_xlabel("filter_size")
    ax.set_ylabel("Recovered Throughput (% of baseline)")
    ax.set_xticks(x)
    ax.set_xticklabels([format_filter_label(value) for value in filters])
    ax.set_ylim(0, 38)
    handles, labels = ax.get_legend_handles_labels()
    handles.append(Patch(facecolor="white", edgecolor="#374151", hatch="//", label="hatched = records = 0"))
    labels.append("hatched = records = 0")
    ax.legend(handles, labels, loc="upper left")

def build_summary_figure(
    sample_rows: list[dict[str, float]],
    filter_rows: list[dict[str, float]],
    output_png: Path,
    output_svg: Path,
    *,
    subtitle: str | None,
    footer: str | None,
    annotate_values: bool,
) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(16, 9))
    plot_sample_interval_panel(axes[0], sample_rows, annotate_values=annotate_values)
    plot_filter_size_panel(axes[1], filter_rows, annotate_values=annotate_values)

    fig.suptitle("Plan B Sweep Results", fontsize=26, y=0.965, fontweight="bold")
    if subtitle:
        fig.text(
            0.5,
            0.925,
            subtitle,
            ha="center",
            fontsize=16,
            color="#374151",
        )
    if footer:
        fig.text(
            0.5,
            0.015,
            footer,
            ha="center",
            fontsize=14,
            color="#374151",
        )
    top_rect = 0.91 if subtitle else 0.93
    bottom_rect = 0.05 if footer else 0.04
    fig.tight_layout(rect=(0, bottom_rect, 1, top_rect))
    fig.savefig(output_png, bbox_inches="tight")
    fig.savefig(output_svg, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    apply_style()
    sample_csv = DEFAULT_SAMPLE_CSV
    filter_csv = DEFAULT_FILTER_CSV
    output_png = DEFAULT_OUTPUT_PNG
    output_svg = DEFAULT_OUTPUT_SVG
    output_minimal_png = DEFAULT_OUTPUT_MINIMAL_PNG
    output_minimal_svg = DEFAULT_OUTPUT_MINIMAL_SVG

    if not sample_csv.exists():
        raise FileNotFoundError(f"Sample CSV not found: {sample_csv}")
    if not filter_csv.exists():
        raise FileNotFoundError(f"Filter CSV not found: {filter_csv}")

    ensure_output_dir(output_png)
    sample_rows = load_rows(sample_csv)
    filter_rows = load_rows(filter_csv)
    build_summary_figure(
        sample_rows,
        filter_rows,
        output_png,
        output_svg,
        subtitle="Record traffic can be reduced aggressively, but throughput recovery plateaus early.",
        footer="Takeaway: the remaining overhead is likely shifting from shared-memory traffic to the producer hot path.",
        annotate_values=True,
    )
    build_summary_figure(
        sample_rows,
        filter_rows,
        output_minimal_png,
        output_minimal_svg,
        subtitle=None,
        footer=None,
        annotate_values=False,
    )
    print(
        "Charts written to: "
        f"{output_png}, {output_svg}, {output_minimal_png}, and {output_minimal_svg}"
    )


if __name__ == "__main__":
    main()
