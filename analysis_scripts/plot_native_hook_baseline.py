from pathlib import Path

import matplotlib.pyplot as plt


# Averaged no-hook baseline results (ops/s).
BASELINE_OPS = {
    1: 8_476_454,
    2: 7_919_032,   # 1 abnormal low run removed
    4: 5_416_447,
    8: 6_048_878,
    16: 6_368_653,
    32: 6_632_448,
    64: 11_040_897,
    80: 12_876_172,
    96: 12_968_720,
}

# Supplementary size experiment results (single-run, ops/s).
SIZE_EXPERIMENT_OPS = {
    19: {1: 8_476_454, 64: 11_040_897, 96: 12_968_720},
    64: {1: 9_669_100, 64: 11_171_338, 96: 12_500_550},
    256: {1: 9_666_803, 64: 10_818_930, 96: 13_068_620},
}


def format_mops(value: float) -> str:
    return f"{value / 1_000_000:.2f}M"


def ensure_output_dir() -> Path:
    output_dir = Path(__file__).resolve().parent / "charts"
    output_dir.mkdir(parents=True, exist_ok=True)
    return output_dir


def apply_common_style() -> None:
    plt.style.use("seaborn-v0_8-whitegrid")
    plt.rcParams.update(
        {
            "figure.dpi": 150,
            "savefig.dpi": 200,
            "axes.titlesize": 15,
            "axes.labelsize": 12,
            "xtick.labelsize": 10,
            "ytick.labelsize": 10,
            "legend.fontsize": 10,
        }
    )


def plot_total_throughput(output_dir: Path) -> None:
    threads = list(BASELINE_OPS.keys())
    ops = list(BASELINE_OPS.values())

    fig, ax = plt.subplots(figsize=(10, 5.5))
    ax.plot(threads, ops, color="#c44e52", marker="o", linewidth=2.5, markersize=7)

    for x, y in zip(threads, ops):
        ax.annotate(format_mops(y), (x, y), textcoords="offset points", xytext=(0, 7), ha="center")

    ax.set_title("No-hook Baseline: Total Throughput vs Thread Count")
    ax.set_xlabel("Threads")
    ax.set_ylabel("Throughput (ops/s)")
    ax.set_xticks(threads)
    ax.set_ylim(0, max(ops) * 1.15)
    ax.text(
        0.01,
        0.02,
        "80-96 threads are close to the platform saturation region.",
        transform=ax.transAxes,
        fontsize=10,
        color="#555555",
    )

    fig.tight_layout()
    fig.savefig(output_dir / "baseline_total_throughput.png", bbox_inches="tight")
    plt.close(fig)


def plot_per_thread_throughput(output_dir: Path) -> None:
    threads = list(BASELINE_OPS.keys())
    per_thread_ops = [BASELINE_OPS[t] / t for t in threads]

    fig, ax = plt.subplots(figsize=(10, 5.5))
    ax.plot(threads, per_thread_ops, color="#4c72b0", marker="o", linewidth=2.5, markersize=7)

    for x, y in zip(threads, per_thread_ops):
        ax.annotate(format_mops(y), (x, y), textcoords="offset points", xytext=(0, 7), ha="center")

    ax.set_title("No-hook Baseline: Per-thread Throughput vs Thread Count")
    ax.set_xlabel("Threads")
    ax.set_ylabel("Per-thread Throughput (ops/s/thread)")
    ax.set_xticks(threads)
    ax.set_ylim(0, max(per_thread_ops) * 1.15)
    ax.text(
        0.01,
        0.02,
        "Per-thread efficiency drops as shared contention grows.",
        transform=ax.transAxes,
        fontsize=10,
        color="#555555",
    )

    fig.tight_layout()
    fig.savefig(output_dir / "baseline_per_thread_throughput.png", bbox_inches="tight")
    plt.close(fig)


def plot_size_comparison(output_dir: Path) -> None:
    threads = [1, 64, 96]
    sizes = [19, 64, 256]
    colors = ["#55a868", "#8172b3", "#dd8452"]
    width = 0.22
    x_positions = list(range(len(threads)))

    fig, ax = plt.subplots(figsize=(10, 5.5))

    for idx, (size, color) in enumerate(zip(sizes, colors)):
        offsets = [x + (idx - 1) * width for x in x_positions]
        values = [SIZE_EXPERIMENT_OPS[size][thread] for thread in threads]
        bars = ax.bar(offsets, values, width=width, color=color, label=f"{size}B")
        for bar, value in zip(bars, values):
            ax.annotate(
                format_mops(value),
                (bar.get_x() + bar.get_width() / 2, bar.get_height()),
                textcoords="offset points",
                xytext=(0, 5),
                ha="center",
                fontsize=9,
            )

    ax.set_title("Supplementary Experiment: Allocation Size Comparison")
    ax.set_xlabel("Threads")
    ax.set_ylabel("Throughput (ops/s)")
    ax.set_xticks(x_positions)
    ax.set_xticklabels([str(t) for t in threads])
    ax.legend(title="Malloc size")
    ax.set_ylim(0, max(v for group in SIZE_EXPERIMENT_OPS.values() for v in group.values()) * 1.18)
    ax.text(
        0.01,
        0.02,
        "64B/256B results are from single-run supplementary tests.",
        transform=ax.transAxes,
        fontsize=10,
        color="#555555",
    )

    fig.tight_layout()
    fig.savefig(output_dir / "allocation_size_comparison.png", bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    apply_common_style()
    output_dir = ensure_output_dir()
    plot_total_throughput(output_dir)
    plot_per_thread_throughput(output_dir)
    plot_size_comparison(output_dir)
    print(f"Charts written to: {output_dir}")


if __name__ == "__main__":
    main()
