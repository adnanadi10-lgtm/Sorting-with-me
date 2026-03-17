/*
╔══════════════════════════════════════════════════════════════════════════════╗
║          SORTING ALGORITHM BENCHMARK — C Implementation                     ║
║                                                                              ║
║  Algorithms : Quick Sort, Merge Sort, Heap Sort, Bubble Sort,               ║
║               Selection Sort, Insertion Sort, Shaker Sort,                  ║
║               Counting Sort, Radix Sort, Timsort                            ║
║                                                                              ║
║  Input sizes: 10 | 20 | 50 | 100 | 1k | 100k | 1m | 10m | 100m            ║
║                                                                              ║
║  Conditions : random | sorted | reverse | nearly_sorted                     ║
║                                                                              ║
║  Distributions:                                                              ║
║    sequential  — values [0, n),      no duplicates (permutation)            ║
║    three_vals  — only {0, 1, 2},     heavy duplicates                       ║
║    wide_range  — values [0, n^3),    duplicates allowed                     ║
║                                                                              ║
║  Metrics    : wall-clock time (seconds) | RSS memory (MB via /proc)         ║
║                                                                              ║
║  Compile    : gcc -O2 -o benchmark sorting_benchmark.c                      ║
║  Run        : ./benchmark                                                    ║
║  Output     : terminal + results.csv                                        ║
╚══════════════════════════════════════════════════════════════════════════════╝

NOTES:
  • O(n^2) algorithms (Bubble, Selection, Insertion, Shaker) are SKIPPED
    for sizes > 100K — set SKIP_SLOW_FOR_LARGE to 0 to force them.
  • Counting Sort is SKIPPED for wide_range distribution: the value range
    is n^3, which would require gigabytes of count-array memory.
  • nearly_sorted: sorted array with ~5% of elements randomly swapped.
  • three_vals sorted/reverse: fill random {0,1,2}, then sort/reverse so
    the condition is meaningful even with only 3 distinct values.
  • Data files saved to ./benchmark_data/ and reused on re-runs.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
  #include <windows.h>
  #include <psapi.h>
  #include <direct.h>
  #define PATH_SEP "\\"
  #define mkdir(p,m) _mkdir(p)
#else
  #include <sys/time.h>
  #define PATH_SEP "/"
#endif

/* ══════════════════════════════════════════════════════════════════════════
   1  CONFIGURATION
   ══════════════════════════════════════════════════════════════════════════ */

#define NUM_SIZES         9
#define NUM_CONDITIONS    4
#define NUM_DISTRIBUTIONS 3
#define NUM_ALGORITHMS    10

static const int   SIZES[]       = {10, 20, 50, 100, 1000,
                                     100000, 1000000, 10000000, 100000000};
static const char *SIZE_LABELS[] = {"10","20","50","100","1k",
                                     "100k","1m","10m","100m"};

/* Condition indices */
#define COND_RANDOM         0
#define COND_SORTED         1
#define COND_REVERSE        2
#define COND_NEARLY_SORTED  3
static const char *CONDITIONS[] = {
    "random", "sorted", "reverse", "nearly_sorted"
};

/* Distribution indices */
#define DIST_SEQUENTIAL  0   /* [0, n)  — no duplicates                  */
#define DIST_THREE_VALS  1   /* {0,1,2} — heavy duplicates               */
#define DIST_WIDE_RANGE  2   /* [0, n^3) — random, duplicates allowed    */
static const char *DIST_NAMES[] = {
    "sequential", "three_vals", "wide_range"
};

#define DATA_DIR             "benchmark_data"
#define RESULTS_CSV          "results.csv"
#define SLOW_ALGO_MAX_SIZE    100000
#define SKIP_SLOW_FOR_LARGE   1     /* set 0 to run O(n^2) on all sizes */

/* Fraction of elements swapped for nearly_sorted (5%) */
#define NEARLY_SORTED_FRAC    0.05

