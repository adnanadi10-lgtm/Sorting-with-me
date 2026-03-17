"""
╔══════════════════════════════════════════════════════════════════════════════╗
║              SORTING ALGORITHM BENCHMARK — FULL SCRIPT                      ║
║                                                                              ║
║  Algorithms : Quick Sort, Merge Sort, Heap Sort, Bubble Sort,               ║
║               Selection Sort, Insertion Sort, Shaker Sort,                  ║
║               Counting Sort, Radix Sort, Timsort                            ║
║                                                                              ║
║  Input sizes: 100,000 | 1,000,000 | 10,000,000 elements                    ║
║  Conditions : random | sorted | reverse                                     ║
║  Metrics    : wall-clock time (seconds) | peak memory (MB)                  ║
║                                                                              ║
║  Usage      : python sorting_benchmark_full.py                              ║
║  Output     : results printed to terminal + saved to results.csv            ║
╚══════════════════════════════════════════════════════════════════════════════╝

NOTES:
  • O(n²) algorithms (Bubble, Selection, Insertion, Shaker) are SKIPPED for
    sizes > 100K by default — they would take hours to finish.
    Set SKIP_SLOW_FOR_LARGE = False to force-run them (not recommended).

  • Data is generated once, saved to ./benchmark_data/ as binary .bin files,
    and reused on subsequent runs (safe to re-run the script).

  • Counting Sort and Radix Sort require non-negative integers — the generated
    data uses range [0, size) so they work correctly.

  • Timsort calls Python's native list.sort() which is implemented in C and
    will always be the fastest reference point.
"""

import os
import sys
import copy
import csv
import struct
import random
import time
import tracemalloc

# ══════════════════════════════════════════════════════════════════════════════
# ❶  CONFIGURATION
# ══════════════════════════════════════════════════════════════════════════════

SIZES = {
    "100k": 100_000,
    "1m":   1_000_000,
    "10m":  10_000_000,
}

CONDITIONS = ["random", "sorted", "reverse"]

DATA_DIR    = "benchmark_data"   # where .bin data files are stored
RESULTS_CSV = "results.csv"      # where final results are saved

# O(n²) algorithms skipped for sizes larger than this
SLOW_ALGO_MAX_SIZE  = 100_000
SKIP_SLOW_FOR_LARGE = True       # set False to force-run (very slow!)

SLOW_ALGOS = {"Bubble Sort", "Selection Sort", "Insertion Sort", "Shaker Sort"}

# ══════════════════════════════════════════════════════════════════════════════
# ❷  DATA GENERATION & I/O
# ══════════════════════════════════════════════════════════════════════════════

def _generate(size: int, condition: str) -> list:
    """
    Build a list of `size` unique integers:
      random  → shuffled  [0, size)
      sorted  → ascending [0, size)
      reverse → descending
    """
    base = list(range(size))
    if condition == "random":
        random.shuffle(base)
    elif condition == "reverse":
        base.reverse()
    # "sorted" needs no change
    return base


def _save_bin(data: list, path: str):
    """Persist a list of ints as packed 32-bit little-endian binary."""
    with open(path, "wb") as f:
        f.write(struct.pack(f"<{len(data)}i", *data))


def _load_bin(path: str) -> list:
    """Load a binary file created by _save_bin() back into a Python list."""
    with open(path, "rb") as f:
        raw = f.read()
    count = len(raw) // 4
    return list(struct.unpack(f"<{count}i", raw))


