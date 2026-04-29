from __future__ import annotations

import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.lines import Line2D


ROOT_DIR = Path(__file__).resolve().parent
SAMPLE_CSV = ROOT_DIR / "linux_native_hook_v1" / "results" / "blocked_sample_interval_sweep_2026-04-28.csv"
FILTER_CSV = ROOT_DIR / "linux_native_hook_v1" / "results" / "blocked_filter_size_sweep_2026-04-28.csv"

OUTPUT_CLEAN_PNG = ROOT_DIR / "charts" / "planb_blocked_summary_ppt_clean.png"
OUTPUT_CLEAN_SVG = ROOT_DIR / "charts" / "planb_blocked_summary_ppt_clean.svg"
OUTPUT_MINIMAL_PNG = ROOT_DIR / "charts" / "planb_blocked_summary_ppt_minimal.png"
OUTPUT_MINIMAL_SVG = ROOT_DIR / "charts" / "planb_blocked_summary_ppt_minimal.svg"


MODE_COLORS = {
    0: "#1f5aa6",
    1: "#c44e52",
}

SIZE_COLORS = {
    32: "#1f5aa6",
    64: "#2a9d8f",
    256: "#f4a261",
}

MODE_LABELS = {
    0: "non-blocked",
    1: "blocked",
}


