"""Infographic of a stock_compare.ps1 run (docs/benchmarks/stock-compare.md).

python devtools/test/stock_compare_chart.py <results dir> <scene csv> <out.png> "<fork label>" "<date>"

<results dir> holds results_level.csv, results_instance.csv and
results_physics.csv and results_shader.csv (devtools/out/stock_compare/<Label>/); <scene csv> is
scene_bench.ps1's CSV of the same run. Values are medians over rounds; CSVs
may use decimal commas (the machine's locale).
"""

import collections
import csv
import statistics
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# Categorical slots in fixed order (dataviz reference palette, validated):
# 1 blue = fork (PhysX GPU), 2 orange = stock (Jolt), 3 aqua = PhysX CPU,
# 4 yellow = GodotPhysics3D. Values are written on every bar.
FORK = "#257de0"
STOCK = "#f47230"
PHYSX_CPU = "#00b894"
GODOT_PHYSICS = "#f2a600"
TEXT = "#1f1f1f"
TEXT_2 = "#6b6b6b"
GRID = "#e6e6e6"
SURFACE = "#fafafa"


def num(text):
    return float(text.replace(",", "."))


def medians(path, key, value_columns, where=None):
    groups = collections.defaultdict(lambda: collections.defaultdict(list))
    with open(path, encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            if where and not where(row):
                continue
            for column in value_columns:
                if row.get(column) not in (None, ""):
                    groups[key(row)][column].append(num(row[column]))
    return {k: {c: statistics.median(v) for c, v in cols.items()} for k, cols in groups.items()}


def style(ax):
    ax.set_facecolor("white")
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color("#bdbdbd")
    ax.tick_params(colors=TEXT_2, labelsize=12, length=0)
    ax.set_axisbelow(True)


def bar_tops(ax, bars, values, fmt):
    top = max(values)
    for bar, value in zip(bars, values):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + top * 0.02, fmt(value), ha="center", va="bottom", fontsize=12, color=TEXT)


def speedup_panel(ax, rows, title, subtitle):
    style(ax)
    rows = sorted(rows, key=lambda r: r[1])
    labels = [r[0] for r in rows]
    values = [r[1] for r in rows]
    bars = ax.barh(range(len(rows)), values, color=FORK, height=0.62)
    ax.set_yticks(range(len(rows)), labels, fontsize=12.5, color=TEXT)
    ax.axvline(1.0, color=TEXT_2, linestyle=(0, (3, 3)), linewidth=1)
    ax.grid(axis="x", color=GRID)
    top = max(values)
    ax.set_xlim(0, top * 1.12)
    for bar, value in zip(bars, values):
        ax.text(value + top * 0.01, bar.get_y() + bar.get_height() / 2, f"{value:.1f}×", va="center", fontsize=12.5, fontweight="bold", color=TEXT)
    ax.set_xlabel("speed-up over stock (stock time ÷ fork time); dashed line: 1×, same as stock", fontsize=11.5, color=TEXT_2)
    ax.set_title(title, loc="left", fontsize=17, fontweight="bold", color=TEXT, pad=40)
    ax.text(0, 1.02, subtitle, transform=ax.transAxes, fontsize=11.5, color=TEXT_2, va="bottom")