def ensure_data():
    """
    Generate and persist all 9 data files (3 sizes × 3 conditions).
    Skips files that already exist so re-running is cheap.
    Returns a dict: {(label, condition): filepath}
    """
    os.makedirs(DATA_DIR, exist_ok=True)

    total  = len(SIZES) * len(CONDITIONS)
    needed = 0
    paths  = {}

    print("=" * 72)
    print("  PHASE 1 — DATA GENERATION")
    print("=" * 72)

    for label, size in SIZES.items():
        print(f"\n  Size: {size:>12,}  ({label.upper()})")
        for condition in CONDITIONS:
            fname    = f"data_{label}_{condition}.bin"
            filepath = os.path.join(DATA_DIR, fname)
            paths[(label, condition)] = filepath

            if os.path.exists(filepath):
                mb = os.path.getsize(filepath) / (1024 ** 2)
                print(f"    [SKIP]  {fname:<28}  already exists  ({mb:.1f} MB)")
                continue

            print(f"    [GEN ]  {fname:<28} ", end="", flush=True)
            t0   = time.perf_counter()
            data = _generate(size, condition)
            _save_bin(data, filepath)
            elapsed = time.perf_counter() - t0
            mb      = os.path.getsize(filepath) / (1024 ** 2)
            print(f"done  {elapsed:5.2f}s  {mb:6.1f} MB")
            needed += 1

    status = f"{needed} new file(s) created" if needed else "all files already existed"
    print(f"\n  Data ready — {status}. ({total} files total in ./{DATA_DIR}/)\n")
    return paths


# ══════════════════════════════════════════════════════════════════════════════
# ❸  SORTING ALGORITHM IMPLEMENTATIONS
# ══════════════════════════════════════════════════════════════════════════════

# ── Quick Sort ────────────────────────────────────────────────────────────────
def quick_sort(arr):
    """Iterative Quick Sort with median-of-three pivot (avoids stack overflow)."""
    def _partition(arr, low, high):
        mid = (low + high) // 2
        # Sort low, mid, high so median ends up at mid
        if arr[low]  > arr[mid]:  arr[low],  arr[mid]  = arr[mid],  arr[low]
        if arr[low]  > arr[high]: arr[low],  arr[high] = arr[high], arr[low]
        if arr[mid]  > arr[high]: arr[mid],  arr[high] = arr[high], arr[mid]
        pivot = arr[mid]
        arr[mid], arr[high - 1] = arr[high - 1], arr[mid]
        i, j = low, high - 1
        while True:
            i += 1
            while arr[i] < pivot: i += 1
            j -= 1
            while arr[j] > pivot: j -= 1
            if i >= j: break
            arr[i], arr[j] = arr[j], arr[i]
        arr[i], arr[high - 1] = arr[high - 1], arr[i]
        return i

    if len(arr) <= 1:
        return arr
    stack = [(0, len(arr) - 1)]
    while stack:
        low, high = stack.pop()
        if low < high:
            p = _partition(arr, low, high)
            stack.append((low, p - 1))
            stack.append((p + 1, high))
    return arr


# ── Merge Sort ────────────────────────────────────────────────────────────────
def merge_sort(arr):
    """Recursive Merge Sort (bottom-up merge of sublists)."""
    if len(arr) <= 1:
        return arr
    mid   = len(arr) // 2
    left  = merge_sort(arr[:mid])
    right = merge_sort(arr[mid:])
    # Merge
    result = []
    i = j = 0
    while i < len(left) and j < len(right):
        if left[i] <= right[j]:
            result.append(left[i]); i += 1
        else:
            result.append(right[j]); j += 1
    result.extend(left[i:])
    result.extend(right[j:])
    return result


