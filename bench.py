"""Build each queue implementation, run it N times, and report p50/p99 of the
per-run TOTAL TIME (one run = 1,000,000 items pushed and popped).

Usage: python bench.py [runs]        (default 2000)
Writes results to benchmark_results.txt.
"""
import re
import statistics
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BIN = ROOT / "bench_bin"
OUT = ROOT / "benchmark_results.txt"
RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
CXX = ["clang++", "-std=c++20", "-O2"]

# (label, source file, extra args)
BENCHES = [
    ("lockFree (SPSC, hand-written)",          "lockFree.cpp",                 []),
    ("boundedQueue (mutex+cv, cap 10k, 10/10)", "boundedQueue.cpp",            []),
    ("producer_consumer_MutexQ (10/10)",        "producer_consumer_MutexQ.cpp", []),
    ("MPMC_test (hazard-ptr MS queue, 10/10)",  "MPMC_test.cpp",                []),
    ("MPMC_NotMine moodycamel SPSC (1/1)",      "MPMC_NotMine.cpp",             ["spsc"]),
    ("MPMC_NotMine moodycamel MPMC (10/10)",    "MPMC_NotMine.cpp",             ["mpmc"]),
]

TIME_RE = re.compile(r"TOTAL TIME:\s*([0-9.eE+-]+)\s*ms")


def percentile(sorted_vals, p):
    """Nearest-rank percentile."""
    k = max(0, min(len(sorted_vals) - 1, int(round(p / 100 * len(sorted_vals) + 0.5)) - 1))
    return sorted_vals[k]


def build(src):
    exe = BIN / (Path(src).stem + ".exe")
    if not exe.exists() or exe.stat().st_mtime < (ROOT / src).stat().st_mtime:
        subprocess.run(CXX + [str(ROOT / src), "-o", str(exe)], check=True)
    return exe


def main():
    BIN.mkdir(exist_ok=True)
    results = []
    for label, src, args in BENCHES:
        exe = build(src)
        times, failures = [], 0
        start = time.time()
        for i in range(RUNS):
            p = subprocess.run([str(exe), *args], capture_output=True, text=True)
            m = TIME_RE.search(p.stdout)
            if p.returncode != 0 or not m:
                failures += 1
            if m:
                times.append(float(m.group(1)))
            if (i + 1) % 100 == 0:
                print(f"{label}: {i + 1}/{RUNS}", flush=True)
        times.sort()
        results.append((label, times, failures, time.time() - start))

    lines = [
        f"Queue benchmark - {datetime.now():%Y-%m-%d %H:%M:%S}",
        f"Compiler: {' '.join(CXX)}",
        f"Runs per benchmark: {RUNS}  (each run = 1,000,000 items; latency = TOTAL TIME per run)",
        "",
        f"{'benchmark':<42}{'p50 ms':>10}{'p99 ms':>10}{'mean ms':>10}{'min ms':>10}{'max ms':>10}{'fails':>7}",
        "-" * 99,
    ]
    for label, t, fails, _ in results:
        if not t:
            lines.append(f"{label:<42}{'no data':>10}{'':>40}{fails:>7}")
            continue
        lines.append(
            f"{label:<42}{percentile(t, 50):>10.2f}{percentile(t, 99):>10.2f}"
            f"{statistics.fmean(t):>10.2f}{t[0]:>10.2f}{t[-1]:>10.2f}{fails:>7}"
        )
    lines.append("")
    lines.append("fails = runs that exited non-zero or printed no TOTAL TIME (lockFree.cpp has no PASS/FAIL check).")
    lines.append("Wall time per benchmark:")
    lines += [f"  {l:<42}{w:>8.0f} s" for l, _, _, w in results]
    OUT.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