def main():
    results, scene_csv, out, fork_label, date = sys.argv[1:6]
    level = medians(f"{results}/results_level.csv", lambda r: (r["Case"], r["Copy"]), ["wall_p50", "vram_mb"])
    inst = medians(f"{results}/results_instance.csv", lambda r: (r["case"], r["Copy"]), ["frame_p50", "gpu_p50", "video_mib"], where=lambda r: r["view"] == "static")
    phys = medians(f"{results}/results_physics.csv", lambda r: (r["Case"], r["Copy"]), ["tick_mean_ms", "sleeping"])
    shader = medians(f"{results}/results_shader.csv", lambda r: r["Copy"], ["spawn_frame_ms"])
    scene = medians(scene_csv, lambda r: (r["Scenario"], int(r["Count"]), "stock" if "stable" in r["Build"] else "fork"), ["FrameMs"])

    plt.rcParams["font.family"] = ["Segoe UI", "DejaVu Sans"]
    fig = plt.figure(figsize=(24, 21.5), dpi=100, facecolor=SURFACE)
    fig.text(0.04, 0.972, f"Godot Diginaat ({fork_label}) vs stock Godot 4.7.2", fontsize=34, fontweight="bold", color=TEXT)
    fig.text(0.04, 0.951, f"Only what the fork changed: GPU-driven geometry, scene runtime, PhysX.  RTX 5070, Vulkan, 1280×720, vsync off, medians of 2-3 alternating rounds, fork defaults.  {date}", fontsize=14, color=TEXT_2)
    fig.patches.append(plt.Rectangle((0.04, 0.929), 0.008, 0.009, color=FORK, transform=fig.transFigure))
    fig.text(0.052, 0.929, f"Fork ({fork_label})", fontsize=14, color=TEXT)
    fig.patches.append(plt.Rectangle((0.30, 0.929), 0.008, 0.009, color=STOCK, transform=fig.transFigure))
    fig.text(0.312, 0.929, "Stock Godot 4.7.2", fontsize=14, color=TEXT)
    fig.text(0.96, 0.929, "Full tables and conditions: docs/benchmarks/stock-compare.md", fontsize=12, color=TEXT_2, ha="right")

    # Game levels.
    fig.text(0.04, 0.895, "Game levels: frame time (ms, lower is better)", fontsize=19, fontweight="bold", color=TEXT)
    presets = [("town", "15k objects"), ("forest", "60k objects"), ("city_extreme", "200k objects, 4,000 materials")]
    for i, (preset, what) in enumerate(presets):
        ax = fig.add_axes([0.06 + i * 0.155, 0.70, 0.12, 0.15])
        style(ax)
        values = [level[(preset, "stock")]["wall_p50"], level[(preset, "fork")]["wall_p50"]]
        bars = ax.bar(["stock", "fork"], values, color=[STOCK, FORK], width=0.6)
        bar_tops(ax, bars, values, lambda v: f"{v:.2f}")
        ax.set_ylim(0, values[0] * 1.2)
        ax.grid(axis="y", color=GRID)
        ax.tick_params(axis="x", labelsize=13, colors=TEXT_2)
        ax.set_title(f"{preset}\n", loc="left", fontsize=15, fontweight="bold", color=TEXT)
        ax.text(0, 1.03, what, transform=ax.transAxes, fontsize=11.5, color=TEXT_2)
        ax.text(0.98, 0.93, f"{values[0] / values[1]:.1f}× faster", transform=ax.transAxes, ha="right", fontsize=15, fontweight="bold", color=TEXT)

    # Physics.
    fig.text(0.53, 0.895, "Physics: ms per tick (lower is better)", fontsize=19, fontweight="bold", color=TEXT)
    engines = [("stock", "Stock\nJolt", STOCK), ("fork", "PhysX\nGPU", FORK), ("fork_physx_cpu", "PhysX\nCPU", PHYSX_CPU), ("stock_godotphysics", "Godot\nPhysics", GODOT_PHYSICS)]
    cases = [("crates_2k", "2k boxes"), ("mixed_8k", "8k mixed shapes"), ("extreme_30k", "30k bodies")]
    for i, (case, what) in enumerate(cases):
        ax = fig.add_axes([0.55 + i * 0.145, 0.70, 0.115, 0.15])
        style(ax)
        values = [phys[(case, copy)]["tick_mean_ms"] for copy, _, _ in engines]
        bars = ax.bar(range(4), values, color=[c for _, _, c in engines], width=0.68)
        ax.set_xticks(range(4), [n for _, n, _ in engines], fontsize=11)
        bar_tops(ax, bars, values, lambda v: f"{v:.2f}" if v < 10 else f"{v:.1f}" if v < 100 else f"{v:.0f}")
        ax.set_ylim(0, max(values) * 1.18)
        ax.grid(axis="y", color=GRID)
        jolt, gpu, cpu = values[0], values[1], values[2]
        if gpu > jolt:
            note = f"vs Jolt: GPU {gpu / jolt:.1f}× slower, CPU {jolt / cpu:.1f}× faster"
        else:
            note = f"vs Jolt: PhysX GPU {jolt / gpu:.1f}× faster"
        ax.set_title(f"{what}\n", loc="left", fontsize=15, fontweight="bold", color=TEXT)
        ax.text(0, 1.03, note, transform=ax.transAxes, fontsize=11, color=TEXT_2)

    # Many separate objects.
    names = {
        "unique_10k": "10k nodes, one material each", "massive_100k": "100k nodes", "churn_100k": "100k nodes, 100 material changes/frame",
        "shared_20k": "20k nodes, 4 meshes", "dynamic_20k": "20k nodes, 2,000 moving", "lights_20k": "20k nodes, 128 omni + 8 shadowed spots",
        "unique_lod_2k": "2k unique meshes with LODs", "small_1k": "1k nodes, 1 mesh",
    }
    rows = [(label, inst[(case, "stock")]["frame_p50"] / inst[(case, "fork")]["frame_p50"]) for case, label in names.items()]
    ax = fig.add_axes([0.20, 0.395, 0.27, 0.22])
    speedup_panel(ax, rows, "Renderer: many separate objects", "instance_bench, static view, frame time.  GPU instances, instance table, HZB occlusion, material tables")

    # Scene runtime.
    scene_names = {
        "instance": "instance a scene (in a group)", "churn": "churn: 2% freed and added", "churn_read": "churn + reads", "parents": "parents", "wide": "wide tree",
        "script": "script process", "heavy": "heavy", "native": "native process", "deep": "deep tree", "mixed": "mixed", "sparse": "sparse transforms", "reparent": "reparent",
    }
    rows = [(label, scene[(s, 100000, "stock")]["FrameMs"] / scene[(s, 100000, "fork")]["FrameMs"]) for s, label in scene_names.items()]
    ax = fig.add_axes([0.69, 0.395, 0.27, 0.22])
    speedup_panel(ax, rows, "Scene runtime: 100k nodes", "scene_bench, headless, ms per frame.  Groups, children cache, transform propagation, core containers")

    # Video memory.
    vram_rows = [("town", level[("town", "stock")]["vram_mb"], level[("town", "fork")]["vram_mb"]),
            ("forest", level[("forest", "stock")]["vram_mb"], level[("forest", "fork")]["vram_mb"]),
            ("city_extreme", level[("city_extreme", "stock")]["vram_mb"], level[("city_extreme", "fork")]["vram_mb"])]
    for case in ("small_1k", "shared_20k", "massive_100k", "churn_100k"):
        vram_rows.append((case, inst[(case, "stock")]["video_mib"], inst[(case, "fork")]["video_mib"]))
    ax = fig.add_axes([0.06, 0.075, 0.40, 0.21])
    style(ax)
    xs = range(len(vram_rows))
    width = 0.38
    stock_bars = ax.bar([x - width / 2 - 0.01 for x in xs], [r[1] for r in vram_rows], width=width, color=STOCK)
    fork_bars = ax.bar([x + width / 2 + 0.01 for x in xs], [r[2] for r in vram_rows], width=width, color=FORK)
    top = max(max(r[1], r[2]) for r in vram_rows)
    for bars in (stock_bars, fork_bars):
        for bar in bars:
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + top * 0.015, f"{bar.get_height():.0f}", ha="center", va="bottom", fontsize=11, color=TEXT)
    ax.set_xticks(list(xs), [r[0] for r in vram_rows], fontsize=12, color=TEXT)
    ax.set_ylim(0, top * 1.15)
    ax.grid(axis="y", color=GRID)
    ax.axvline(2.5, color="#bdbdbd", linewidth=1)
    ax.text(1.0, top * 1.1, "game levels", ha="center", fontsize=11.5, color=TEXT_2)
    ax.text(4.5, top * 1.1, "instance_bench (process total after each case)", ha="center", fontsize=11.5, color=TEXT_2)
    ax.set_title("Video memory (MB, lower is better)", loc="left", fontsize=17, fontweight="bold", color=TEXT, pad=40)
    ax.text(0, 1.02, "Performance.RENDER_VIDEO_MEM_USED.  Stock left, fork right", transform=ax.transAxes, fontsize=11.5, color=TEXT_2, va="bottom")

    # Where the fork is not better.
    box = fig.add_axes([0.53, 0.06, 0.43, 0.25])
    box.set_xticks([])
    box.set_yticks([])
    box.set_facecolor("white")
    for spine in box.spines.values():
        spine.set_color("#dddddd")
    gpu_extra = [inst[(c, "fork")]["gpu_p50"] - inst[(c, "stock")]["gpu_p50"] for c in names if inst[(c, "fork")]["gpu_p50"] - inst[(c, "stock")]["gpu_p50"] > 0.005]
    crates = {copy: phys[("crates_2k", copy)] for copy in ("stock", "fork", "fork_physx_cpu")}
    lines = [
        f"PhysX GPU with few bodies: {crates['fork']['tick_mean_ms']:.2f} vs {crates['stock']['tick_mean_ms']:.2f} ms per tick at 2k boxes (fixed GPU cost).",
        f"   PhysX CPU is faster there: {crates['fork_physx_cpu']['tick_mean_ms']:.2f} ms.",
        f"PhysX keeps settled bodies awake ({crates['fork']['sleeping']:,.0f} of 2,000 asleep vs {crates['stock']['sleeping']:,.0f} on Jolt),",
        "   so resting scenes cost more.",
        f"Video memory: city_extreme {level[('city_extreme', 'fork')]['vram_mb'] - level[('city_extreme', 'stock')]['vram_mb']:+.0f} MB, town {level[('town', 'fork')]['vram_mb'] - level[('town', 'stock')]['vram_mb']:+.0f} MB (instance table, HZB, culling buffers).",
        f"GPU time in open instance_bench cases: +{min(gpu_extra):.2f}-{max(gpu_extra):.2f} ms in {len(gpu_extra)} of 8 (occlusion with",
        "   nothing to hide; it saves 0.9-3.4 ms of GPU time in forest and city_extreme, measured separately).",
        f"Tiny scenes on the GPU: small_1k {inst[('small_1k', 'stock')]['gpu_p50']:.3f} vs {inst[('small_1k', 'fork')]['gpu_p50']:.3f} ms (the frame is still faster).",
        f"scene_bench native process at 10k: {scene[('native', 10000, 'stock')]['FrameMs']:.3f} vs {scene[('native', 10000, 'fork')]['FrameMs']:.3f} ms; 100k is {scene[('native', 100000, 'stock')]['FrameMs'] / scene[('native', 100000, 'fork')]['FrameMs']:.1f}× faster.",
        f"Cold shader compile, 120 materials in one frame: {shader['fork']['spawn_frame_ms'] / 1000:.1f} s vs {shader['stock']['spawn_frame_ms'] / 1000:.1f} s on stock ({100 * (shader['fork']['spawn_frame_ms'] / shader['stock']['spawn_frame_ms'] - 1):+.0f}%).",
    ]
    box.text(0.035, 0.90, "Where the fork is not better", transform=box.transAxes, fontsize=18, fontweight="bold", color=TEXT, va="top")
    y = 0.76
    for line in lines:
        bullet = "" if line.startswith("   ") else "•  "
        box.text(0.035, y, bullet + line.strip() if bullet else "     " + line.strip(), transform=box.transAxes, fontsize=13, color=TEXT, va="top")
        y -= 0.071

    fig.savefig(out, facecolor=SURFACE)


if __name__ == "__main__":
    main()