/* Counting Sort index — skipped on wide_range (range = n^3 -> OOM) */
#define COUNTING_SORT_IDX     7

static const char *ALGO_NAMES[NUM_ALGORITHMS] = {
    "Quick Sort",  "Merge Sort",     "Heap Sort",
    "Bubble Sort", "Selection Sort", "Insertion Sort",
    "Shaker Sort", "Counting Sort",  "Radix Sort",
    "Timsort"
};

static const int IS_SLOW[NUM_ALGORITHMS] = {
    0, 0, 0,   /* Quick, Merge, Heap      */
    1, 1, 1,   /* Bubble, Select, Insert  */
    1, 0, 0,   /* Shaker, Counting, Radix */
    0          /* Timsort                 */
};

/* ══════════════════════════════════════════════════════════════════════════
   2  TIMING & MEMORY
   ══════════════════════════════════════════════════════════════════════════ */

static double get_time_sec(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

static double get_memory_mb(void) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
    return -1.0;
#else
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1.0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            fclose(f);
            long kb = 0;
            sscanf(line + 6, "%ld", &kb);
            return (double)kb / 1024.0;
        }
    }
    fclose(f);
    return -1.0;
#endif
}

/* ══════════════════════════════════════════════════════════════════════════
   3  RNG
   ══════════════════════════════════════════════════════════════════════════ */

static uint32_t lcg_state = 12345678u;

