# Sorting Algorithm Benchmark v2

A comprehensive performance analysis tool that benchmarks **12 different sorting algorithms** across both **C** and **Python** implementations. This project evaluates algorithms under diverse data distributions, input sizes, and initial conditions to provide a holistic view of their efficiency, stability, and memory usage.

## Overview

This repository contains a high-performance benchmarking suite designed to compare the execution speed, memory footprint, and complexity of various sorting algorithms. By implementing the same logic in both C (native) and Python (interpreted), it allows for a direct comparison of language performance overhead in algorithmic tasks.

---

## Algorithms Implemented

The suite includes 12 variants, covering classic, efficient, and hybrid sorting techniques:

| Category | Algorithms |
| :--- | :--- |
| **Quadratic $O(n^2)$** | Bubble Sort, Selection Sort, Insertion Sort, Shaker Sort |
| **Log-linear $O(n \log n)$** | Quick Sort (Iterative & Recursive), Merge Sort (Iterative & Recursive), Heap Sort |
| **Linear / Hybrid** | Counting Sort, Radix Sort (LSD), Timsort (Hybrid) |

---

## Benchmark Parameters

Each algorithm is tested against a rigorous set of conditions:

### Input Sizes
- **C Implementation:** 10, 20, 50, 100, 1K, 10K, 100K, 1M, 10M, **100M**
- **Python Implementation:** 10 to **10M** (limited due to memory overhead per object)

### Data Distributions
1.  **Sequential:** $[0, n)$ range, no duplicates.
2.  **Three Values:** $\{0, 1, 2\}$ only, heavy duplicates.
3.  **Wide Range:** $[0, n^3)$ range, random with allowed duplicates.

### Initial Conditions
- `random`: Uniformly shuffled data.
- `sorted`: Data already in ascending order.
- `reverse`: Data in descending order.
- `nearly_sorted`: 95% sorted with 5% random swaps.
- `all_identical`: Every element is the same (critical for testing Quick Sort pivots).

---

## Key Metrics Captured

For every single run, the suite records:
- **Wall-clock Time:** Minimum, Average, and Maximum across repetitions (for small sizes).
- **Memory RSS:** Peak memory usage during the sorting process (MB).
- **Comparisons:** Total count of element-to-element comparisons.
- **Swaps/Writes:** Total count of element movements or writes.
- **Stability:** Verification if equal elements preserve their relative order.
- **Status:** Marks success, Timeout (>180s), or Skipped (for $O(n^2)$ on large N).

---

## Usage

### C Implementation
The C version is optimized for high-speed execution and supports massive datasets up to 100M elements.

**Compilation:**
```bash
gcc -O2 -o benchmark sorting.c
```

**Execution:**
```bash
./benchmark
```
*Outputs results to `c_results.csv` and generates binary test data in `benchmark_data/`.*

---

### Python Implementation
The Python version mirrors the C logic exactly (using the same LCG seed for data generation) to ensure fair comparison.

**Execution:**
```bash
python sorting.py
```
*Outputs results to `python_results.csv`.*

---

## Project Structure

- `sorting.c`: Core C benchmarking engine.
- `sorting.py`: Core Python benchmarking engine.
- `benchmark_data/`: Directory containing generated `.bin` datasets (interchangeable between C and Python).
- `c_results.csv`: Collected metrics from the C runs.
- `python_results.csv`: Collected metrics from the Python runs.
- `SortingWithAdnan.pdf`: Summary report and analysis of the findings.

---

## Implementation Details
- **Pivot Selection:** Quick Sort uses a **median-of-three** strategy combined with **3-way partitioning** (Dutch National Flag) to handle duplicate-heavy and identical arrays gracefully.
- **Safety:** $O(n^2)$ algorithms are automatically skipped for input sizes exceeding 100K (C) or 10K (Python) to prevent excessive runtimes.
- **Interchangeability:** Both implementations use an identical Linear Congruential Generator (LCG) with the same constants, ensuring they sort the exact same data sequences.

---

## Author
**Adnan**

---
*Developed for performance analysis and algorithmic study.*