# ── Heap Sort ─────────────────────────────────────────────────────────────────
def heap_sort(arr):
    """In-place Heap Sort using a max-heap."""
    def _heapify(arr, n, i):
        while True:
            largest = i
            l, r = 2*i + 1, 2*i + 2
            if l < n and arr[l] > arr[largest]: largest = l
            if r < n and arr[r] > arr[largest]: largest = r
            if largest == i: break
            arr[i], arr[largest] = arr[largest], arr[i]
            i = largest

    n = len(arr)
    for i in range(n // 2 - 1, -1, -1):
        _heapify(arr, n, i)
    for i in range(n - 1, 0, -1):
        arr[0], arr[i] = arr[i], arr[0]
        _heapify(arr, i, 0)
    return arr


# ── Bubble Sort ───────────────────────────────────────────────────────────────
def bubble_sort(arr):
    """Optimised Bubble Sort — exits early if no swaps (O(n) best case)."""
    n = len(arr)
    for i in range(n):
        swapped = False
        for j in range(0, n - i - 1):
            if arr[j] > arr[j + 1]:
                arr[j], arr[j + 1] = arr[j + 1], arr[j]
                swapped = True
        if not swapped:
            break
    return arr


# ── Selection Sort ────────────────────────────────────────────────────────────
def selection_sort(arr):
    """Selection Sort — always O(n²) comparisons regardless of input."""
    n = len(arr)
    for i in range(n):
        min_idx = i
        for j in range(i + 1, n):
            if arr[j] < arr[min_idx]:
                min_idx = j
        arr[i], arr[min_idx] = arr[min_idx], arr[i]
    return arr


# ── Insertion Sort ────────────────────────────────────────────────────────────
def insertion_sort(arr):
    """Insertion Sort — O(n) best case on nearly-sorted data."""
    for i in range(1, len(arr)):
        key = arr[i]
        j   = i - 1
        while j >= 0 and arr[j] > key:
            arr[j + 1] = arr[j]
            j -= 1
        arr[j + 1] = key
    return arr


# ── Shaker Sort (Cocktail Sort) ───────────────────────────────────────────────
def shaker_sort(arr):
    """Bidirectional Bubble Sort — slightly better than standard Bubble Sort."""
    n = len(arr)
    swapped = True
    start, end = 0, n - 1
    while swapped:
        swapped = False
        for i in range(start, end):
            if arr[i] > arr[i + 1]:
                arr[i], arr[i + 1] = arr[i + 1], arr[i]
                swapped = True
        if not swapped: break
        end    -= 1
        swapped = False
        for i in range(end - 1, start - 1, -1):
            if arr[i] > arr[i + 1]:
                arr[i], arr[i + 1] = arr[i + 1], arr[i]
                swapped = True
        start += 1
    return arr


# ── Counting Sort ─────────────────────────────────────────────────────────────
def counting_sort(arr):
    """Counting Sort — O(n + k) where k is the value range. Non-negative ints only."""
    if not arr:
        return arr
    min_val, max_val = min(arr), max(arr)
    count  = [0] * (max_val - min_val + 1)
    for x in arr:
        count[x - min_val] += 1
    result = []
    for i, c in enumerate(count):
        if c:
            result.extend([i + min_val] * c)
    return result


# ── Radix Sort (LSD) ──────────────────────────────────────────────────────────
def radix_sort(arr):
    """LSD Radix Sort (base 10) — O(nk) where k is number of digits."""
    def _counting_pass(arr, exp):
        n      = len(arr)
        output = [0] * n
        count  = [0] * 10
        for x in arr:
            count[(x // exp) % 10] += 1
        for i in range(1, 10):
            count[i] += count[i - 1]
        for i in range(n - 1, -1, -1):
            d = (arr[i] // exp) % 10
            output[count[d] - 1] = arr[i]
            count[d] -= 1
        return output

    if not arr:
        return arr
    max_val = max(arr)
    exp = 1
    while max_val // exp > 0:
        arr  = _counting_pass(arr, exp)
        exp *= 10
    return arr


# ── Timsort ───────────────────────────────────────────────────────────────────
def timsort(arr):
    """Python's built-in Timsort — highly optimised C implementation."""
    arr.sort()
    return arr


# ── Algorithm registry ────────────────────────────────────────────────────────
ALGORITHMS = {
    "Quick Sort":     quick_sort,
    "Merge Sort":     merge_sort,
    "Heap Sort":      heap_sort,
    "Bubble Sort":    bubble_sort,
    "Selection Sort": selection_sort,
    "Insertion Sort": insertion_sort,
    "Shaker Sort":    shaker_sort,
    "Counting Sort":  counting_sort,
    "Radix Sort":     radix_sort,
    "Timsort":        timsort,
}


# ══════════════════════════════════════════════════════════════════════════════
# ❹  BENCHMARKING ENGINE
# ══════════════════════════════════════════════════════════════════════════════

def run_one(func, data: list):
    """
    Run func on a fresh copy of data.
    Returns (elapsed_seconds, peak_memory_MB).
    Memory is measured via tracemalloc — tracks Python heap allocations.
    """
    arr = copy.copy(data)

    tracemalloc.start()
    snap_before = tracemalloc.take_snapshot()

    t0     = time.perf_counter()
    func(arr)
    elapsed = time.perf_counter() - t0

    snap_after = tracemalloc.take_snapshot()
    tracemalloc.stop()

    stats    = snap_after.compare_to(snap_before, "lineno")
    peak_b   = sum(s.size_diff for s in stats if s.size_diff > 0)
    peak_mb  = peak_b / (1024 ** 2)

    return elapsed, peak_mb


def run_benchmarks(data_paths: dict):
    """
    Main benchmark loop.
    Iterates over all (size × condition) combinations and all algorithms,
    prints live progress, collects results, then prints summary tables
    and saves a CSV.
    """

    # results[algo][size_label][condition] = (time, mem)  or  (None, None)
    results = {name: {label: {} for label in SIZES} for name in ALGORITHMS}
    csv_rows = []   # flat list for CSV export

    print("=" * 72)
    print("  PHASE 2 — BENCHMARKING")
    print("=" * 72)

    for label, size in SIZES.items():
        for condition in CONDITIONS:

            print(f"\n{'─'*72}")
            print(f"  ▶  Size: {size:>12,}  |  Condition: {condition.upper()}")
            print(f"{'─'*72}")
            print(f"  {'Algorithm':<18} {'Time (s)':>12}  {'Memory (MB)':>12}  Status")
            print(f"  {'─'*18} {'─'*12}  {'─'*12}  {'─'*10}")

            # Load the pre-generated data file once per (size, condition)
            path = data_paths[(label, condition)]
            data = _load_bin(path)

            # Increase recursion limit for Merge Sort on large inputs
            if size > 500_000:
                sys.setrecursionlimit(max(sys.getrecursionlimit(), size * 2))

            for algo_name, func in ALGORITHMS.items():

                # ── Skip slow O(n²) on large inputs ──────────────────────────
                if SKIP_SLOW_FOR_LARGE and algo_name in SLOW_ALGOS and size > SLOW_ALGO_MAX_SIZE:
                    results[algo_name][label][condition] = (None, None)
                    csv_rows.append({
                        "algorithm": algo_name, "size_label": label,
                        "size": size, "condition": condition,
                        "time_s": "", "memory_mb": "", "status": "SKIPPED"
                    })
                    print(f"  {algo_name:<18} {'—':>12}  {'—':>12}  SKIPPED (O(n²))")
                    continue

                # ── Run benchmark ─────────────────────────────────────────────
                try:
                    t, m = run_one(func, data)
                    results[algo_name][label][condition] = (t, m)
                    csv_rows.append({
                        "algorithm": algo_name, "size_label": label,
                        "size": size, "condition": condition,
                        "time_s": f"{t:.6f}", "memory_mb": f"{m:.4f}",
                        "status": "OK"
                    })
                    print(f"  {algo_name:<18} {t:>12.4f}  {m:>12.2f}  OK")

                except Exception as exc:
                    results[algo_name][label][condition] = (None, None)
                    csv_rows.append({
                        "algorithm": algo_name, "size_label": label,
                        "size": size, "condition": condition,
                        "time_s": "", "memory_mb": "", "status": f"ERROR"
                    })
                    print(f"  {algo_name:<18} {'—':>12}  {'—':>12}  ERROR: {exc}")

    return results, csv_rows


# ══════════════════════════════════════════════════════════════════════════════
# ❺  RESULTS DISPLAY & EXPORT
# ══════════════════════════════════════════════════════════════════════════════

def _fmt(val, decimals=4):
    return f"{val:.{decimals}f}" if val is not None else "—"


def print_summary(results):
    """Print per-condition time and memory tables for every size."""

    for condition in CONDITIONS:
        print(f"\n{'═'*72}")
        print(f"  RESULTS — Condition: {condition.upper()}")
        print(f"{'═'*72}")

        # ── Time table ────────────────────────────────────────────────────────
        print(f"\n  {'':4}Time (seconds)")
        size_headers = "".join(f"  {lbl.upper():>10}" for lbl in SIZES)
        print(f"  {'Algorithm':<18}{size_headers}")
        print(f"  {'─'*18}" + "".join(f"  {'─'*10}" for _ in SIZES))

        for name in ALGORITHMS:
            row = f"  {name:<18}"
            for label in SIZES:
                val = results[name][label].get(condition, (None, None))[0]
                row += f"  {_fmt(val):>10}"
            print(row)

        # ── Memory table ──────────────────────────────────────────────────────
        print(f"\n  {'':4}Memory (MB)")
        print(f"  {'Algorithm':<18}{size_headers}")
        print(f"  {'─'*18}" + "".join(f"  {'─'*10}" for _ in SIZES))

        for name in ALGORITHMS:
            row = f"  {name:<18}"
            for label in SIZES:
                val = results[name][label].get(condition, (None, None))[1]
                row += f"  {_fmt(val, 2):>10}"
            print(row)

    # ── Cross-condition quick-reference for Time ───────────────────────────────
    print(f"\n{'═'*72}")
    print("  QUICK REFERENCE — Time (s) across all conditions")
    print(f"{'═'*72}")

    col_labels = [f"{lbl}/{cond[:3]}" for lbl in SIZES for cond in CONDITIONS]
    header     = f"  {'Algorithm':<18}" + "".join(f"  {c:>11}" for c in col_labels)
    print(header)
    print(f"  {'─'*18}" + "".join(f"  {'─'*11}" for _ in col_labels))

    for name in ALGORITHMS:
        row = f"  {name:<18}"
        for label in SIZES:
            for condition in CONDITIONS:
                val = results[name][label].get(condition, (None, None))[0]
                row += f"  {_fmt(val):>11}"
        print(row)


def save_csv(csv_rows):
    """Write all benchmark results to a CSV file."""
    fields = ["algorithm", "size_label", "size", "condition",
              "time_s", "memory_mb", "status"]
    with open(RESULTS_CSV, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(csv_rows)
    print(f"\n  ✔  Results saved to {RESULTS_CSV}")


# ══════════════════════════════════════════════════════════════════════════════
# ❻  ENTRY POINT
# ══════════════════════════════════════════════════════════════════════════════

def main():
    wall_start = time.perf_counter()

    # Step 1 — generate / load data
    data_paths = ensure_data()

    # Step 2 — run all benchmarks
    results, csv_rows = run_benchmarks(data_paths)

    # Step 3 — print summary tables
    print_summary(results)

    # Step 4 — save CSV
    save_csv(csv_rows)

    wall_total = time.perf_counter() - wall_start
    print(f"\n{'═'*72}")
    print(f"  Total wall-clock time: {wall_total:.1f} s  ({wall_total/60:.1f} min)")
    print(f"{'═'*72}")

    print("""
  LEGEND
  ──────────────────────────────────────────────────────────────────────
  —          Algorithm skipped (O(n²) too slow for this input size)
  Time (s)   Wall-clock seconds measured with time.perf_counter()
  Memory MB  Peak heap allocated during sort (Python tracemalloc)

  ALGORITHM COMPLEXITY REFERENCE
  ──────────────────────────────────────────────────────────────────────
  Algorithm       Best       Average    Worst      Memory
  Quick Sort      O(n log n) O(n log n) O(n²)      O(log n)
  Merge Sort      O(n log n) O(n log n) O(n log n) O(n)
  Heap Sort       O(n log n) O(n log n) O(n log n) O(1)
  Bubble Sort     O(n)       O(n²)      O(n²)      O(1)
  Selection Sort  O(n²)      O(n²)      O(n²)      O(1)
  Insertion Sort  O(n)       O(n²)      O(n²)      O(1)
  Shaker Sort     O(n)       O(n²)      O(n²)      O(1)
  Counting Sort   O(n+k)     O(n+k)     O(n+k)     O(k)
  Radix Sort      O(nk)      O(nk)      O(nk)      O(n+k)
  Timsort         O(n)       O(n log n) O(n log n) O(n)
  ──────────────────────────────────────────────────────────────────────
  k = value range (Counting) or digit count (Radix)
""")


if __name__ == "__main__":
    main()