def ensure_output_dir(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def apply_style() -> None:
    plt.style.use("seaborn-v0_8-whitegrid")
    plt.rcParams.update(
        {
            "figure.dpi": 150,
            "savefig.dpi": 220,
            "axes.titlesize": 18,
            "axes.labelsize": 14,
            "xtick.labelsize": 12,
            "ytick.labelsize": 12,
            "legend.fontsize": 11,
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
                if key in {
                    "threads",
                    "size",
                    "duration",
                    "flush_threshold",
                    "blocked",
                    "sample_interval",
                    "filter_size",
                    "records",
                    "alloc",
                    "free",
                    "thread_name",
                    "flush",
                    "dropped",
                }:
                    parsed[key] = int(float(value))
                else:
                    parsed[key] = float(value)
            rows.append(parsed)
        return rows


def pct_recovered(row: dict[str, float]) -> float:
    baseline = row["baseline_ops"]
    if baseline == 0:
        return 0.0
    return row["with_hook_ops"] * 100.0 / baseline


def sort_filter_value(value: int) -> tuple[int, int]:
    return (value != -1, value)


def plot_blocked_sample_panel(ax: plt.Axes, rows: list[dict[str, float]], annotate: bool) -> None:
    sample_values = sorted(set(int(row["sample_interval"]) for row in rows))
    x = np.arange(len(sample_values))
    for blocked in (0, 1):
        blocked_rows = sorted(
            (row for row in rows if int(row["blocked"]) == blocked),
            key=lambda row: int(row["sample_interval"]),
        )
        recovered = [pct_recovered(row) for row in blocked_rows]
        ax.plot(
            x,
            recovered,
            color=MODE_COLORS[blocked],
            linewidth=2.8,
            linestyle="-" if blocked == 0 else "--",
            marker="o" if blocked == 0 else "s",
            markersize=7,
            label=MODE_LABELS[blocked],
        )
        if annotate:
            for xi, yi in zip(x, recovered):
                ax.annotate(
                    f"{yi:.1f}%",
                    (xi, yi),
                    textcoords="offset points",
                    xytext=(0, 6),
                    ha="center",
                    color=MODE_COLORS[blocked],
                    fontsize=10,
                )

    ax.set_title("Blocked + Sample Interval Sweep")
    ax.set_xlabel("sample_interval")
    ax.set_ylabel("Recovered Throughput (% of baseline)")
    ax.set_xticks(x)
    ax.set_xticklabels([str(v) for v in sample_values])
    ax.set_ylim(0, 35)
    ax.legend(loc="upper left")


def plot_blocked_filter_panel(ax: plt.Axes, rows: list[dict[str, float]], annotate: bool) -> None:
    filter_values = sorted(set(int(row["filter_size"]) for row in rows), key=sort_filter_value)
    x = np.arange(len(filter_values))

    for size in (32, 64, 256):
        for blocked in (0, 1):
            subset = sorted(
                (row for row in rows if int(row["size"]) == size and int(row["blocked"]) == blocked),
                key=lambda row: sort_filter_value(int(row["filter_size"])),
            )
            recovered = [pct_recovered(row) for row in subset]
            ax.plot(
                x,
                recovered,
                color=SIZE_COLORS[size],
                linewidth=2.4,
                linestyle="-" if blocked == 0 else "--",
                marker="o" if blocked == 0 else "s",
                markersize=5.5,
            )

            if annotate:
                zero_like_indices = [
                    idx
                    for idx, row in enumerate(subset)
                    if int(row["records"]) <= 4 and pct_recovered(row) > 0
                ]
                for idx in zero_like_indices:
                    ax.scatter(
                        x[idx],
                        recovered[idx],
                        s=70,
                        facecolors="none",
                        edgecolors=SIZE_COLORS[size],
                        linewidths=1.4,
                        zorder=5,
                    )

    ax.set_title("Blocked + Filter Size Sweep")
    ax.set_xlabel("filter_size")
    ax.set_ylabel("Recovered Throughput (% of baseline)")
    ax.set_xticks(x)
    ax.set_xticklabels(["off" if v == -1 else str(v) for v in filter_values])
    ax.set_ylim(0, 35)

    size_handles = [
        Line2D([0], [0], color=SIZE_COLORS[size], linewidth=2.8, marker="o", markersize=6, label=f"size={size}B")
        for size in (32, 64, 256)
    ]
    mode_handles = [
        Line2D([0], [0], color="#374151", linewidth=2.6, linestyle="-", marker="o", markersize=6, label="non-blocked"),
        Line2D([0], [0], color="#374151", linewidth=2.6, linestyle="--", marker="s", markersize=6, label="blocked"),
        Line2D([0], [0], color="#374151", linewidth=0, marker="o", markersize=8, markerfacecolor="none", label="circle outline = records≈0"),
    ]

    size_legend = ax.legend(handles=size_handles, loc="upper left")
    ax.add_artist(size_legend)
    ax.legend(handles=mode_handles, loc="upper right")


def build_figure(
    sample_rows: list[dict[str, float]],
    filter_rows: list[dict[str, float]],
    output_png: Path,
    output_svg: Path,
    *,
    subtitle: str | None,
    footer: str | None,
    annotate: bool,
) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(16, 9))
    plot_blocked_sample_panel(axes[0], sample_rows, annotate=annotate)
    plot_blocked_filter_panel(axes[1], filter_rows, annotate=annotate)

    fig.suptitle("Blocked Mode Interactions", fontsize=26, y=0.965, fontweight="bold")
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
    if not SAMPLE_CSV.exists():
        raise FileNotFoundError(f"Sample CSV not found: {SAMPLE_CSV}")
    if not FILTER_CSV.exists():
        raise FileNotFoundError(f"Filter CSV not found: {FILTER_CSV}")

    ensure_output_dir(OUTPUT_CLEAN_PNG)

    sample_rows = load_rows(SAMPLE_CSV)
    filter_rows = load_rows(FILTER_CSV)

    build_figure(
        sample_rows,
        filter_rows,
        OUTPUT_CLEAN_PNG,
        OUTPUT_CLEAN_SVG,
        subtitle="Blocked mode is much more expensive at high record density, but the gap shrinks after aggressive sampling/filtering.",
        footer="Takeaway: blocked overhead is most visible while the producer is still generating many records.",
        annotate=True,
    )
    build_figure(
        sample_rows,
        filter_rows,
        OUTPUT_MINIMAL_PNG,
        OUTPUT_MINIMAL_SVG,
        subtitle=None,
        footer=None,
        annotate=False,
    )

    print(
        "Charts written to: "
        f"{OUTPUT_CLEAN_PNG}, {OUTPUT_CLEAN_SVG}, {OUTPUT_MINIMAL_PNG}, and {OUTPUT_MINIMAL_SVG}"
    )


if __name__ == "__main__":
    main()
