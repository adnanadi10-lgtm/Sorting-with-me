"""
╔══════════════════════════════════════════════════════════════════════════════╗
║        SORTING ALGORITHM BENCHMARK v2 — Python Implementation               ║
║                                                                              ║
║  Algorithms (12 variants):                                                  ║
║    Quick Sort     — iterative (3-way partition, median-3 pivot)             ║
║    Quick Sort     — recursive (3-way partition, median-3 pivot)             ║
║    Merge Sort     — recursive (top-down)                                    ║
║    Merge Sort     — iterative (bottom-up)                                   ║
║    Heap Sort, Bubble Sort, Selection Sort, Insertion Sort                   ║
║    Shaker Sort, Counting Sort, Radix Sort, Timsort                          ║
║                                                                              ║
║  Sizes : 10 | 20 | 50 | 100 | 1k | 10k | 100k | 1m | 10m | 100m           ║
║                                                                              ║
║  Conditions : random | sorted | reverse | nearly_sorted | all_identical     ║
║                                                                              ║
║  Distributions:                                                              ║
║    sequential — [0, n),   no duplicates                                     ║
║    three_vals — {0,1,2},  heavy duplicates                                  ║
║    wide_range — [0,n^3),  random, duplicates allowed                        ║
║                                                                              ║
║  Metrics per run:                                                            ║
║    time_s        wall-clock seconds  (time.perf_counter)                    ║
║    memory_mb     RSS after sort (MB via tracemalloc peak)                   ║
║    comparisons   element comparisons inside sort                            ║
║    swaps         element writes / swaps inside sort                         ║
║    stable        verified stable/unstable for sizes <= 50K                  ║
║    variant       iterative | recursive | hybrid                             ║
║    complexity_*  theoretical best/avg/worst/space                           ║
║                                                                              ║
║  Small sizes (<= 1K): each run repeated SMALL_RUNS=50 times,               ║
║    reporting min_time, avg_time, max_time                                   ║
║                                                                              ║
║  Run    : python sorting_benchmark_v2.py                                    ║
║  Output : terminal + results.csv                                            ║
╚══════════════════════════════════════════════════════════════════════════════╝
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
# 1  CONFIGURATION
# ══════════════════════════════════════════════════════════════════════════════

# NOTE: Python lists use ~36 bytes/int vs C's 4 bytes.
# 100M elements would require ~3.6 GB — exceeds typical RAM.
# Max practical size for Python is 10M (~360 MB per list).
# The C version covers 100M.
SIZES = [10, 20, 50, 100, 1_000,
         10_000, 100_000, 1_000_000,
         10_000_000]

SIZE_LABELS = ["10","20","50","100","1k",
               "10k","100k","1m","10m"]

CONDITIONS   = ["random","sorted","reverse","nearly_sorted","all_identical"]
DIST_NAMES   = ["sequential","three_vals","wide_range"]

DATA_DIR             = "benchmark_data"
RESULTS_CSV          = "python_results.csv"
SMALL_SIZE_THRESHOLD = 1_000
SMALL_RUNS           = 50
SLOW_ALGO_MAX_SIZE   = 10_000
SKIP_SLOW_FOR_LARGE  = True
NEARLY_SORTED_FRAC   = 0.05
TIMEOUT_SECONDS      = 180.0   # max seconds per single sort run
STABILITY_MAX        = 50_000

SLOW_ALGOS = {
    "Bubble Sort", "Selection Sort", "Insertion Sort", "Shaker Sort"
}

# Theoretical complexity [best, avg, worst, space]
COMPLEXITY = {
    "Quick Sort (iter)": ("O(n log n)", "O(n log n)", "O(n^2)",     "O(log n)"),
    "Quick Sort (rec)":  ("O(n log n)", "O(n log n)", "O(n^2)",     "O(log n)"),
    "Merge Sort (rec)":  ("O(n log n)", "O(n log n)", "O(n log n)", "O(n)"),
    "Merge Sort (iter)": ("O(n log n)", "O(n log n)", "O(n log n)", "O(n)"),
    "Heap Sort":         ("O(n log n)", "O(n log n)", "O(n log n)", "O(1)"),
    "Bubble Sort":       ("O(n)",       "O(n^2)",     "O(n^2)",     "O(1)"),
    "Selection Sort":    ("O(n^2)",     "O(n^2)",     "O(n^2)",     "O(1)"),
    "Insertion Sort":    ("O(n)",       "O(n^2)",     "O(n^2)",     "O(1)"),
    "Shaker Sort":       ("O(n)",       "O(n^2)",     "O(n^2)",     "O(1)"),
    "Counting Sort":     ("O(n+k)",     "O(n+k)",     "O(n+k)",     "O(k)"),
    "Radix Sort":        ("O(nk)",      "O(nk)",       "O(nk)",     "O(n+k)"),
    "Timsort":           ("O(n)",       "O(n log n)", "O(n log n)", "O(n)"),
}

KNOWN_STABLE = {
    "Quick Sort (iter)": False,
    "Quick Sort (rec)":  False,
    "Merge Sort (rec)":  True,
    "Merge Sort (iter)": True,
    "Heap Sort":         False,
    "Bubble Sort":       True,
    "Selection Sort":    False,
    "Insertion Sort":    True,
    "Shaker Sort":       True,
    "Counting Sort":     True,
    "Radix Sort":        True,
    "Timsort":           True,
}

ALGO_VARIANT = {
    "Quick Sort (iter)": "iterative",
    "Quick Sort (rec)":  "recursive",
    "Merge Sort (rec)":  "recursive",
    "Merge Sort (iter)": "iterative",
    "Heap Sort":         "iterative",
    "Bubble Sort":       "iterative",
    "Selection Sort":    "iterative",
    "Insertion Sort":    "iterative",
    "Shaker Sort":       "iterative",
    "Counting Sort":     "iterative",
    "Radix Sort":        "iterative",
    "Timsort":           "hybrid",
}

# ══════════════════════════════════════════════════════════════════════════════
# 2  GLOBAL COUNTERS
#    Python can't use macros, so algorithms receive a counter object and
#    call counter.cmp() / counter.swap() — same effect as C macros.
# ══════════════════════════════════════════════════════════════════════════════

class Counter:
    __slots__ = ("_comparisons", "_swaps", "start_time", "timeout", "_check_interval")
    def __init__(self, start_time=0.0, timeout=180.0):
        self._comparisons    = 0
        self._swaps          = 0
        self.start_time      = start_time
        self.timeout         = timeout
        self._check_interval = 1000  # Check time every 1000 operations

    @property
    def comparisons(self):
        return self._comparisons

    @comparisons.setter
    def comparisons(self, val):
        self._comparisons = val
        if (self._comparisons + self._swaps) % self._check_interval == 0:
            self.check_timeout()

    @property
    def swaps(self):
        return self._swaps

    @swaps.setter
    def swaps(self, val):
        self._swaps = val
        if (self._comparisons + self._swaps) % self._check_interval == 0:
            self.check_timeout()

    def cmp(self, a, b) -> bool:
        """Increment comparison count and return a > b."""
        self.comparisons += 1
        return a > b

    def swap(self, arr, i, j):
        """Increment swap count and swap arr[i], arr[j] in place."""
        self.swaps += 1
        arr[i], arr[j] = arr[j], arr[i]

    def reset(self, start_time=0.0, timeout=180.0, n=0):
        self._comparisons = 0
        self._swaps       = 0
        self.start_time   = start_time
        self.timeout      = timeout
        # Dynamic check interval: check less frequently for larger N
        if n > 1_000_000:
            self._check_interval = 50_000
        elif n > 100_000:
            self._check_interval = 10_000
        else:
            self._check_interval = 1_000

    def check_timeout(self):
        if self.timeout > 0 and (time.perf_counter() - self.start_time) > self.timeout:
            raise TimeoutError("Algorithm exceeded time limit")


# ══════════════════════════════════════════════════════════════════════════════
# 3  DATA GENERATION & I/O
#    Uses the same LCG as the C version (seed 12345678, multiplier 1664525,
#    increment 1013904223) so data files are identical and interchangeable.
# ══════════════════════════════════════════════════════════════════════════════

LCG_MASK = 0xFFFFFFFF

def _lcg_sequence(n: int, seed: int = 12345678) -> list:
    """Generate n LCG values using the same constants as the C version."""
    state = seed
    out   = []
    for _ in range(n):
        state = (state * 1664525 + 1013904223) & LCG_MASK
        out.append(state)
    return out


def _fisher_yates(arr: list, rng: list) -> list:
    """In-place Fisher-Yates shuffle using pre-generated LCG values."""
    n = len(arr)
    for i in range(n - 1, 0, -1):
        j = rng[n - 1 - i] % (i + 1)
        arr[i], arr[j] = arr[j], arr[i]
    return arr


def generate(n: int, dist: str, cond: str) -> list:
    """
    Build a list of n integers matching the C version's generate() exactly.
    dist : 'sequential' | 'three_vals' | 'wide_range'
    cond : 'random' | 'sorted' | 'reverse' | 'nearly_sorted' | 'all_identical'
    """
    if cond == "all_identical":
        return [0] * n

    rng = _lcg_sequence(max(n * 2, 1))  # enough values for shuffle + swaps

    # Step 1: fill base values
    if dist == "sequential":
        arr = list(range(n))
    elif dist == "three_vals":
        arr = [rng[i] % 3 for i in range(n)]
    else:  # wide_range
        limit = min(n ** 3, 2_000_000_000)
        arr   = [rng[i] % limit for i in range(n)]

    # Step 2: apply ordering
    if cond == "random":
        _fisher_yates(arr, rng)
    elif cond == "sorted":
        arr.sort()
    elif cond == "reverse":
        arr.sort()
        arr.reverse()
    elif cond == "nearly_sorted":
        arr.sort()
        swaps = max(1, int(n * NEARLY_SORTED_FRAC))
        rng2  = _lcg_sequence(swaps * 2, seed=rng[n - 1])
        for s in range(swaps):
            a = rng2[s * 2]     % n
            b = rng2[s * 2 + 1] % n
            arr[a], arr[b] = arr[b], arr[a]

    return arr


def save_bin(data: list, path: str):
    with open(path, "wb") as f:
        f.write(struct.pack(f"<{len(data)}i", *data))


def load_bin(path: str) -> list:
    with open(path, "rb") as f:
        raw = f.read()
    count = len(raw) // 4
    return list(struct.unpack(f"<{count}i", raw))


# ══════════════════════════════════════════════════════════════════════════════
# 4  SORTING ALGORITHMS
#    Each function takes (arr, counter) where arr is a list to sort IN PLACE
#    and counter is a Counter instance for tracking comparisons/swaps.
# ══════════════════════════════════════════════════════════════════════════════

# ── Quick Sort — 3-way partition (Dutch National Flag) ────────────────────────

def _qs_3way(arr, lo, hi, c):
    """3-way partition: returns (lt, gt) where arr[lt..gt] == pivot."""
    mid = lo + (hi - lo) // 2
    # median-of-three: sort arr[lo], arr[mid], arr[hi]
    if c.cmp(arr[mid], arr[lo]):  c.swap(arr, mid, lo)
    if c.cmp(arr[hi],  arr[lo]):  c.swap(arr, hi,  lo)
    if c.cmp(arr[mid], arr[hi]):  c.swap(arr, mid, hi)
    pivot = arr[hi]

    lt, gt, i = lo, hi, lo
    while i <= gt:
        c.comparisons += 1
        if arr[i] < pivot:
            c.swap(arr, lt, i); lt += 1; i += 1
        else:
            c.comparisons += 1
            if arr[i] > pivot:
                c.swap(arr, i, gt); gt -= 1
            else:
                i += 1
    return lt, gt


def quick_sort_iter(arr, c):
    n = len(arr)
    if n <= 1: return
    stack = [(0, n - 1)]
    while stack:
        lo, hi = stack.pop()
        if lo < hi:
            lt, gt = _qs_3way(arr, lo, hi, c)
            if lo   < lt - 1: stack.append((lo,    lt - 1))
            if gt + 1 < hi:   stack.append((gt + 1, hi))


def _quick_rec(arr, lo, hi, c):
    if lo >= hi: return
    lt, gt = _qs_3way(arr, lo, hi, c)
    _quick_rec(arr, lo,    lt - 1, c)
    _quick_rec(arr, gt + 1, hi,    c)


def quick_sort_rec(arr, c):
    if len(arr) <= 1: return
    sys.setrecursionlimit(max(sys.getrecursionlimit(), len(arr) * 3))
    _quick_rec(arr, 0, len(arr) - 1, c)


# ── Merge Sort ────────────────────────────────────────────────────────────────

def _merge(arr, l, m, r, c):
    left  = arr[l:m+1]
    right = arr[m+1:r+1]
    i = j = 0
    k = l
    while i < len(left) and j < len(right):
        c.comparisons += 1
        if left[i] <= right[j]:
            arr[k] = left[i];  i += 1
        else:
            arr[k] = right[j]; j += 1; c.swaps += 1
        k += 1
    while i < len(left):
        arr[k] = left[i]; i += 1; k += 1
    while j < len(right):
        arr[k] = right[j]; j += 1; k += 1; c.swaps += 1
    c.swaps += r - l + 1   # count the write-back


def _merge_rec(arr, l, r, c):
    if l >= r: return
    m = l + (r - l) // 2
    _merge_rec(arr, l,   m, c)
    _merge_rec(arr, m+1, r, c)
    _merge(arr, l, m, r, c)


def merge_sort_rec(arr, c):
    if len(arr) <= 1: return
    sys.setrecursionlimit(max(sys.getrecursionlimit(), len(arr) * 3))
    _merge_rec(arr, 0, len(arr) - 1, c)


def merge_sort_iter(arr, c):
    n = len(arr)
    if n <= 1: return
    width = 1
    while width < n:
        for l in range(0, n, 2 * width):
            m = min(l + width - 1, n - 1)
            r = min(l + 2 * width - 1, n - 1)
            if m < r:
                _merge(arr, l, m, r, c)
        width *= 2


# ── Heap Sort ─────────────────────────────────────────────────────────────────

def _heapify(arr, n, i, c):
    while True:
        largest = i
        l, r = 2*i+1, 2*i+2
        if l < n and c.cmp(arr[l], arr[largest]): largest = l
        if r < n and c.cmp(arr[r], arr[largest]): largest = r
        if largest == i: break
        c.swap(arr, i, largest)
        i = largest


def heap_sort(arr, c):
    n = len(arr)
    for i in range(n // 2 - 1, -1, -1):
        _heapify(arr, n, i, c)
    for i in range(n - 1, 0, -1):
        c.swap(arr, 0, i)
        _heapify(arr, i, 0, c)


# ── Bubble Sort ───────────────────────────────────────────────────────────────

def bubble_sort(arr, c):
    n = len(arr)
    for i in range(n - 1):
        swapped = False
        for j in range(n - i - 1):
            if c.cmp(arr[j], arr[j+1]):
                c.swap(arr, j, j+1); swapped = True
        if not swapped: break


# ── Selection Sort ────────────────────────────────────────────────────────────

def selection_sort(arr, c):
    n = len(arr)
    for i in range(n - 1):
        mi = i
        for j in range(i+1, n):
            if c.cmp(arr[j], arr[mi]): mi = j
        if mi != i: c.swap(arr, i, mi)


# ── Insertion Sort ────────────────────────────────────────────────────────────

def insertion_sort(arr, c):
    for i in range(1, len(arr)):
        key = arr[i]
        j   = i - 1
        while j >= 0:
            c.comparisons += 1
            if arr[j] > key:
                arr[j+1] = arr[j]; c.swaps += 1; j -= 1
            else:
                break
        arr[j+1] = key


# ── Shaker Sort ───────────────────────────────────────────────────────────────

def shaker_sort(arr, c):
    start, end = 0, len(arr) - 1
    while True:
        swapped = False
        for i in range(start, end):
            if c.cmp(arr[i], arr[i+1]):
                c.swap(arr, i, i+1); swapped = True
        if not swapped: break
        end -= 1
        swapped = False
        for i in range(end - 1, start - 1, -1):
            if c.cmp(arr[i], arr[i+1]):
                c.swap(arr, i, i+1); swapped = True
        start += 1
        if not swapped: break


# ── Counting Sort ─────────────────────────────────────────────────────────────

def counting_sort(arr, c):
    if not arr: return
    mn, mx = min(arr), max(arr)
    c.comparisons += len(arr)       # min/max scan counts as comparisons
    rang  = mx - mn + 1
    count = [0] * rang
    for x in arr:
        count[x - mn] += 1
    idx = 0
    for i in range(rang):
        while count[i] > 0:
            arr[idx] = i + mn; c.swaps += 1
            idx += 1; count[i] -= 1


# ── Radix Sort (LSD, base 10) ─────────────────────────────────────────────────

def _radix_pass(arr, exp, c):
    n      = len(arr)
    output = [0] * n
    count  = [0] * 10
    for x in arr:
        count[(x // exp) % 10] += 1
    for i in range(1, 10):
        count[i] += count[i-1]
    for i in range(n - 1, -1, -1):
        d = (arr[i] // exp) % 10
        output[count[d] - 1] = arr[i]; c.swaps += 1
        count[d] -= 1
    arr[:] = output


def radix_sort(arr, c):
    if not arr: return
    mx  = max(arr)
    exp = 1
    while mx // exp > 0:
        _radix_pass(arr, exp, c)
        exp *= 10


# ── Timsort (Python's built-in, hybrid insertion+merge) ───────────────────────

TIM_RUN = 32

def _tim_insertion(arr, l, r, c):
    for i in range(l+1, r+1):
        key = arr[i]; j = i - 1
        while j >= l:
            c.comparisons += 1
            if arr[j] > key:
                arr[j+1] = arr[j]; c.swaps += 1; j -= 1
            else:
                break
        arr[j+1] = key


def _tim_merge(arr, l, m, r, c):
    left  = arr[l:m+1]
    right = arr[m+1:r+1]
    i = j = 0; k = l
    while i < len(left) and j < len(right):
        c.comparisons += 1
        c.swaps += 1
        if left[i] <= right[j]:
            arr[k] = left[i];  i += 1
        else:
            arr[k] = right[j]; j += 1
        k += 1
    while i < len(left):
        arr[k] = left[i];  i += 1; k += 1; c.swaps += 1
    while j < len(right):
        arr[k] = right[j]; j += 1; k += 1; c.swaps += 1



def timsort(arr, c):
    n = len(arr)
    for i in range(0, n, TIM_RUN):
        _tim_insertion(arr, i, min(i + TIM_RUN - 1, n - 1), c)
    size = TIM_RUN
    while size < n:
        for l in range(0, n, 2 * size):
            m = l + size - 1
            r = min(l + 2*size - 1, n - 1)
            if m < r:
                _tim_merge(arr, l, m, r, c)
        size *= 2


# ── Algorithm registry ────────────────────────────────────────────────────────

ALGORITHMS = [
    ("Quick Sort (iter)",  quick_sort_iter),
    ("Quick Sort (rec)",   quick_sort_rec),
    ("Merge Sort (rec)",   merge_sort_rec),
    ("Merge Sort (iter)",  merge_sort_iter),
    ("Heap Sort",          heap_sort),
    ("Bubble Sort",        bubble_sort),
    ("Selection Sort",     selection_sort),
    ("Insertion Sort",     insertion_sort),
    ("Shaker Sort",        shaker_sort),
    ("Counting Sort",      counting_sort),
    ("Radix Sort",         radix_sort),
    ("Timsort",            timsort),
]

# ══════════════════════════════════════════════════════════════════════════════
# 5  STABILITY VERIFICATION
#    Sort list of (value, original_index) pairs using the algorithm on the
#    values only, then check equal values kept ascending index order.
#    Only run for n <= STABILITY_MAX.
# ══════════════════════════════════════════════════════════════════════════════

def verify_stable(fn, data: list) -> str:
    n = len(data)
    if n > STABILITY_MAX:
        return "not_checked"

    # Build (value, index) pairs and sort them with a known-stable reference
    pairs = [(v, i) for i, v in enumerate(data)]
    ref   = sorted(pairs, key=lambda x: x[0])   # Python sort is stable

    # Run the algorithm on a plain copy and check value correctness
    arr = data[:]
    dummy = Counter(timeout=0) # Disable timeout for verification phase
    fn(arr, dummy)

    # Check sorted values match
    ref_vals = [p[0] for p in ref]
    if arr != ref_vals:
        return "unstable"  # wrong sort result — treat as unstable

    # Check consecutive equal values in reference kept ascending indices
    for i in range(1, len(ref)):
        if ref[i][0] == ref[i-1][0] and ref[i][1] < ref[i-1][1]:
            return "unstable"

    return "stable"


# ══════════════════════════════════════════════════════════════════════════════
# 6  MEMORY MEASUREMENT
#    tracemalloc tracks Python heap allocations.
#    Returns peak MB allocated during the sort call.
# ══════════════════════════════════════════════════════════════════════════════

def get_memory_mb() -> float:
    try:
        import psutil, os
        process = psutil.Process(os.getpid())
        return process.memory_info().rss / (1024 * 1024)
    except ImportError:
        pass
    # fallback: tracemalloc peak
    _, peak = tracemalloc.get_traced_memory()
    return peak / (1024 * 1024)


# ══════════════════════════════════════════════════════════════════════════════
# 7  BENCHMARK ENGINE
# ══════════════════════════════════════════════════════════════════════════════

def run_benchmark(name: str, fn, data: list) -> dict:
    n      = len(data)
    reps   = SMALL_RUNS if n <= SMALL_SIZE_THRESHOLD else 1
    c      = Counter()

    times      = []
    total_cmp  = 0
    total_sw   = 0
    mem_mb     = 0.0

    tracemalloc.start()

    for rep in range(reps):
        arr = data[:]
        t_start = time.perf_counter()
        c.reset(start_time=t_start, timeout=TIMEOUT_SECONDS, n=n)

        try:
            fn(arr, c)
            elapsed = time.perf_counter() - t_start
        except TimeoutError:
            elapsed = time.perf_counter() - t_start
            tracemalloc.stop()
            # "Announce" the partial results and state
            preview = str(arr[:10]) + ("..." if len(arr) > 10 else "")
            print(f"\n  [TIMEOUT] {name} stopped at {elapsed:.2f}s!")
            print(f"  Partial progress: {c.comparisons} compares, {c.swaps} swaps.")
            print(f"  Array preview: {preview}")
            
            return {
                "time_min":    elapsed,
                "time_avg":    elapsed,
                "time_max":    elapsed,
                "memory_mb":   mem_mb,
                "comparisons": c.comparisons,
                "swaps":       c.swaps,
                "stable":      "not_checked",
                "runs":        rep + 1,
                "skipped":     False,
                "error":       None,
                "timed_out":   True,
            }

        times.append(elapsed)
        total_cmp += c.comparisons
        total_sw  += c.swaps

        if rep == 0:
            _, peak = tracemalloc.get_traced_memory()
            mem_mb  = peak / (1024 * 1024)

    tracemalloc.stop()


    stable = verify_stable(fn, data) if n <= STABILITY_MAX else "not_checked"

    return {
        "time_min":    min(times),
        "time_avg":    sum(times) / reps,
        "time_max":    max(times),
        "memory_mb":   mem_mb,
        "comparisons": total_cmp // reps,
        "swaps":       total_sw  // reps,
        "stable":      stable,
        "runs":        reps,
        "skipped":     False,
        "error":       None,
        "timed_out":   False,
    }


# ══════════════════════════════════════════════════════════════════════════════
# 8  PRETTY PRINT HELPERS
# ══════════════════════════════════════════════════════════════════════════════

def _bar(char="-", width=80): return char * width

def _fmt_count(n: int) -> str:
    if   n >= 1_000_000_000: return f"{n/1e9:.2f}B"
    elif n >= 1_000_000:     return f"{n/1e6:.2f}M"
    elif n >= 1_000:         return f"{n/1e3:.1f}K"
    else:                    return str(n)


# ══════════════════════════════════════════════════════════════════════════════
# 9  MAIN
# ══════════════════════════════════════════════════════════════════════════════

def main():
    wall_start = time.perf_counter()

    # results[dist][algo_name][size_label][condition] = result_dict
    results = {
        dist: {
            name: {lbl: {} for lbl in SIZE_LABELS}
            for name, _ in ALGORITHMS
        }
        for dist in DIST_NAMES
    }

    # ── PHASE 1: DATA GENERATION ─────────────────────────────────────────────
    print(_bar("="))
    print("  PHASE 1 -- DATA GENERATION")
    print(_bar("="))

    os.makedirs(DATA_DIR, exist_ok=True)

    # data_paths[dist][size_label][condition] = filepath
    data_paths = {}
    total_files   = len(DIST_NAMES) * len(SIZES) * len(CONDITIONS)
    new_files     = 0

    for dist in DIST_NAMES:
        print(f"\n  Distribution: {dist}")
        data_paths[dist] = {}

        for lbl, size in zip(SIZE_LABELS, SIZES):
            data_paths[dist][lbl] = {}

            for cond in CONDITIONS:
                fname = f"data_{dist}_{lbl}_{cond}.bin"
                fpath = os.path.join(DATA_DIR, fname)
                data_paths[dist][lbl][cond] = fpath

                if os.path.exists(fpath):
                    mb = os.path.getsize(fpath) / (1024**2)
                    print(f"    [SKIP] {fname} ({mb:.1f}MB)")
                else:
                    print(f"    [GEN ] {fname} ... ", end="", flush=True)
                    t0   = time.perf_counter()
                    data = generate(size, dist, cond)
                    save_bin(data, fpath)
                    elapsed = time.perf_counter() - t0
                    mb      = os.path.getsize(fpath) / (1024**2)
                    print(f"done {elapsed:.2f}s {mb:.1f}MB")
                    new_files += 1

    print(f"\n  Data ready — {new_files} new, {total_files} total files in ./{DATA_DIR}/\n")

    fields = [
        "distribution","algorithm","variant","size_label","size","condition",
        "runs","time_min_s","time_avg_s","time_max_s","memory_mb",
        "comparisons","swaps","stable_verified","known_stable",
        "complexity_best","complexity_avg","complexity_worst","complexity_space",
        "status"
    ]

    # ── RESUME LOGIC: Load existing results ──
    completed_cases = set()
    if os.path.exists(RESULTS_CSV):
        try:
            with open(RESULTS_CSV, "r", newline="") as f:
                reader = csv.DictReader(f)
                for row in reader:
                    key = (row["distribution"], row["algorithm"], row["size_label"], row["condition"])
                    completed_cases.add(key)
            print(f"  [RESUME] Found {len(completed_cases)} existing results in {RESULTS_CSV}")
        except Exception as e:
            print(f"  [RESUME] Could not read {RESULTS_CSV}, starting fresh. Error: {e}")

    # Ensure header exists if file is new
    if not os.path.exists(RESULTS_CSV):
        with open(RESULTS_CSV, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fields)
            writer.writeheader()

    def append_result(dist, name, lbl, size, cond, r, status="OK"):
        cplx = COMPLEXITY[name]
        row = {
            "distribution":     dist,
            "algorithm":        name,
            "variant":          ALGO_VARIANT[name],
            "size_label":       lbl,
            "size":             size,
            "condition":        cond,
            "runs":             r.get("runs", ""),
            "time_min_s":       f"{r['time_min']:.8f}" if "time_min" in r else "",
            "time_avg_s":       f"{r['time_avg']:.8f}" if "time_avg" in r else "",
            "time_max_s":       f"{r['time_max']:.8f}" if "time_max" in r else "",
            "memory_mb":        f"{r['memory_mb']:.4f}" if "memory_mb" in r else "",
            "comparisons":      r.get("comparisons", ""),
            "swaps":            r.get("swaps", ""),
            "stable_verified":  r.get("stable", ""),
            "known_stable":     "stable" if KNOWN_STABLE[name] else "unstable",
            "complexity_best":  cplx[0],
            "complexity_avg":   cplx[1],
            "complexity_worst": cplx[2],
            "complexity_space": cplx[3],
            "status":           status,
        }
        with open(RESULTS_CSV, "a", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fields)
            writer.writerow(row)

    # ── PHASE 2: BENCHMARKING ────────────────────────────────────────────────
    print(_bar("="))
    print("  PHASE 2 -- BENCHMARKING")
    print(_bar("="))

    # Cache for loaded data to avoid re-reading binary files redundantly
    current_data = None
    current_data_key = None

    for dist in DIST_NAMES:
        print(f"\n{'*'*80}")
        print(f"  DISTRIBUTION: {dist}")
        print(f"{'*'*80}")

        for lbl, size in zip(SIZE_LABELS, SIZES):
            is_small = size <= SMALL_SIZE_THRESHOLD

            for cond in CONDITIONS:
                repeat_note = "  [repeated 50x]" if is_small else ""
                
                # Check if all algorithms for this dist/size/cond are already done
                # This is a small optimization to avoid loading data if not needed
                all_done = True
                for name, _ in ALGORITHMS:
                    if (dist, name, lbl, cond) not in completed_cases:
                        all_done = False
                        break
                
                if all_done:
                    continue

                print(f"\n{_bar()}")
                print(f"  > Dist:{dist:<12}  Size:{lbl:<6}({size})  Cond:{cond}{repeat_note}")
                print(_bar())
                print(f"  {'Algorithm':<22} {'Time(s)':>9} {'Mem(MB)':>9} {'Compares':>10} {'Swaps':>10}  ST  Status")
                print(f"  {'-'*22} {'-'*9} {'-'*9} {'-'*10} {'-'*10}  --  ------")

                # Load data only once per condition block
                data_key = (dist, lbl, cond)
                if current_data_key != data_key:
                    current_data = load_bin(data_paths[dist][lbl][cond])
                    current_data_key = data_key

                for name, fn in ALGORITHMS:
                    # RESUME CHECK
                    if (dist, name, lbl, cond) in completed_cases:
                        print(f"  {name:<22} {'-':>9} {'-':>9} {'-':>10} {'-':>10}  -   ALREADY DONE")
                        continue

                    # Skip O(n^2) on large inputs
                    if SKIP_SLOW_FOR_LARGE and name in SLOW_ALGOS and size > SLOW_ALGO_MAX_SIZE:
                        results[dist][name][lbl][cond] = {"skipped": True}
                        append_result(dist, name, lbl, size, cond, {}, "SKIPPED")
                        print(f"  {name:<22} {'-':>9} {'-':>9} {'-':>10} {'-':>10}  -   SKIPPED")
                        continue

                    # Skip Counting Sort on wide_range (range = n^3 -> OOM)
                    if name == "Counting Sort" and dist == "wide_range":
                        results[dist][name][lbl][cond] = {"skipped": True}
                        append_result(dist, name, lbl, size, cond, {}, "SKIPPED(range=n^3)")
                        print(f"  {name:<22} {'-':>9} {'-':>9} {'-':>10} {'-':>10}  -   SKIPPED(range=n^3)")
                        continue

                    try:
                        r = run_benchmark(name, fn, current_data)
                        results[dist][name][lbl][cond] = r

                        cmp_s = _fmt_count(r["comparisons"])
                        sw_s  = _fmt_count(r["swaps"])
                        status = "TIMEOUT" if r["timed_out"] else "OK"
                        
                        append_result(dist, name, lbl, size, cond, r, status)

                        if r["timed_out"]:
                            print(f"  {name:<22} {r['time_avg']:>9.1f} {r['memory_mb']:>9.2f} "
                                  f"{cmp_s:>10} {sw_s:>10}  -   TIMEOUT (>{TIMEOUT_SECONDS:.0f}s)")
                        elif is_small:
                            st = {"stable":"Y","unstable":"N","not_checked":"-"}[r["stable"]]
                            print(f"  {name:<22} {r['time_avg']:>9.6f} {r['memory_mb']:>9.2f} "
                                  f"{cmp_s:>10} {sw_s:>10}  {st:<3} avg(min={r['time_min']:.6f})")
                        else:
                            st = {"stable":"Y","unstable":"N","not_checked":"-"}[r["stable"]]
                            print(f"  {name:<22} {r['time_avg']:>9.4f} {r['memory_mb']:>9.2f} "
                                  f"{cmp_s:>10} {sw_s:>10}  {st:<3} OK")

                    except Exception as e:
                        results[dist][name][lbl][cond] = {"skipped": False, "error": str(e)}
                        append_result(dist, name, lbl, size, cond, {}, f"ERROR: {e}")
                        print(f"  {name:<22} {'-':>9} {'-':>9} {'-':>10} {'-':>10}  -   ERROR: {e}")

                    sys.stdout.flush()


    # Phase 3 was integrated into Phase 2 for incremental saving.
    print(f"\n  All completed results are in {RESULTS_CSV}")


    wall_total = time.perf_counter() - wall_start
    print(f"\n{_bar('=')}")
    print(f"  Total wall-clock time: {wall_total:.1f}s ({wall_total/60:.1f} min)")
    print(_bar("="))
    print()


if __name__ == "__main__":
    main()