/*
╔══════════════════════════════════════════════════════════════════════════════╗
║        SORTING ALGORITHM BENCHMARK v2 — C Implementation                   ║
║                                                                              ║
║  Algorithms (12 variants):                                                  ║
║    Quick Sort     — iterative (median-3 pivot)                              ║
║    Quick Sort     — recursive (median-3 pivot)                              ║
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
║    time_s        wall-clock seconds                                         ║
║    memory_mb     RSS after sort (MB)                                        ║
║    comparisons   element comparisons inside sort                            ║
║    swaps         element writes / swaps inside sort                         ║
║    stable        verified stable (1) or not (0)                             ║
║    variant       iterative | recursive                                      ║
║    complexity_*  theoretical best/avg/worst/space                           ║
║                                                                              ║
║  Small sizes (≤ 1K): each run repeated SMALL_RUNS times,                   ║
║    reporting min_time, avg_time, max_time                                   ║
║                                                                              ║
║  Compile : gcc -O2 -o benchmark sorting_benchmark_v2.c                     ║
║  Run     : ./benchmark                                                      ║
║  Output  : terminal + results.csv                                           ║
╚══════════════════════════════════════════════════════════════════════════════╝
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include <sys/stat.h>

#ifdef _WIN32
  #include <windows.h>
  #include <psapi.h>
  #include <direct.h>
  #define PATH_SEP "\\"
  #define make_dir(p) _mkdir(p)
#else
  #include <sys/time.h>
  #define PATH_SEP "/"
  #define make_dir(p) mkdir(p, 0755)
#endif

/* ══════════════════════════════════════════════════════════════════════════
   1  CONFIGURATION
   ══════════════════════════════════════════════════════════════════════════ */

#define NUM_SIZES          10
#define NUM_CONDITIONS      5
#define NUM_DISTRIBUTIONS   3
#define NUM_ALGORITHMS     12   /* 10 original + 2 extra variants */

/* Sizes — added 10k and 10m vs previous version */
static const int   SIZES[]       = {10, 20, 50, 100, 1000,
                                     10000, 100000, 1000000,
                                     10000000, 100000000};
static const char *SIZE_LABELS[] = {"10","20","50","100","1k",
                                     "10k","100k","1m","10m","100m"};

/* Threshold below which we do SMALL_RUNS repeated iterations */
#define SMALL_SIZE_THRESHOLD   1000
#define SMALL_RUNS             50

/* Conditions */
#define COND_RANDOM          0
#define COND_SORTED          1
#define COND_REVERSE         2
#define COND_NEARLY_SORTED   3
#define COND_ALL_IDENTICAL   4   /* NEW — every element == 0 */
static const char *CONDITIONS[] = {
    "random","sorted","reverse","nearly_sorted","all_identical"
};

/* Distributions */
#define DIST_SEQUENTIAL  0
#define DIST_THREE_VALS  1
#define DIST_WIDE_RANGE  2
static const char *DIST_NAMES[] = {
    "sequential","three_vals","wide_range"
};

#define DATA_DIR              "benchmark_data"
#define RESULTS_CSV           "results.csv"
#define SLOW_ALGO_MAX_SIZE    100000
#define SKIP_SLOW_FOR_LARGE   1
#define NEARLY_SORTED_FRAC    0.05

/* Algorithm indices */
#define IDX_QUICK_ITER    0
#define IDX_QUICK_REC     1
#define IDX_MERGE_REC     2
#define IDX_MERGE_ITER    3
#define IDX_HEAP          4
#define IDX_BUBBLE        5
#define IDX_SELECTION     6
#define IDX_INSERTION     7
#define IDX_SHAKER        8
#define IDX_COUNTING      9
#define IDX_RADIX        10
#define IDX_TIMSORT      11

static const char *ALGO_NAMES[NUM_ALGORITHMS] = {
    "Quick Sort (iter)",  "Quick Sort (rec)",
    "Merge Sort (rec)",   "Merge Sort (iter)",
    "Heap Sort",
    "Bubble Sort",        "Selection Sort",    "Insertion Sort",
    "Shaker Sort",        "Counting Sort",
    "Radix Sort",         "Timsort"
};

/* Which algorithms are O(n^2) and skipped on large inputs */
static const int IS_SLOW[NUM_ALGORITHMS] = {
    0,0, 0,0, 0,   /* Quick×2, Merge×2, Heap   */
    1,1,1,1,       /* Bubble,Sel,Ins,Shaker     */
    0,0,0          /* Counting,Radix,Tim        */
};