static uint32_t lcg_next(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

/* Reset so every dataset is reproducible regardless of run order */
static void lcg_reset(void) { lcg_state = 12345678u; }

/* Fisher-Yates in-place shuffle */
static void shuffle(int *arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(lcg_next() % (uint32_t)(i + 1));
        int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

static int cmp_int_asc(const void *a, const void *b) {
    int x = *(const int*)a, y = *(const int*)b;
    return (x > y) - (x < y);
}

/* ══════════════════════════════════════════════════════════════════════════
   4  DATA GENERATION
   ══════════════════════════════════════════════════════════════════════════

   Two-step process:
     Step 1 — fill base values for the chosen distribution
     Step 2 — apply condition ordering on top of those values

   Distribution fills:
     DIST_SEQUENTIAL : arr[i] = i              -> [0, 1, ..., n-1]
     DIST_THREE_VALS : arr[i] = lcg() % 3      -> random mix of {0,1,2}
     DIST_WIDE_RANGE : arr[i] = lcg() % n^3    -> large random integers

   Condition ordering:
     random        -> shuffle
     sorted        -> qsort ascending
     reverse       -> qsort then reverse
     nearly_sorted -> qsort then swap ~5% random pairs
   ══════════════════════════════════════════════════════════════════════════ */

static void generate(int *arr, int n, int dist, int cond) {

    lcg_reset();

    /* ── Step 1: fill base values ── */
    if (dist == DIST_SEQUENTIAL) {
        for (int i = 0; i < n; i++) arr[i] = i;

    } else if (dist == DIST_THREE_VALS) {
        for (int i = 0; i < n; i++)
            arr[i] = (int)(lcg_next() % 3u);

    } else {
        /* DIST_WIDE_RANGE: range = n^3, capped at 2 000 000 000 to avoid
           overflow when n is large (e.g. n=100 000 000, n^3 >> INT_MAX) */
        uint64_t range;
        uint64_t n64 = (uint64_t)n;
        if (n64 * n64 * n64 > 2000000000ULL)
            range = 2000000000ULL;
        else
            range = n64 * n64 * n64;
        for (int i = 0; i < n; i++)
            arr[i] = (int)(lcg_next() % (uint32_t)range);
    }

    /* ── Step 2: apply ordering condition ── */
    switch (cond) {

        case COND_RANDOM:
            shuffle(arr, n);
            break;

        case COND_SORTED:
            qsort(arr, n, sizeof(int), cmp_int_asc);
            break;

        case COND_REVERSE:
            qsort(arr, n, sizeof(int), cmp_int_asc);
            for (int i = 0, j = n - 1; i < j; i++, j--) {
                int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
            }
            break;

        case COND_NEARLY_SORTED:
            qsort(arr, n, sizeof(int), cmp_int_asc);
            {
                int swaps = (int)(n * NEARLY_SORTED_FRAC);
                if (swaps < 1) swaps = 1;
                for (int s = 0; s < swaps; s++) {
                    int a = (int)(lcg_next() % (uint32_t)n);
                    int b = (int)(lcg_next() % (uint32_t)n);
                    int tmp = arr[a]; arr[a] = arr[b]; arr[b] = tmp;
                }
            }
            break;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   5  FILE I/O
   ══════════════════════════════════════════════════════════════════════════ */

static void save_bin(const int *arr, int n, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s for writing\n", path); exit(1); }
    fwrite(arr, sizeof(int), n, f);
    fclose(f);
}

static void load_bin(int *arr, int n, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s for reading\n", path); exit(1); }
    size_t rd = fread(arr, sizeof(int), n, f);
    fclose(f);
    if ((int)rd != n) { fprintf(stderr, "ERROR: short read from %s\n", path); exit(1); }
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static double file_size_mb(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0.0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return (double)sz / (1024.0 * 1024.0);
}

static void make_dir(const char *path) {
#ifdef _WIN32
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

/* ══════════════════════════════════════════════════════════════════════════
   6  SORTING ALGORITHMS
   ══════════════════════════════════════════════════════════════════════════ */

/* ── Quick Sort ──────────────────────────────────────────────────────────── */
static int _qs_partition(int *arr, int low, int high) {
    int mid = low + (high - low) / 2;
    if (arr[low] > arr[mid])  { int t=arr[low];  arr[low]=arr[mid];  arr[mid]=t; }
    if (arr[low] > arr[high]) { int t=arr[low];  arr[low]=arr[high]; arr[high]=t; }
    if (arr[mid] > arr[high]) { int t=arr[mid];  arr[mid]=arr[high]; arr[high]=t; }
    int pivot = arr[mid];
    int t = arr[mid]; arr[mid] = arr[high-1]; arr[high-1] = t;
    int i = low, j = high - 1;
    while (1) {
        while (arr[++i] < pivot);
        while (arr[--j] > pivot);
        if (i >= j) break;
        t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
    t = arr[i]; arr[i] = arr[high-1]; arr[high-1] = t;
    return i;
}

static void quick_sort(int *arr, int n) {
    if (n <= 1) return;
    int *stack = (int*)malloc(2 * n * sizeof(int));
    int top = -1;
    stack[++top] = 0;
    stack[++top] = n - 1;
    while (top >= 0) {
        int high = stack[top--];
        int low  = stack[top--];
        if (low < high) {
            int p = _qs_partition(arr, low, high);
            stack[++top] = low;   stack[++top] = p - 1;
            stack[++top] = p + 1; stack[++top] = high;
        }
    }
    free(stack);
}

/* ── Merge Sort ──────────────────────────────────────────────────────────── */
static int *_merge_buf = NULL;

static void _merge(int *arr, int l, int m, int r) {
    int i = l, j = m + 1, k = l;
    while (i <= m && j <= r) {
        if (arr[i] <= arr[j]) _merge_buf[k++] = arr[i++];
        else                   _merge_buf[k++] = arr[j++];
    }
    while (i <= m) _merge_buf[k++] = arr[i++];
    while (j <= r) _merge_buf[k++] = arr[j++];
    memcpy(arr + l, _merge_buf + l, (r - l + 1) * sizeof(int));
}

static void _merge_sort_rec(int *arr, int l, int r) {
    if (l >= r) return;
    int m = l + (r - l) / 2;
    _merge_sort_rec(arr, l, m);
    _merge_sort_rec(arr, m + 1, r);
    _merge(arr, l, m, r);
}

static void merge_sort(int *arr, int n) {
    if (n <= 1) return;
    _merge_buf = (int*)malloc(n * sizeof(int));
    _merge_sort_rec(arr, 0, n - 1);
    free(_merge_buf);
    _merge_buf = NULL;
}

/* ── Heap Sort ───────────────────────────────────────────────────────────── */
static void _heapify(int *arr, int n, int i) {
    while (1) {
        int largest = i, l = 2*i+1, r = 2*i+2;
        if (l < n && arr[l] > arr[largest]) largest = l;
        if (r < n && arr[r] > arr[largest]) largest = r;
        if (largest == i) break;
        int t = arr[i]; arr[i] = arr[largest]; arr[largest] = t;
        i = largest;
    }
}

static void heap_sort(int *arr, int n) {
    for (int i = n/2 - 1; i >= 0; i--) _heapify(arr, n, i);
    for (int i = n - 1;   i > 0;   i--) {
        int t = arr[0]; arr[0] = arr[i]; arr[i] = t;
        _heapify(arr, i, 0);
    }
}

/* ── Bubble Sort ─────────────────────────────────────────────────────────── */
static void bubble_sort(int *arr, int n) {
    for (int i = 0; i < n - 1; i++) {
        int swapped = 0;
        for (int j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j+1]) {
                int t = arr[j]; arr[j] = arr[j+1]; arr[j+1] = t;
                swapped = 1;
            }
        }
        if (!swapped) break;
    }
}

/* ── Selection Sort ──────────────────────────────────────────────────────── */
static void selection_sort(int *arr, int n) {
    for (int i = 0; i < n - 1; i++) {
        int min_idx = i;
        for (int j = i+1; j < n; j++)
            if (arr[j] < arr[min_idx]) min_idx = j;
        int t = arr[i]; arr[i] = arr[min_idx]; arr[min_idx] = t;
    }
}

/* ── Insertion Sort ──────────────────────────────────────────────────────── */
static void insertion_sort(int *arr, int n) {
    for (int i = 1; i < n; i++) {
        int key = arr[i], j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j+1] = arr[j]; j--; }
        arr[j+1] = key;
    }
}

/* ── Shaker Sort (Cocktail Sort) ─────────────────────────────────────────── */
static void shaker_sort(int *arr, int n) {
    int start = 0, end = n - 1, swapped = 1;
    while (swapped) {
        swapped = 0;
        for (int i = start; i < end; i++)
            if (arr[i] > arr[i+1]) {
                int t=arr[i]; arr[i]=arr[i+1]; arr[i+1]=t; swapped=1;
            }
        if (!swapped) break;
        end--;
        swapped = 0;
        for (int i = end - 1; i >= start; i--)
            if (arr[i] > arr[i+1]) {
                int t=arr[i]; arr[i]=arr[i+1]; arr[i+1]=t; swapped=1;
            }
        start++;
    }
}

/* ── Counting Sort ───────────────────────────────────────────────────────── */
static void counting_sort(int *arr, int n) {
    if (n == 0) return;
    int min_val = arr[0], max_val = arr[0];
    for (int i = 1; i < n; i++) {
        if (arr[i] < min_val) min_val = arr[i];
        if (arr[i] > max_val) max_val = arr[i];
    }
    int range = max_val - min_val + 1;
    int *count = (int*)calloc(range, sizeof(int));
    if (!count) return;
    for (int i = 0; i < n; i++) count[arr[i] - min_val]++;
    int idx = 0;
    for (int i = 0; i < range; i++)
        while (count[i]-- > 0) arr[idx++] = i + min_val;
    free(count);
}

/* ── Radix Sort (LSD, base 10) ───────────────────────────────────────────── */
static void _radix_pass(int *arr, int *output, int n, int exp) {
    int count[10] = {0};
    for (int i = 0; i < n; i++) count[(arr[i] / exp) % 10]++;
    for (int i = 1; i < 10; i++) count[i] += count[i-1];
    for (int i = n - 1; i >= 0; i--) {
        int d = (arr[i] / exp) % 10;
        output[--count[d]] = arr[i];
    }
    memcpy(arr, output, n * sizeof(int));
}

static void radix_sort(int *arr, int n) {
    if (n == 0) return;
    int max_val = arr[0];
    for (int i = 1; i < n; i++) if (arr[i] > max_val) max_val = arr[i];
    int *output = (int*)malloc(n * sizeof(int));
    for (int exp = 1; max_val / exp > 0; exp *= 10)
        _radix_pass(arr, output, n, exp);
    free(output);
}

/* ── Timsort ─────────────────────────────────────────────────────────────── */
#define TIM_RUN 32

static void _tim_insertion(int *arr, int l, int r) {
    for (int i = l + 1; i <= r; i++) {
        int key = arr[i], j = i - 1;
        while (j >= l && arr[j] > key) { arr[j+1] = arr[j]; j--; }
        arr[j+1] = key;
    }
}

static void _tim_merge(int *arr, int l, int m, int r) {
    int len1 = m - l + 1, len2 = r - m;
    int *left  = (int*)malloc(len1 * sizeof(int));
    int *right = (int*)malloc(len2 * sizeof(int));
    memcpy(left,  arr + l,     len1 * sizeof(int));
    memcpy(right, arr + m + 1, len2 * sizeof(int));
    int i = 0, j = 0, k = l;
    while (i < len1 && j < len2)
        arr[k++] = (left[i] <= right[j]) ? left[i++] : right[j++];
    while (i < len1) arr[k++] = left[i++];
    while (j < len2) arr[k++] = right[j++];
    free(left); free(right);
}

static void timsort(int *arr, int n) {
    for (int i = 0; i < n; i += TIM_RUN)
        _tim_insertion(arr, i,
            (i + TIM_RUN - 1 < n - 1) ? i + TIM_RUN - 1 : n - 1);
    for (int size = TIM_RUN; size < n; size *= 2) {
        for (int l = 0; l < n; l += 2 * size) {
            int m = l + size - 1;
            int r = (l + 2*size - 1 < n - 1) ? l + 2*size - 1 : n - 1;
            if (m < r) _tim_merge(arr, l, m, r);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   7  BENCHMARK ENGINE
   ══════════════════════════════════════════════════════════════════════════ */

typedef void (*SortFn)(int*, int);

static const SortFn ALGO_FNS[NUM_ALGORITHMS] = {
    quick_sort, merge_sort,     heap_sort,
    bubble_sort,selection_sort, insertion_sort,
    shaker_sort,counting_sort,  radix_sort,
    timsort
};

typedef struct {
    double time_s;
    double mem_before_mb;
    double mem_after_mb;
    double mem_delta_mb;
    int    skipped;
    int    error;
} BenchResult;

static BenchResult run_one(SortFn fn, const int *src, int n) {
    BenchResult r = {0};
    int *arr = (int*)malloc(n * sizeof(int));
    if (!arr) { r.error = 1; return r; }
    memcpy(arr, src, n * sizeof(int));

    r.mem_before_mb = get_memory_mb();
    double t_start  = get_time_sec();
    fn(arr, n);
    r.time_s        = get_time_sec() - t_start;
    r.mem_after_mb  = get_memory_mb();
    r.mem_delta_mb  = r.mem_after_mb - r.mem_before_mb;
    if (r.mem_delta_mb < 0) r.mem_delta_mb = 0.0;

    free(arr);
    return r;
}

/* ══════════════════════════════════════════════════════════════════════════
   8  PRETTY PRINTING
   ══════════════════════════════════════════════════════════════════════════ */

static void print_line(char c, int w) {
    for (int i = 0; i < w; i++) putchar(c);
    putchar('\n');
}

/* ══════════════════════════════════════════════════════════════════════════
   9  MAIN
   ══════════════════════════════════════════════════════════════════════════ */

int main(void) {

    /* results[dist][algo][size][cond] */
    BenchResult results[NUM_DISTRIBUTIONS][NUM_ALGORITHMS][NUM_SIZES][NUM_CONDITIONS];
    memset(results, 0, sizeof(results));

    double wall_start = get_time_sec();

    /* ── PHASE 1: DATA GENERATION ──────────────────────────────────────── */
    print_line('=', 78);
    printf("  PHASE 1 -- DATA GENERATION\n");
    print_line('=', 78);

    make_dir(DATA_DIR);

    /* data_paths[dist][size][cond] */
    char data_paths[NUM_DISTRIBUTIONS][NUM_SIZES][NUM_CONDITIONS][256];

    int max_n    = SIZES[NUM_SIZES - 1];
    int *gen_buf = (int*)malloc(max_n * sizeof(int));
    if (!gen_buf) { fprintf(stderr, "Out of memory\n"); return 1; }

    for (int di = 0; di < NUM_DISTRIBUTIONS; di++) {
        printf("\n  Distribution: %s\n", DIST_NAMES[di]);

        for (int si = 0; si < NUM_SIZES; si++) {
            int n = SIZES[si];
            printf("    Size: %s (%d)\n", SIZE_LABELS[si], n);

            for (int ci = 0; ci < NUM_CONDITIONS; ci++) {
                snprintf(data_paths[di][si][ci],
                         sizeof(data_paths[di][si][ci]),
                         DATA_DIR PATH_SEP "data_%s_%s_%s.bin",
                         DIST_NAMES[di], SIZE_LABELS[si], CONDITIONS[ci]);

                const char *path = data_paths[di][si][ci];

                if (file_exists(path)) {
                    printf("      [SKIP] %s_%s_%s.bin (%.1f MB)\n",
                           DIST_NAMES[di], SIZE_LABELS[si], CONDITIONS[ci],
                           file_size_mb(path));
                    continue;
                }

                printf("      [GEN ] %s_%s_%s.bin ... ",
                       DIST_NAMES[di], SIZE_LABELS[si], CONDITIONS[ci]);
                fflush(stdout);

                double t0 = get_time_sec();
                generate(gen_buf, n, di, ci);
                save_bin(gen_buf, n, path);
                printf("done  %.2fs  %.1f MB\n",
                       get_time_sec() - t0, file_size_mb(path));
            }
        }
    }

    printf("\n  Data ready. (%d files in ./%s/)\n\n",
           NUM_DISTRIBUTIONS * NUM_SIZES * NUM_CONDITIONS, DATA_DIR);
    free(gen_buf);

    /* ── PHASE 2: BENCHMARKING ─────────────────────────────────────────── */
    print_line('=', 78);
    printf("  PHASE 2 -- BENCHMARKING\n");
    print_line('=', 78);

    int *data_buf = (int*)malloc(max_n * sizeof(int));
    if (!data_buf) { fprintf(stderr, "Out of memory\n"); return 1; }

    for (int di = 0; di < NUM_DISTRIBUTIONS; di++) {
        printf("\n");
        print_line('*', 78);
        printf("  DISTRIBUTION: %s\n", DIST_NAMES[di]);
        print_line('*', 78);

        for (int si = 0; si < NUM_SIZES; si++) {
            int n = SIZES[si];

            for (int ci = 0; ci < NUM_CONDITIONS; ci++) {
                printf("\n");
                print_line('-', 78);
                printf("  > Dist: %-12s  Size: %-6s (%d)  Cond: %s\n",
                       DIST_NAMES[di], SIZE_LABELS[si], n, CONDITIONS[ci]);
                print_line('-', 78);
                printf("  %-18s %12s  %12s  %s\n",
                       "Algorithm", "Time (s)", "Memory (MB)", "Status");
                printf("  %-18s %12s  %12s  %s\n",
                       "------------------","------------",
                       "------------","----------");

                load_bin(data_buf, n, data_paths[di][si][ci]);

                for (int ai = 0; ai < NUM_ALGORITHMS; ai++) {

                    /* Skip O(n^2) on large inputs */
                    if (SKIP_SLOW_FOR_LARGE && IS_SLOW[ai] &&
                        n > SLOW_ALGO_MAX_SIZE) {
                        results[di][ai][si][ci].skipped = 1;
                        printf("  %-18s %12s  %12s  SKIPPED (O(n2))\n",
                               ALGO_NAMES[ai], "-", "-");
                        continue;
                    }

                    /* Skip Counting Sort on wide_range (range = n^3 -> OOM) */
                    if (ai == COUNTING_SORT_IDX && di == DIST_WIDE_RANGE) {
                        results[di][ai][si][ci].skipped = 1;
                        printf("  %-18s %12s  %12s  SKIPPED (range=n^3)\n",
                               ALGO_NAMES[ai], "-", "-");
                        continue;
                    }

                    BenchResult r = run_one(ALGO_FNS[ai], data_buf, n);
                    results[di][ai][si][ci] = r;

                    if (r.error)
                        printf("  %-18s %12s  %12s  ERROR\n",
                               ALGO_NAMES[ai], "-", "-");
                    else
                        printf("  %-18s %12.6f  %12.2f  OK\n",
                               ALGO_NAMES[ai], r.time_s, r.mem_after_mb);

                    fflush(stdout);
                }
            }
        }
    }

    free(data_buf);

    /* ── PHASE 3: SUMMARY TABLES ───────────────────────────────────────── */
    for (int di = 0; di < NUM_DISTRIBUTIONS; di++) {
        for (int ci = 0; ci < NUM_CONDITIONS; ci++) {
            printf("\n");
            print_line('=', 78);
            printf("  RESULTS  |  Distribution: %-14s  Condition: %s\n",
                   DIST_NAMES[di], CONDITIONS[ci]);
            print_line('=', 78);

            /* Time table */
            printf("\n  Time (seconds)\n");
            printf("  %-18s", "Algorithm");
            for (int si = 0; si < NUM_SIZES; si++)
                printf("  %10s", SIZE_LABELS[si]);
            printf("\n  %-18s", "------------------");
            for (int si = 0; si < NUM_SIZES; si++)
                printf("  %10s", "----------");
            printf("\n");
            for (int ai = 0; ai < NUM_ALGORITHMS; ai++) {
                printf("  %-18s", ALGO_NAMES[ai]);
                for (int si = 0; si < NUM_SIZES; si++) {
                    BenchResult *r = &results[di][ai][si][ci];
                    if (r->skipped || r->error) printf("  %10s", "-");
                    else                        printf("  %10.6f", r->time_s);
                }
                printf("\n");
            }

            /* Memory table */
            printf("\n  Memory -- RSS after sort (MB)\n");
            printf("  %-18s", "Algorithm");
            for (int si = 0; si < NUM_SIZES; si++)
                printf("  %10s", SIZE_LABELS[si]);
            printf("\n  %-18s", "------------------");
            for (int si = 0; si < NUM_SIZES; si++)
                printf("  %10s", "----------");
            printf("\n");
            for (int ai = 0; ai < NUM_ALGORITHMS; ai++) {
                printf("  %-18s", ALGO_NAMES[ai]);
                for (int si = 0; si < NUM_SIZES; si++) {
                    BenchResult *r = &results[di][ai][si][ci];
                    if (r->skipped || r->error) printf("  %10s", "-");
                    else                        printf("  %10.2f", r->mem_after_mb);
                }
                printf("\n");
            }
        }
    }

    /* ── PHASE 4: CSV EXPORT ───────────────────────────────────────────── */
    FILE *csv = fopen(RESULTS_CSV, "w");
    if (csv) {
        fprintf(csv, "distribution,algorithm,size_label,size,condition,"
                     "time_s,memory_mb,status\n");

        for (int di = 0; di < NUM_DISTRIBUTIONS; di++) {
            for (int ai = 0; ai < NUM_ALGORITHMS; ai++) {
                for (int si = 0; si < NUM_SIZES; si++) {
                    for (int ci = 0; ci < NUM_CONDITIONS; ci++) {
                        BenchResult *r = &results[di][ai][si][ci];
                        const char *status =
                            r->skipped ? "SKIPPED" :
                            (r->error  ? "ERROR"   : "OK");

                        if (r->skipped || r->error) {
                            fprintf(csv, "%s,%s,%s,%d,%s,,,\"%s\"\n",
                                    DIST_NAMES[di], ALGO_NAMES[ai],
                                    SIZE_LABELS[si], SIZES[si],
                                    CONDITIONS[ci], status);
                        } else {
                            fprintf(csv,
                                    "%s,%s,%s,%d,%s,%.6f,%.4f,\"%s\"\n",
                                    DIST_NAMES[di], ALGO_NAMES[ai],
                                    SIZE_LABELS[si], SIZES[si],
                                    CONDITIONS[ci],
                                    r->time_s, r->mem_after_mb, status);
                        }
                    }
                }
            }
        }
        fclose(csv);
        printf("\n  Results saved to %s\n", RESULTS_CSV);
    }

    double wall_total = get_time_sec() - wall_start;
    printf("\n");
    print_line('=', 78);
    printf("  Total wall-clock time: %.1f s (%.1f min)\n",
           wall_total, wall_total / 60.0);
    print_line('=', 78);

    printf("\n"
"  LEGEND\n"
"  -----------------------------------------------------------------------\n"
"  -              Skipped or error\n"
"  Time (s)       Wall-clock via clock_gettime(CLOCK_MONOTONIC)\n"
"  Memory (MB)    Resident Set Size from /proc/self/status (Linux)\n"
"\n"
"  DISTRIBUTIONS\n"
"  -----------------------------------------------------------------------\n"
"  sequential     Values [0, n), no duplicates (permutation of 0..n-1)\n"
"  three_vals     Only values {0, 1, 2}, randomly distributed\n"
"  wide_range     Values [0, n^3), random integers, duplicates allowed\n"
"\n"
"  CONDITIONS\n"
"  -----------------------------------------------------------------------\n"
"  random         Randomly shuffled\n"
"  sorted         Ascending order\n"
"  reverse        Descending order\n"
"  nearly_sorted  Sorted with ~5%% of elements randomly swapped\n"
"\n"
"  COMPLEXITY REFERENCE\n"
"  -----------------------------------------------------------------------\n"
"  Algorithm       Best        Average     Worst       Space\n"
"  Quick Sort      O(n log n)  O(n log n)  O(n^2)      O(log n)\n"
"  Merge Sort      O(n log n)  O(n log n)  O(n log n)  O(n)\n"
"  Heap Sort       O(n log n)  O(n log n)  O(n log n)  O(1)\n"
"  Bubble Sort     O(n)        O(n^2)      O(n^2)      O(1)\n"
"  Selection Sort  O(n^2)      O(n^2)      O(n^2)      O(1)\n"
"  Insertion Sort  O(n)        O(n^2)      O(n^2)      O(1)\n"
"  Shaker Sort     O(n)        O(n^2)      O(n^2)      O(1)\n"
"  Counting Sort   O(n+k)      O(n+k)      O(n+k)      O(k)\n"
"  Radix Sort      O(nk)       O(nk)       O(nk)       O(n+k)\n"
"  Timsort         O(n)        O(n log n)  O(n log n)  O(n)\n"
"  -----------------------------------------------------------------------\n\n");

    return 0;
}