/* Theoretical complexity strings [best, avg, worst, space] */
static const char *COMPLEXITY[NUM_ALGORITHMS][4] = {
    {"O(n log n)","O(n log n)","O(n^2)",    "O(log n)"},  /* Quick iter */
    {"O(n log n)","O(n log n)","O(n^2)",    "O(log n)"},  /* Quick rec  */
    {"O(n log n)","O(n log n)","O(n log n)","O(n)"},      /* Merge rec  */
    {"O(n log n)","O(n log n)","O(n log n)","O(n)"},      /* Merge iter */
    {"O(n log n)","O(n log n)","O(n log n)","O(1)"},      /* Heap       */
    {"O(n)",      "O(n^2)",    "O(n^2)",    "O(1)"},      /* Bubble     */
    {"O(n^2)",    "O(n^2)",    "O(n^2)",    "O(1)"},      /* Selection  */
    {"O(n)",      "O(n^2)",    "O(n^2)",    "O(1)"},      /* Insertion  */
    {"O(n)",      "O(n^2)",    "O(n^2)",    "O(1)"},      /* Shaker     */
    {"O(n+k)",    "O(n+k)",    "O(n+k)",    "O(k)"},      /* Counting   */
    {"O(nk)",     "O(nk)",     "O(nk)",     "O(n+k)"},    /* Radix      */
    {"O(n)",      "O(n log n)","O(n log n)","O(n)"},      /* Timsort    */
};

/* Known stability: 1=stable, 0=unstable */
static const int KNOWN_STABLE[NUM_ALGORITHMS] = {
    0,0, 1,1, 0, 1,0,1,1,1,1,1
};

/* Variant label */
static const char *VARIANT[NUM_ALGORITHMS] = {
    "iterative","recursive",
    "recursive","iterative",
    "iterative",
    "iterative","iterative","iterative","iterative",
    "iterative","iterative","hybrid"
};

/* ══════════════════════════════════════════════════════════════════════════
   2  GLOBAL COUNTERS — written to by sort functions
   ══════════════════════════════════════════════════════════════════════════ */

static uint64_t g_comparisons = 0;
static uint64_t g_swaps       = 0;

#define CMP(a,b)   (g_comparisons++, ((a)>(b)))
#define SWAP(a,b)  do { g_swaps++; int _t=(a);(a)=(b);(b)=_t; } while(0)

/* ══════════════════════════════════════════════════════════════════════════
   3  TIMING & MEMORY
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
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (double)pmc.WorkingSetSize / (1024.0*1024.0);
    return -1.0;
#else
    FILE *f = fopen("/proc/self/status","r");
    if (!f) return -1.0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line,"VmRSS:",6)==0) {
            fclose(f);
            long kb=0; sscanf(line+6,"%ld",&kb);
            return (double)kb/1024.0;
        }
    }
    fclose(f); return -1.0;
#endif
}

/* ══════════════════════════════════════════════════════════════════════════
   4  RNG
   ══════════════════════════════════════════════════════════════════════════ */

static uint32_t lcg_state = 12345678u;
static uint32_t lcg_next(void) {
    lcg_state = lcg_state*1664525u + 1013904223u;
    return lcg_state;
}
static void lcg_reset(void) { lcg_state = 12345678u; }

static void shuffle(int *arr, int n) {
    for (int i=n-1; i>0; i--) {
        int j=(int)(lcg_next()%(uint32_t)(i+1));
        int t=arr[i]; arr[i]=arr[j]; arr[j]=t;
    }
}

static int cmp_asc(const void *a, const void *b) {
    int x=*(const int*)a, y=*(const int*)b;
    return (x>y)-(x<y);
}

/* ══════════════════════════════════════════════════════════════════════════
   5  DATA GENERATION
   ══════════════════════════════════════════════════════════════════════════ */

static void generate(int *arr, int n, int dist, int cond) {
    lcg_reset();

    if (cond == COND_ALL_IDENTICAL) {
        /* All identical — dist doesn't matter, all zeros */
        memset(arr, 0, n * sizeof(int));
        return;
    }

    /* Step 1: fill base values */
    if (dist == DIST_SEQUENTIAL) {
        for (int i=0; i<n; i++) arr[i]=i;
    } else if (dist == DIST_THREE_VALS) {
        for (int i=0; i<n; i++) arr[i]=(int)(lcg_next()%3u);
    } else {
        uint64_t n64=(uint64_t)n;
        uint64_t range = (n64*n64*n64 > 2000000000ULL)
                       ? 2000000000ULL : n64*n64*n64;
        for (int i=0; i<n; i++)
            arr[i]=(int)(lcg_next()%(uint32_t)range);
    }

    /* Step 2: apply ordering */
    switch (cond) {
        case COND_RANDOM:        shuffle(arr, n); break;
        case COND_SORTED:        qsort(arr, n, sizeof(int), cmp_asc); break;
        case COND_REVERSE:
            qsort(arr, n, sizeof(int), cmp_asc);
            for (int i=0,j=n-1; i<j; i++,j--) { int t=arr[i];arr[i]=arr[j];arr[j]=t; }
            break;
        case COND_NEARLY_SORTED:
            qsort(arr, n, sizeof(int), cmp_asc);
            { int sw=(int)(n*NEARLY_SORTED_FRAC); if(sw<1)sw=1;
              for (int s=0;s<sw;s++) {
                int a=(int)(lcg_next()%(uint32_t)n);
                int b=(int)(lcg_next()%(uint32_t)n);
                int t=arr[a];arr[a]=arr[b];arr[b]=t;
              }
            }
            break;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   6  FILE I/O
   ══════════════════════════════════════════════════════════════════════════ */

static void save_bin(const int *arr, int n, const char *path) {
    FILE *f=fopen(path,"wb");
    if (!f){fprintf(stderr,"ERROR: cannot open %s\n",path);exit(1);}
    fwrite(arr,sizeof(int),n,f); fclose(f);
}
static void load_bin(int *arr, int n, const char *path) {
    FILE *f=fopen(path,"rb");
    if (!f){fprintf(stderr,"ERROR: cannot open %s\n",path);exit(1);}
    size_t r=fread(arr,sizeof(int),n,f); fclose(f);
    if ((int)r!=n){fprintf(stderr,"ERROR: short read %s\n",path);exit(1);}
}
static int file_exists(const char *p){FILE*f=fopen(p,"rb");if(f){fclose(f);return 1;}return 0;}
static double file_mb(const char *p){FILE*f=fopen(p,"rb");if(!f)return 0;fseek(f,0,SEEK_END);long s=ftell(f);fclose(f);return s/1048576.0;}

/* ══════════════════════════════════════════════════════════════════════════
   7  SORTING ALGORITHMS
      All use CMP() and SWAP() macros to count operations.
   ══════════════════════════════════════════════════════════════════════════ */

/* ── Quick Sort — Lomuto partition (safe, counts ops) ───────────────────── */
/*
 * Median-of-three pivot selection, then Lomuto partition.
 * Safe for all subarray sizes >= 1.
 */
static int _qs_part(int *arr, int lo, int hi) {
    /* Pick median-of-three pivot and place it at hi */
    int mid = lo + (hi - lo) / 2;
    /* Sort lo, mid, hi so median ends at hi */
    if (CMP(arr[mid], arr[lo]))  SWAP(arr[mid], arr[lo]);
    if (CMP(arr[hi],  arr[lo]))  SWAP(arr[hi],  arr[lo]);
    if (CMP(arr[mid], arr[hi]))  SWAP(arr[mid], arr[hi]);
    /* arr[hi] is now the median — use as pivot */
    int pivot = arr[hi];
    int i = lo - 1;
    for (int j = lo; j < hi; j++) {
        g_comparisons++;
        if (arr[j] <= pivot) {
            i++;
            SWAP(arr[i], arr[j]);
        }
    }
    SWAP(arr[i+1], arr[hi]);
    return i + 1;
}

/* Iterative Quick Sort */
static void quick_sort_iter(int *arr, int n) {
    if (n <= 1) return;
    int *stk = (int*)malloc(2 * n * sizeof(int));
    int top = -1;
    stk[++top] = 0;
    stk[++top] = n - 1;
    while (top >= 0) {
        int hi = stk[top--];
        int lo = stk[top--];
        if (lo < hi) {
            int p = _qs_part(arr, lo, hi);
            stk[++top] = lo;   stk[++top] = p - 1;
            stk[++top] = p + 1; stk[++top] = hi;
        }
    }
    free(stk);
}

/* Recursive Quick Sort */
static void _quick_rec(int *arr, int lo, int hi) {
    if (lo >= hi) return;
    int p = _qs_part(arr, lo, hi);
    _quick_rec(arr, lo, p - 1);
    _quick_rec(arr, p + 1, hi);
}
static void quick_sort_rec(int *arr, int n) {
    if (n <= 1) return;
    _quick_rec(arr, 0, n - 1);
}

/* ── Merge Sort helpers ───────────────────────────────────────────────────── */
static int *_mbuf = NULL;

static void _merge(int *arr, int l, int m, int r) {
    int i=l, j=m+1, k=l;
    while (i<=m && j<=r) {
        g_comparisons++;
        if (arr[i]<=arr[j]) { _mbuf[k++]=arr[i++]; }
        else                { _mbuf[k++]=arr[j++]; g_swaps++; }
    }
    while (i<=m) { _mbuf[k++]=arr[i++]; }
    while (j<=r) { _mbuf[k++]=arr[j++]; g_swaps++; }
    int len=r-l+1;
    memcpy(arr+l, _mbuf+l, len*sizeof(int));
    g_swaps += len;
}

/* Recursive Merge Sort */
static void _merge_rec(int *arr, int l, int r) {
    if (l>=r) return;
    int m=l+(r-l)/2;
    _merge_rec(arr,l,m);
    _merge_rec(arr,m+1,r);
    _merge(arr,l,m,r);
}
static void merge_sort_rec(int *arr, int n) {
    if (n<=1) return;
    _mbuf=(int*)malloc(n*sizeof(int));
    _merge_rec(arr,0,n-1);
    free(_mbuf); _mbuf=NULL;
}

/* Iterative (bottom-up) Merge Sort */
static void merge_sort_iter(int *arr, int n) {
    if (n<=1) return;
    _mbuf=(int*)malloc(n*sizeof(int));
    for (int w=1; w<n; w*=2) {
        for (int l=0; l<n; l+=2*w) {
            int m=l+w-1;
            int r=(l+2*w-1<n-1)?(l+2*w-1):(n-1);
            if (m<r) _merge(arr,l,m,r);
        }
    }
    free(_mbuf); _mbuf=NULL;
}

/* ── Heap Sort ────────────────────────────────────────────────────────────── */
static void _heapify(int *arr, int n, int i) {
    for(;;){
        int lg=i, l=2*i+1, r=2*i+2;
        if (l<n && CMP(arr[l],arr[lg])) lg=l;
        if (r<n && CMP(arr[r],arr[lg])) lg=r;
        if (lg==i) break;
        SWAP(arr[i],arr[lg]); i=lg;
    }
}
static void heap_sort(int *arr, int n) {
    for (int i=n/2-1; i>=0; i--) _heapify(arr,n,i);
    for (int i=n-1;   i>0;   i--) { SWAP(arr[0],arr[i]); _heapify(arr,i,0); }
}

/* ── Bubble Sort ──────────────────────────────────────────────────────────── */
static void bubble_sort(int *arr, int n) {
    for (int i=0; i<n-1; i++) {
        int sw=0;
        for (int j=0; j<n-i-1; j++)
            if (CMP(arr[j],arr[j+1])) { SWAP(arr[j],arr[j+1]); sw=1; }
        if (!sw) break;
    }
}

/* ── Selection Sort ───────────────────────────────────────────────────────── */
static void selection_sort(int *arr, int n) {
    for (int i=0; i<n-1; i++) {
        int mi=i;
        for (int j=i+1; j<n; j++) if (CMP(arr[j],arr[mi])) mi=j;
        if (mi!=i) SWAP(arr[i],arr[mi]);
    }
}

/* ── Insertion Sort ───────────────────────────────────────────────────────── */
static void insertion_sort(int *arr, int n) {
    for (int i=1; i<n; i++) {
        int key=arr[i], j=i-1;
        while (j>=0 && (g_comparisons++, arr[j]>key)) {
            arr[j+1]=arr[j]; g_swaps++; j--;
        }
        arr[j+1]=key;
    }
}

/* ── Shaker Sort ──────────────────────────────────────────────────────────── */
static void shaker_sort(int *arr, int n) {
    int start=0, end=n-1, sw=1;
    while (sw) {
        sw=0;
        for (int i=start; i<end;   i++) if (CMP(arr[i],arr[i+1]))   { SWAP(arr[i],arr[i+1]); sw=1; }
        end--;
        for (int i=end-1; i>=start; i--) if (CMP(arr[i],arr[i+1])) { SWAP(arr[i],arr[i+1]); sw=1; }
        start++;
    }
}

/* ── Counting Sort ────────────────────────────────────────────────────────── */
static void counting_sort(int *arr, int n) {
    if (!n) return;
    int mn=arr[0], mx=arr[0];
    for (int i=1; i<n; i++) {
        g_comparisons++;
        if (arr[i]<mn) mn=arr[i];
        if (arr[i]>mx) mx=arr[i];
    }
    int range=mx-mn+1;
    int *cnt=(int*)calloc(range,sizeof(int));
    if (!cnt) return;
    for (int i=0; i<n; i++) cnt[arr[i]-mn]++;
    int idx=0;
    for (int i=0; i<range; i++)
        while (cnt[i]-->0) { arr[idx++]=i+mn; g_swaps++; }
    free(cnt);
}

/* ── Radix Sort ───────────────────────────────────────────────────────────── */
static void _radix_pass(int *arr, int *out, int n, int exp) {
    int cnt[10]={0};
    for (int i=0; i<n; i++) cnt[(arr[i]/exp)%10]++;
    for (int i=1; i<10; i++) cnt[i]+=cnt[i-1];
    for (int i=n-1; i>=0; i--) {
        int d=(arr[i]/exp)%10;
        out[--cnt[d]]=arr[i]; g_swaps++;
    }
    memcpy(arr,out,n*sizeof(int));
}
static void radix_sort(int *arr, int n) {
    if (!n) return;
    int mx=arr[0];
    for (int i=1; i<n; i++) if (arr[i]>mx) mx=arr[i];
    int *out=(int*)malloc(n*sizeof(int));
    for (int exp=1; mx/exp>0; exp*=10) _radix_pass(arr,out,n,exp);
    free(out);
}

/* ── Timsort ──────────────────────────────────────────────────────────────── */
#define TIM_RUN 32

static void _tim_ins(int *arr, int l, int r) {
    for (int i=l+1; i<=r; i++) {
        int key=arr[i], j=i-1;
        while (j>=l && (g_comparisons++, arr[j]>key)) {
            arr[j+1]=arr[j]; g_swaps++; j--;
        }
        arr[j+1]=key;
    }
}
static void _tim_merge(int *arr, int l, int m, int r) {
    int l1=m-l+1, l2=r-m;
    int *L=(int*)malloc(l1*sizeof(int));
    int *R=(int*)malloc(l2*sizeof(int));
    memcpy(L,arr+l,   l1*sizeof(int));
    memcpy(R,arr+m+1, l2*sizeof(int));
    int i=0,j=0,k=l;
    while (i<l1 && j<l2) {
        g_comparisons++;
        arr[k++] = (L[i]<=R[j]) ? (g_swaps++, L[i++]) : (g_swaps++, R[j++]);
    }
    while (i<l1) { arr[k++]=L[i++]; g_swaps++; }
    while (j<l2) { arr[k++]=R[j++]; g_swaps++; }
    free(L); free(R);
}
static void timsort(int *arr, int n) {
    for (int i=0; i<n; i+=TIM_RUN)
        _tim_ins(arr, i, (i+TIM_RUN-1<n-1)?(i+TIM_RUN-1):(n-1));
    for (int sz=TIM_RUN; sz<n; sz*=2)
        for (int l=0; l<n; l+=2*sz) {
            int m=l+sz-1, r=(l+2*sz-1<n-1)?(l+2*sz-1):(n-1);
            if (m<r) _tim_merge(arr,l,m,r);
        }
}

/* ── Function table ───────────────────────────────────────────────────────── */
typedef void (*SortFn)(int*,int);
static const SortFn ALGO_FNS[NUM_ALGORITHMS] = {
    quick_sort_iter, quick_sort_rec,
    merge_sort_rec,  merge_sort_iter,
    heap_sort,
    bubble_sort, selection_sort, insertion_sort, shaker_sort,
    counting_sort, radix_sort, timsort
};

/* ══════════════════════════════════════════════════════════════════════════
   8  STABILITY VERIFICATION
      Sort (value, original_index) pairs. After sorting, equal values
      should appear in ascending index order if the algorithm is stable.
   ══════════════════════════════════════════════════════════════════════════ */

typedef struct { int val; int idx; } Pair;

static int cmp_pair(const void *a, const void *b) {
    const Pair *pa=a, *pb=b;
    return (pa->val > pb->val) - (pa->val < pb->val);
}

/* Minimal insertion sort on pairs — used for stability check */
static void _ins_pair(Pair *p, int n) {
    for (int i=1; i<n; i++) {
        Pair k=p[i]; int j=i-1;
        while (j>=0 && (p[j].val>k.val||(p[j].val==k.val&&p[j].idx>k.idx)))
            { p[j+1]=p[j]; j--; }
        p[j+1]=k;
    }
}

/*
 * Verify stability of a sort function on data[0..n-1].
 * Returns 1 if stable, 0 if not.
 * We only run this for n <= STABILITY_MAX to keep it fast.
 */
#define STABILITY_MAX 50000

static int verify_stable(SortFn fn, const int *data, int n) {
    if (n > STABILITY_MAX) return -1; /* too large — skip */

    /* Build reference stable sort using pairs */
    Pair *ref = (Pair*)malloc(n * sizeof(Pair));
    Pair *test= (Pair*)malloc(n * sizeof(Pair));
    if (!ref || !test) { free(ref); free(test); return -1; }

    for (int i=0; i<n; i++) { ref[i].val=data[i]; ref[i].idx=i; }
    memcpy(test, ref, n*sizeof(Pair));

    /* Reference: stable sort via insertion sort on pairs */
    _ins_pair(ref, n);

    /* Test: sort a plain int copy, then check index order for equal values */
    int *tmp = (int*)malloc(n*sizeof(int));
    memcpy(tmp, data, n*sizeof(int));
    uint64_t save_cmp=g_comparisons, save_sw=g_swaps;
    fn(tmp, n);
    g_comparisons=save_cmp; g_swaps=save_sw; /* don't count stability check */

    /* Build what a stable sort would produce */
    int stable = 1;
    /* Walk ref pairs — check consecutive equal values keep ascending idx */
    for (int i=1; i<n && stable; i++) {
        if (ref[i].val == ref[i-1].val && ref[i].idx < ref[i-1].idx)
            stable = 0;
    }
    /* Check if algorithm produced same sorted values (correctness) */
    for (int i=0; i<n && stable; i++) {
        if (tmp[i] != ref[i].val) { stable = 0; }
    }

    free(ref); free(test); free(tmp);
    return stable;
}

/* ══════════════════════════════════════════════════════════════════════════
   9  BENCHMARK RESULT STRUCT
   ══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    double   time_min;
    double   time_avg;
    double   time_max;
    double   mem_mb;
    uint64_t comparisons;
    uint64_t swaps;
    int      stable;      /* -1=not checked, 0=unstable, 1=stable */
    int      skipped;
    int      error;
    int      runs;        /* how many repetitions were averaged */
} BenchResult;

/* ══════════════════════════════════════════════════════════════════════════
   10  BENCHMARK ENGINE
   ══════════════════════════════════════════════════════════════════════════ */

static BenchResult run_benchmark(SortFn fn, const int *src, int n) {
    BenchResult r; memset(&r,0,sizeof(r));
    r.stable = -1;

    int reps = (n <= SMALL_SIZE_THRESHOLD) ? SMALL_RUNS : 1;
    r.runs   = reps;

    int *arr = (int*)malloc(n*sizeof(int));
    if (!arr) { r.error=1; return r; }

    double t_sum=0, t_min=1e18, t_max=0;
    uint64_t total_cmp=0, total_sw=0;

    for (int rep=0; rep<reps; rep++) {
        memcpy(arr, src, n*sizeof(int));
        g_comparisons=0; g_swaps=0;

        double mem_before = get_memory_mb();
        double t0 = get_time_sec();
        fn(arr, n);
        double elapsed = get_time_sec() - t0;
        double mem_after = get_memory_mb();

        t_sum += elapsed;
        if (elapsed < t_min) t_min = elapsed;
        if (elapsed > t_max) t_max = elapsed;
        total_cmp += g_comparisons;
        total_sw  += g_swaps;

        if (rep==0) r.mem_mb = (mem_after>0 ? mem_after : mem_before);
    }

    r.time_min   = t_min;
    r.time_avg   = t_sum / reps;
    r.time_max   = t_max;
    r.comparisons= total_cmp / reps;
    r.swaps      = total_sw  / reps;

    /* Stability check (only on first rep's data, small sizes only) */
    if (n <= STABILITY_MAX) {
        r.stable = verify_stable(fn, src, n);
    }

    free(arr);
    return r;
}

/* ══════════════════════════════════════════════════════════════════════════
   11  PRETTY PRINT
   ══════════════════════════════════════════════════════════════════════════ */

static void print_line(char c, int w) {
    for (int i=0; i<w; i++) putchar(c);
    putchar('\n');
}

static void fmt_count(char *buf, uint64_t n) {
    if      (n >= 1000000000ULL) sprintf(buf,"%.2fB",(double)n/1e9);
    else if (n >= 1000000ULL)    sprintf(buf,"%.2fM",(double)n/1e6);
    else if (n >= 1000ULL)       sprintf(buf,"%.1fK",(double)n/1e3);
    else                          sprintf(buf,"%llu",(unsigned long long)n);
}

/* ══════════════════════════════════════════════════════════════════════════
   12  MAIN
   ══════════════════════════════════════════════════════════════════════════ */

int main(void) {

    /* results[dist][algo][size][cond] */
    static BenchResult results[NUM_DISTRIBUTIONS][NUM_ALGORITHMS]
                               [NUM_SIZES][NUM_CONDITIONS];
    memset(results, 0, sizeof(results));

    double wall_start = get_time_sec();

    /* ── PHASE 1: DATA GENERATION ──────────────────────────────────────── */
    print_line('=', 80);
    printf("  PHASE 1 -- DATA GENERATION\n");
    print_line('=', 80);

    make_dir(DATA_DIR);

    char data_paths[NUM_DISTRIBUTIONS][NUM_SIZES][NUM_CONDITIONS][300];
    int max_n = SIZES[NUM_SIZES-1];
    int *gen_buf = (int*)malloc(max_n*sizeof(int));
    if (!gen_buf) { fprintf(stderr,"Out of memory\n"); return 1; }

    int total_files = NUM_DISTRIBUTIONS * NUM_SIZES * NUM_CONDITIONS;
    int generated   = 0;

    for (int di=0; di<NUM_DISTRIBUTIONS; di++) {
        printf("\n  Distribution: %s\n", DIST_NAMES[di]);
        for (int si=0; si<NUM_SIZES; si++) {
            int n=SIZES[si];
            for (int ci=0; ci<NUM_CONDITIONS; ci++) {
                snprintf(data_paths[di][si][ci],
                         sizeof(data_paths[di][si][ci]),
                         DATA_DIR PATH_SEP "data_%s_%s_%s.bin",
                         DIST_NAMES[di], SIZE_LABELS[si], CONDITIONS[ci]);
                const char *path = data_paths[di][si][ci];
                if (file_exists(path)) {
                    printf("    [SKIP] %s_%s_%s.bin (%.1fMB)\n",
                           DIST_NAMES[di],SIZE_LABELS[si],CONDITIONS[ci],
                           file_mb(path));
                } else {
                    printf("    [GEN ] %s_%s_%s.bin ... ",
                           DIST_NAMES[di],SIZE_LABELS[si],CONDITIONS[ci]);
                    fflush(stdout);
                    double t0=get_time_sec();
                    generate(gen_buf,n,di,ci);
                    save_bin(gen_buf,n,path);
                    printf("done %.2fs %.1fMB\n",
                           get_time_sec()-t0, file_mb(path));
                    generated++;
                }
            }
        }
    }
    printf("\n  Data ready — %d new, %d total files in ./%s/\n\n",
           generated, total_files, DATA_DIR);
    free(gen_buf);

    /* ── PHASE 2: BENCHMARKING ─────────────────────────────────────────── */
    print_line('=', 80);
    printf("  PHASE 2 -- BENCHMARKING\n");
    print_line('=', 80);

    int *data_buf = (int*)malloc(max_n*sizeof(int));
    if (!data_buf) { fprintf(stderr,"Out of memory\n"); return 1; }

    for (int di=0; di<NUM_DISTRIBUTIONS; di++) {
        printf("\n");
        print_line('*', 80);
        printf("  DISTRIBUTION: %s\n", DIST_NAMES[di]);
        print_line('*', 80);

        for (int si=0; si<NUM_SIZES; si++) {
            int n=SIZES[si];
            int is_small = (n <= SMALL_SIZE_THRESHOLD);

            for (int ci=0; ci<NUM_CONDITIONS; ci++) {
                printf("\n");
                print_line('-', 80);
                printf("  > Dist:%-12s Size:%-6s(%d)  Cond:%s%s\n",
                       DIST_NAMES[di],SIZE_LABELS[si],n,CONDITIONS[ci],
                       is_small ? "  [repeated 50x]" : "");
                print_line('-', 80);
                printf("  %-22s %9s %9s %10s %10s  ST  Status\n",
                       "Algorithm","Time(s)","Mem(MB)","Compares","Swaps");
                printf("  %-22s %9s %9s %10s %10s  --  ------\n",
                       "----------------------","---------","---------",
                       "----------","----------");

                load_bin(data_buf, n, data_paths[di][si][ci]);

                for (int ai=0; ai<NUM_ALGORITHMS; ai++) {

                    /* Skip O(n^2) on large inputs */
                    if (SKIP_SLOW_FOR_LARGE && IS_SLOW[ai] && n>SLOW_ALGO_MAX_SIZE) {
                        results[di][ai][si][ci].skipped=1;
                        printf("  %-22s %9s %9s %10s %10s  -   SKIPPED\n",
                               ALGO_NAMES[ai],"-","-","-","-");
                        continue;
                    }

                    /* Skip Counting Sort on wide_range (OOM) */
                    if (ai==IDX_COUNTING && di==DIST_WIDE_RANGE) {
                        results[di][ai][si][ci].skipped=1;
                        printf("  %-22s %9s %9s %10s %10s  -   SKIPPED(range=n^3)\n",
                               ALGO_NAMES[ai],"-","-","-","-");
                        continue;
                    }

                    BenchResult r = run_benchmark(ALGO_FNS[ai], data_buf, n);
                    results[di][ai][si][ci] = r;

                    if (r.error) {
                        printf("  %-22s %9s %9s %10s %10s  -   ERROR\n",
                               ALGO_NAMES[ai],"-","-","-","-");
                    } else {
                        char cmp_s[24], sw_s[24];
                        fmt_count(cmp_s, r.comparisons);
                        fmt_count(sw_s,  r.swaps);
                        const char *st_str = (r.stable==1)?"Y":(r.stable==0)?"N":"-";
                        /* Show avg time; for small sizes also show min */
                        if (is_small)
                            printf("  %-22s %9.6f %9.2f %10s %10s  %-3s avg(min=%.6f)\n",
                                   ALGO_NAMES[ai],r.time_avg,r.mem_mb,
                                   cmp_s,sw_s,st_str,r.time_min);
                        else
                            printf("  %-22s %9.4f %9.2f %10s %10s  %-3s OK\n",
                                   ALGO_NAMES[ai],r.time_avg,r.mem_mb,
                                   cmp_s,sw_s,st_str);
                    }
                    fflush(stdout);
                }
            }
        }
    }
    free(data_buf);

    /* ── PHASE 3: CSV EXPORT ───────────────────────────────────────────── */
    printf("\n");
    print_line('=', 80);
    printf("  PHASE 3 -- CSV EXPORT\n");
    print_line('=', 80);

    FILE *csv = fopen(RESULTS_CSV, "w");
    if (!csv) { fprintf(stderr,"ERROR: cannot open %s\n",RESULTS_CSV); return 1; }

    /* Header */
    fprintf(csv,
        "distribution,"
        "algorithm,"
        "variant,"
        "size_label,"
        "size,"
        "condition,"
        "runs,"
        "time_min_s,"
        "time_avg_s,"
        "time_max_s,"
        "memory_mb,"
        "comparisons,"
        "swaps,"
        "stable_verified,"
        "known_stable,"
        "complexity_best,"
        "complexity_avg,"
        "complexity_worst,"
        "complexity_space,"
        "status\n"
    );

    for (int di=0; di<NUM_DISTRIBUTIONS; di++) {
        for (int ai=0; ai<NUM_ALGORITHMS; ai++) {
            for (int si=0; si<NUM_SIZES; si++) {
                for (int ci=0; ci<NUM_CONDITIONS; ci++) {
                    BenchResult *r = &results[di][ai][si][ci];

                    const char *status =
                        r->skipped ? "SKIPPED" :
                        r->error   ? "ERROR"   : "OK";

                    const char *stable_v =
                        (r->stable== 1) ? "stable"   :
                        (r->stable== 0) ? "unstable" : "not_checked";

                    if (r->skipped || r->error) {
                        fprintf(csv,
                            "%s,%s,%s,%s,%d,%s,"
                            "%d,,,,"          /* runs, times */
                            ",,"              /* memory, comparisons */
                            ","               /* swaps */
                            "%s,%s,"          /* stable_v, known_stable */
                            "%s,%s,%s,%s,"    /* complexity */
                            "%s\n",
                            DIST_NAMES[di], ALGO_NAMES[ai], VARIANT[ai],
                            SIZE_LABELS[si], SIZES[si], CONDITIONS[ci],
                            r->runs,
                            stable_v,
                            KNOWN_STABLE[ai] ? "stable" : "unstable",
                            COMPLEXITY[ai][0], COMPLEXITY[ai][1],
                            COMPLEXITY[ai][2], COMPLEXITY[ai][3],
                            status);
                    } else {
                        fprintf(csv,
                            "%s,%s,%s,%s,%d,%s,"
                            "%d,%.8f,%.8f,%.8f,"
                            "%.4f,%llu,%llu,"
                            "%s,%s,"
                            "%s,%s,%s,%s,"
                            "%s\n",
                            DIST_NAMES[di], ALGO_NAMES[ai], VARIANT[ai],
                            SIZE_LABELS[si], SIZES[si], CONDITIONS[ci],
                            r->runs,
                            r->time_min, r->time_avg, r->time_max,
                            r->mem_mb,
                            (unsigned long long)r->comparisons,
                            (unsigned long long)r->swaps,
                            stable_v,
                            KNOWN_STABLE[ai] ? "stable" : "unstable",
                            COMPLEXITY[ai][0], COMPLEXITY[ai][1],
                            COMPLEXITY[ai][2], COMPLEXITY[ai][3],
                            status);
                    }
                }
            }
        }
    }

    fclose(csv);
    printf("\n  Results saved to %s\n", RESULTS_CSV);
    printf("  Columns: distribution, algorithm, variant, size_label, size,\n");
    printf("           condition, runs, time_min_s, time_avg_s, time_max_s,\n");
    printf("           memory_mb, comparisons, swaps, stable_verified,\n");
    printf("           known_stable, complexity_best, complexity_avg,\n");
    printf("           complexity_worst, complexity_space, status\n");

    double wall_total = get_time_sec() - wall_start;
    printf("\n");
    print_line('=', 80);
    printf("  Total wall-clock time: %.1f s (%.1f min)\n",
           wall_total, wall_total/60.0);
    print_line('=', 80);

    printf("\n"
"  WHAT IS NEW vs v1\n"
"  -----------------------------------------------------------------------\n"
"  + Quick Sort iterative AND recursive variants (both benchmarked)\n"
"  + Merge Sort recursive AND iterative (bottom-up) variants\n"
"  + Comparison counter inside every sort (CMP macro)\n"
"  + Swap / write counter inside every sort (SWAP macro)\n"
"  + Stability verification for sizes <= 50K\n"
"  + all_identical condition (5th input condition)\n"
"  + Sizes 10K and 10M added (was missing the gap)\n"
"  + Small sizes (<= 1K) repeated 50 times, reports min/avg/max time\n"
"  + Full CSV with 20 columns per row including complexity theory\n"
"  -----------------------------------------------------------------------\n\n");

    return 0;
}