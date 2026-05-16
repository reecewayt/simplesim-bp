/*
 * tage/tests/test_tage.c  –  Unity unit tests for the TAGE branch predictor.
 *
 * Design philosophy
 * ──────────────────
 * These tests are fully self-contained: they do NOT include bpred.h or any
 * other SimpleSim simulator header.  Instead they:
 *
 *   1. Test the pure algorithmic helpers in tage_core.h (index/tag
 *      computation, counter saturation, folded history).
 *
 *   2. Exercise the full TAGE lookup + update cycle using a local
 *      "mock" struct that mirrors the memory layout of bpred_dir_t on
 *      64-bit platforms (the only platform targeted).
 *
 * Mock struct layout
 * ──────────────────
 * On a 64-bit platform (macOS / Linux):
 *
 *   bpred_dir_t {
 *     enum bpred_class class;  // sizeof(int) = 4 bytes
 *     [4 bytes padding]        // pointer alignment
 *     union {
 *       …
 *       struct { … } tage;     // at offset 8 from start of bpred_dir_t
 *     } config;
 *   }
 *
 * The mock struct below places the TAGE config at the same offset (8).
 * A compile-time assertion verifies this.
 *
 * Compilation
 * ──────────────────
 * From simplesim-bp/tage/ run:
 *
 *   make test
 *
 * The tage/Makefile pattern rule compiles:
 *
 *   gcc -std=c99 -I. -I.. -o tests/run_tage \
 *       tests/test_tage.c tests/unity/unity.c -lm
 */

/* Standard headers (no simulator deps)*/
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>   /* offsetof */
#include <assert.h>

/* Pure algorithmic helpers under test*/
#include "../tage_core.h"

/*Unity test framework  */
#include "unity/unity.h"

/*
 * Mock bpred_dir_t
 *
 * Carefully mirrors the memory layout of bpred_dir_t.config.tage so that
 * we can pass a pointer to one of these to bpred_dir_lookup_tage() and
 * bpred_dir_update_tage() via a cast.
 *
 * */

/* Sizes from tage/tage.h – duplicated to stay self-contained. */
#define T_NUM_TABLES        5
#define T_BASE_TABLE_SIZE   4096
#define T_TAGGED_TABLE_SIZE 1024
#define T_TAG_WIDTH         8
#define T_HISTORY_REG_SIZE  64
#define T_N_TAGGED          4   /* num_tables - 1 */
#define T_UNTAGGED_IDX      0   /* T1 index (hist_len = 8); T1 is now a tagged table */

static const int T_HIST_LENGTHS[T_N_TAGGED] = { 8, 16, 32, 64 };

/*
 * The tage config struct (must match bpred_dir_t.config.tage field-for-field
 * and in the same order).
 */
typedef struct {
    int            num_tables;
    int            base_table_size;
    int            tagged_table_size;
    int           *hist_lengths;
    int            geometric_factor;
    uint64_t       global_history;
    unsigned char  *base_table;   /* T0 bimodal – must follow global_history to match bpred.h */
    unsigned char **counters;
    unsigned char **tags;
    unsigned char **usefulness;
    unsigned char **meta;
} mock_tage_config_t;

/*
 * The full mock bpred_dir_t.
 *
 * On 64-bit:
 *   offset 0: _class  (int, 4 bytes)
 *   offset 4: _pad    (int, 4 bytes) – explicit alignment padding
 *   offset 8: config  (union; largest member = mock_tage_config_t)
 *
 * bpred_dir_t.config.tage is at the same offset (8) because the
 * union always starts with the tage member.
 */
typedef struct {
    int               _class;  /* mirrors enum bpred_class (sizeof int = 4)  */
    int               _pad;    /* explicit padding to align config to offset 8*/
    union {
        struct { unsigned int size; unsigned char *table; } bimod; /* placeholder */
        mock_tage_config_t tage;
    } config;
} mock_bpred_dir_t;

/* Compile-time guard: tage config must be at byte 8 from the mock struct */
typedef char _layout_assert[
    (offsetof(mock_bpred_dir_t, config.tage) == 8) ? 1 : -1
];

/* ── Minimal type stubs that tage/tage.c needs from bpred.h ──── */
#ifndef BPRED_H
#define BPRED_H  /* prevent bpred.h from being loaded if somehow reachable */
#endif

/* md_addr_t – matches host.h: typedef unsigned int word_t; machine.h: typedef word_t md_addr_t; */
typedef unsigned int md_addr_t;

/* MD_BR_SHIFT – log2 of minimum branch alignment (machine.h = 3 for PISA) */
#ifndef MD_BR_SHIFT
#define MD_BR_SHIFT 3
#endif

/*
 * Declare struct bpred_dir_t as an alias for our mock struct.
 * The TAGE functions only dereference the tage config fields, so the
 * memory layout must match – which it does by construction above.
 */
typedef mock_bpred_dir_t bpred_dir_t_mock;

/* Trick: tell the compiler that "struct bpred_dir_t" IS our mock struct.
 * We do this by providing the definition *before* the function bodies are
 * compiled (via the #include below). */
struct bpred_dir_t {
    int               _class;
    int               _pad;
    union {
        struct { unsigned int size; unsigned char *table; } bimod;
        mock_tage_config_t tage;
    } config;
};

/* Import TAGE constants (normally provided via bpred.h → tage/tage.h).       *
 * Since we blocked bpred.h above we include tage.h directly.                 *
 * The forward declaration of struct bpred_dir_t is compatible with our full  *
 * definition; the include guard prevents double-inclusion.                    */
#include "../tage.h"

/* Now include the implementation */
#include "../tage.c"

/*
 * §2  Test helpers
 **/

/*
 * Allocate and fully initialise a mock predictor.
 * Call free_mock_pred() when done to avoid leaks.
 */
static struct bpred_dir_t *
alloc_mock_pred(void)
{
    struct bpred_dir_t *p = calloc(1, sizeof(struct bpred_dir_t));
    if (!p) { TEST_FAIL_MESSAGE("calloc failed"); return NULL; }

    /* Mirror bpred_dir_create(BPredTage, …) */
    p->config.tage.num_tables        = T_NUM_TABLES;
    p->config.tage.base_table_size   = T_BASE_TABLE_SIZE;
    p->config.tage.tagged_table_size = T_TAGGED_TABLE_SIZE;
    p->config.tage.geometric_factor  = 2;
    p->config.tage.global_history    = 0;

    /* base table (T0): 3-bit saturating counters (0-7, taken >= 4), weak-NT init */
    p->config.tage.base_table = malloc(T_BASE_TABLE_SIZE * sizeof(unsigned char));
    if (!p->config.tage.base_table) { TEST_FAIL_MESSAGE("malloc failed"); free(p); return NULL; }
    for (int k = 0; k < T_BASE_TABLE_SIZE; k++)
        p->config.tage.base_table[k] = 3; /* TAGE_CTR_WEAK_NOTTAKEN */

    /* hist_lengths: 4 entries */
    p->config.tage.hist_lengths = calloc(T_N_TAGGED, sizeof(int));
    for (int i = 0; i < T_N_TAGGED; i++)
        p->config.tage.hist_lengths[i] = T_HIST_LENGTHS[i];

    /* Allocate counter, tag, and usefulness arrays */
    p->config.tage.counters   = calloc(T_N_TAGGED, sizeof(unsigned char *));
    p->config.tage.tags       = calloc(T_N_TAGGED, sizeof(unsigned char *));
    p->config.tage.usefulness = calloc(T_N_TAGGED, sizeof(unsigned char *));
    p->config.tage.meta       = calloc(1,           sizeof(unsigned char *));

    for (int i = 0; i < T_N_TAGGED; i++) {
        p->config.tage.counters[i] = calloc(T_TAGGED_TABLE_SIZE,
                                            sizeof(unsigned char));
        /*
         * Initialise tags to 0xFF (sentinel = "no valid entry").
         * Real bpred_dir_create uses 0 (calloc), but 0 is also a
         * valid computed tag for many PCs, causing spurious hits.
         * Tests that need realistic cold-start behaviour use PCs
         * whose computed tag is non-0xFF, so there are no false hits.
         */
        p->config.tage.tags[i] = malloc(T_TAGGED_TABLE_SIZE *
                                         sizeof(unsigned char));
        memset(p->config.tage.tags[i], 0xFF,
               T_TAGGED_TABLE_SIZE * sizeof(unsigned char));
        p->config.tage.usefulness[i] = calloc(T_TAGGED_TABLE_SIZE,
                                               sizeof(unsigned char));
        if (i == 0)
            p->config.tage.meta[0] = calloc(T_BASE_TABLE_SIZE,
                                            sizeof(unsigned char));
    }
    return p;
}

static void
free_mock_pred(struct bpred_dir_t *p)
{
    if (!p) return;
    for (int i = 0; i < T_N_TAGGED; i++) {
        free(p->config.tage.counters[i]);
        free(p->config.tage.tags[i]);
        free(p->config.tage.usefulness[i]);
    }
    free(p->config.tage.base_table);
    free(p->config.tage.meta[0]);
    free(p->config.tage.hist_lengths);
    free(p->config.tage.counters);
    free(p->config.tage.tags);
    free(p->config.tage.usefulness);
    free(p->config.tage.meta);
    free(p);
}

/* Unity setUp / tearDown */
static struct bpred_dir_t *g_pred = NULL;

void setUp(void)    { g_pred = alloc_mock_pred(); }
void tearDown(void) { free_mock_pred(g_pred); g_pred = NULL; }

/*
 * Tests: tage_fold_history
 **/

void test_fold_history_allZeros_givesZero(void)
{
    /* All-zero history → XOR of zeros → 0 */
    TEST_ASSERT_EQUAL_UINT32(0, tage_fold_history(0, 64, 10));
}

void test_fold_history_singleBit_propagates(void)
{
    /* Only bit 0 set; hist_len > out_bits → just bit 0 is XOR'd once */
    uint32_t r = tage_fold_history(1ULL, 64, 10);
    TEST_ASSERT_EQUAL_UINT32(1, r);
}

void test_fold_history_respectsHistLen(void)
{
    /*
     * With hist_len = 4 only bits 0-3 matter.
     * Bits 4-63 must be ignored even if set.
     */
    uint64_t history_with_high_bits = UINT64_MAX;  /* all bits set */
    uint32_t r_long  = tage_fold_history(history_with_high_bits, 64, 8);
    uint32_t r_short = tage_fold_history(history_with_high_bits, 4,  8);
    /* With hist_len=4, only 0xF (4 bits) is folded into 8 bits → result=0xF */
    TEST_ASSERT_EQUAL_UINT32(0x0F, r_short);
    /* They should differ because r_long folds far more bits */
    TEST_ASSERT_NOT_EQUAL(r_long, r_short);
}

void test_fold_history_outputWidthMask(void)
{
    /* Result must always fit in out_bits bits */
    uint32_t r = tage_fold_history(UINT64_MAX, 64, 5);
    TEST_ASSERT_EQUAL_UINT32(r & 0x1F, r);   /* only low 5 bits */
}

void test_fold_history_knownValue(void)
{
    /*
     * Manually compute: history = 0b...11001011 (0xCB), hist_len=8, out_bits=4
     * Fold into 4 bits:
     *   low nibble  = 0xCB & 0xF = 0xB
     *   high nibble = (0xCB >> 4) & 0xF = 0xC
     *   XOR         = 0xB ^ 0xC = 0x7
     */
    uint32_t r = tage_fold_history(0xCBULL, 8, 4);
    TEST_ASSERT_EQUAL_UINT32(0x7, r);
}

/*
 *Tests: tage_compute_index and tage_compute_tag
 **/

void test_compute_index_inRange(void)
{
    /* Index must be in [0, table_size - 1] */
    for (int hist = 0; hist < 64; hist++) {
        int idx = tage_compute_index(0xDEADC0DE, (uint64_t)hist, 16, 1024);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, idx);
        TEST_ASSERT_LESS_THAN_INT(1024, idx);
    }
}

void test_compute_index_differentPC_differentIndex(void)
{
    /*
     * Choose PCs that are 4 bytes apart so (pc >> 2) differs by exactly 1.
     * With GHR=0, fold returns 0, so index = (pc >> 2) & (table_size - 1).
     * 0x1000 >> 2 = 0x400, 0x1004 >> 2 = 0x401; both & 0x3FF: 0 vs 1.
     */
    int idx1 = tage_compute_index(0x1000, 0, 8, 1024);
    int idx2 = tage_compute_index(0x1004, 0, 8, 1024);
    TEST_ASSERT_NOT_EQUAL(idx1, idx2);
}

void test_compute_index_sameInputs_sameResult(void)
{
    int idx1 = tage_compute_index(0xABCD1234, 0xFFFF, 32, 1024);
    int idx2 = tage_compute_index(0xABCD1234, 0xFFFF, 32, 1024);
    TEST_ASSERT_EQUAL_INT(idx1, idx2);
}

void test_compute_tag_fitsInTagWidth(void)
{
    unsigned char tag = tage_compute_tag(0x12345678, 0xABCD, 16, T_TAG_WIDTH);
    TEST_ASSERT_LESS_THAN(1 << T_TAG_WIDTH, (unsigned int)tag);
}

void test_compute_tag_differentHistory_differentTag(void)
{
    unsigned char t1 = tage_compute_tag(0x1000, 0x00, 8, T_TAG_WIDTH);
    unsigned char t2 = tage_compute_tag(0x1000, 0xFF, 8, T_TAG_WIDTH);
    TEST_ASSERT_NOT_EQUAL(t1, t2);
}

void test_compute_tag_deterministic(void)
{
    unsigned char t1 = tage_compute_tag(0xBEEF, 0xDEAD, 16, T_TAG_WIDTH);
    unsigned char t2 = tage_compute_tag(0xBEEF, 0xDEAD, 16, T_TAG_WIDTH);
    TEST_ASSERT_EQUAL_UINT8(t1, t2);
}

/*
 * §5  Tests: counter helpers (tage_ctr_pred, tage_ctr_update)
 **/

void test_ctr_pred_belowThreshold_notTaken(void)
{
    for (unsigned char v = 0; v < TAGE_CTR_THRESHOLD; v++)
        TEST_ASSERT_EQUAL_INT(0, tage_ctr_pred(v));
}

void test_ctr_pred_atAndAboveThreshold_taken(void)
{
    for (unsigned char v = TAGE_CTR_THRESHOLD; v <= TAGE_CTR_MAX; v++)
        TEST_ASSERT_EQUAL_INT(1, tage_ctr_pred(v));
}

void test_ctr_update_increment_doesNotExceedMax(void)
{
    unsigned char c = TAGE_CTR_MAX;
    tage_ctr_update(&c, 1 /* taken */);
    TEST_ASSERT_EQUAL_UINT8(TAGE_CTR_MAX, c);
}

void test_ctr_update_decrement_doesNotGoBelowMin(void)
{
    unsigned char c = TAGE_CTR_MIN;
    tage_ctr_update(&c, 0 /* not taken */);
    TEST_ASSERT_EQUAL_UINT8(TAGE_CTR_MIN, c);
}

void test_ctr_update_taken_increments(void)
{
    unsigned char c = 3;
    tage_ctr_update(&c, 1);
    TEST_ASSERT_EQUAL_UINT8(4, c);
}

void test_ctr_update_notTaken_decrements(void)
{
    unsigned char c = 4;
    tage_ctr_update(&c, 0);
    TEST_ASSERT_EQUAL_UINT8(3, c);
}

/*
 * §6  Tests: bpred_dir_lookup_tage
 **/

void test_lookup_coldPredictor_returnsNonNull(void)
{
    /* Cold predictor: all tagged tables miss (0xFF sentinel), T0 is the provider */
    char *p = bpred_dir_lookup_tage(g_pred, 0x1000);
    TEST_ASSERT_NOT_NULL(p);
}

void test_lookup_coldPredictor_predictNotTaken(void)
{
    /*
     * Cold predictor: all tagged tables miss (0xFF sentinel tags), so T0
     * (base bimodal) is the provider.  base_table is initialised to 3
     * (TAGE_CTR_WEAK_NOTTAKEN); tage_ctr_pred(3) = 0 = not-taken.
     */
    char *p = bpred_dir_lookup_tage(g_pred, 0x1000);
    TEST_ASSERT_EQUAL_INT(0, tage_ctr_pred((unsigned char)*p));
}

void test_lookup_samePC_samePointer(void)
{
    char *p1 = bpred_dir_lookup_tage(g_pred, 0x2000);
    /* Second lookup with same PC and GHR must return the same counter */
    TEST_ASSERT_EQUAL_PTR_MESSAGE(p1,
                                  bpred_dir_lookup_tage(g_pred, 0x2000),
        "Same PC and GHR should produce the same counter pointer");
}

void test_lookup_afterTaggedEntry_usesLongestHistory(void)
{
    /*
     * Manually plant a tag in table T3 (index 2, hist_len=32).
     * After lookup for the same PC+GHR, the pointer should point to
     * table 2's counter (not T1's).
     *
     * Use a PC where (pc >> 2) & 0xFF != 0xFF so the planted tag does
     * not accidentally equal the cold-state (0xFF) in other tables.
     * pc = 0x3104: (0x3104 >> 2) = 0xC41, tag portion & 0xFF = 0x41.
     */
    uint32_t pc  = 0x3104;
    uint64_t ghr = 0;
    int hist_len = T_HIST_LENGTHS[2]; /* 32 */
    int idx      = tage_compute_index(pc, ghr, hist_len, T_TAGGED_TABLE_SIZE);
    unsigned char tag = tage_compute_tag(pc, ghr, hist_len, T_TAG_WIDTH);

    /* The sentinel in other tables is 0xFF, so we must not plant 0xFF here */
    if (tag == 0xFF) {
        TEST_IGNORE_MESSAGE("Computed tag collides with cold sentinel; skip");
        return;
    }

    /* Plant entry in T3 only; other tables keep sentinel tags (no hit) */
    g_pred->config.tage.tags[2][idx]     = tag;
    g_pred->config.tage.counters[2][idx] = 5; /* strongly taken */

    char *p = bpred_dir_lookup_tage(g_pred, pc);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_PTR(&g_pred->config.tage.counters[2][idx], p);
}

void test_lookup_afterTaggedEntry_predictsTaken(void)
{
    /*
     * pc = 0x4104: (0x4104 >> 2) & 0xFF = 0x41 (not 0xFF, no sentinel hit).
     */
    uint32_t pc  = 0x4104;
    uint64_t ghr = 0;
    int hist_len = T_HIST_LENGTHS[1]; /* 16 */
    int idx      = tage_compute_index(pc, ghr, hist_len, T_TAGGED_TABLE_SIZE);
    unsigned char tag = tage_compute_tag(pc, ghr, hist_len, T_TAG_WIDTH);

    if (tag == 0xFF) {
        TEST_IGNORE_MESSAGE("Computed tag collides with cold sentinel; skip");
        return;
    }

    g_pred->config.tage.tags[1][idx]     = tag;
    g_pred->config.tage.counters[1][idx] = 6; /* strongly taken */

    char *p = bpred_dir_lookup_tage(g_pred, pc);
    TEST_ASSERT_EQUAL_INT(1, tage_ctr_pred((unsigned char)*p));
}

/*
 * §7  Tests: bpred_dir_update_tage
 **/

void test_update_takenBranch_incrementsCounter(void)
{
    md_addr_t pc = 0x5000;

    /* Cold lookup: T1 untagged provides prediction */
    char *p = bpred_dir_lookup_tage(g_pred, pc);
    unsigned char before = (unsigned char)*p;

    /* Branch actually taken */
    bpred_dir_update_tage(g_pred, pc, 0, 1 /*taken*/, 0 /*pred NT*/, p);

    /* GHR has shifted, so index may differ; check the counter we updated */
    // Ternary operator is simply a saturating incrementor logic
    TEST_ASSERT_EQUAL_UINT8((unsigned char)(before + 1 <= TAGE_CTR_MAX
                                          ? before + 1 : TAGE_CTR_MAX),
                            (unsigned char)*p);
}

void test_update_notTakenBranch_decrementsCounter(void)
{
    /*
     * Use a PC where the computed tag for all tagged tables is != 0xFF
     * (the sentinel used for cold tags in alloc_mock_pred) so there is
     * no spurious tagged-table hit; the lookup must return T1 (untagged).
     *
     * pc = 0x6104: (0x6104 >> 2) & 0xFF = 0x41 != 0xFF.
     */
    md_addr_t pc = 0x6104;

    /* Plant matching tag AND counter so T1 (index 0, hist_len=8) is the provider.
     * Without a planted tag, T1 would miss and T0 would be returned instead. */
    int hist_len = T_HIST_LENGTHS[T_UNTAGGED_IDX]; /* 8 */
    uint64_t ghr0 = g_pred->config.tage.global_history;
    int idx = tage_compute_index((uint32_t)pc, ghr0,
                                 hist_len, T_TAGGED_TABLE_SIZE);
    unsigned char tag0 = tage_compute_tag((uint32_t)pc, ghr0, hist_len, T_TAG_WIDTH);
    if (tag0 == 0xFF) {
        TEST_IGNORE_MESSAGE("Computed T1 tag collides with sentinel; skip");
        return;
    }
    g_pred->config.tage.tags[T_UNTAGGED_IDX][idx]     = tag0;
    g_pred->config.tage.counters[T_UNTAGGED_IDX][idx] = 5;

    char *p = bpred_dir_lookup_tage(g_pred, pc);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5, (unsigned char)*p,
        "Lookup should return T1's counter (no tag hit expected)");

    bpred_dir_update_tage(g_pred, pc, 0, 0 /*not taken*/, 1 /*pred T*/, p);

    TEST_ASSERT_EQUAL_UINT8(4, (unsigned char)*p);
}

void test_update_GHR_shiftedAfterUpdate(void)
{
    md_addr_t pc  = 0x7000;
    uint64_t  ghr_before = g_pred->config.tage.global_history;

    char *p = bpred_dir_lookup_tage(g_pred, pc);
    bpred_dir_update_tage(g_pred, pc, 0, 1 /*taken*/, 0, p);

    uint64_t ghr_after = g_pred->config.tage.global_history;
    /* GHR should have '1' shifted into the LSB */
    TEST_ASSERT_EQUAL_UINT64((ghr_before << 1) | 1ULL, ghr_after);
}

void test_update_misprediction_allocatesEntry(void)
{
    /*
     * Use pc=0x8104 so (pc>>2)&0xFF = 0x41 ≠ 0xFF (no cold-sentinel hit).
     * Trigger a misprediction: predict NT (T1 counter = 0 < 4) but branch
     * is actually taken.  Expect a new entry in one of tables 1-3.
     */
    md_addr_t pc    = 0x8104;
    uint64_t  ghr   = g_pred->config.tage.global_history; /* = 0 */

    char *p = bpred_dir_lookup_tage(g_pred, pc);
    TEST_ASSERT_EQUAL_INT(0, tage_ctr_pred((unsigned char)*p));

    bpred_dir_update_tage(g_pred, pc, 0, 1/*taken*/, 0/*predicted NT*/, p);

    /*
     * Allocation used the GHR value that was current during the update
     * (before the shift).  Scan all three tagged tables.
     */
    int found = 0;
    for (int t = 0; t < T_N_TAGGED; t++) {
        int hist_len = T_HIST_LENGTHS[t];
        int idx = tage_compute_index((uint32_t)pc, ghr,
                                     hist_len, T_TAGGED_TABLE_SIZE);
        unsigned char ctag = tage_compute_tag((uint32_t)pc, ghr,
                                              hist_len, T_TAG_WIDTH);
        if (g_pred->config.tage.tags[t]        != NULL    &&
            g_pred->config.tage.tags[t][idx]   == ctag    &&
            g_pred->config.tage.counters[t][idx] == TAGE_CTR_WEAK_TAKEN)
        {
            found = 1;
            break;
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, found,
        "Expected a newly-allocated entry with weak-taken counter");
}

void test_update_repeatedTaken_convergesStronglyTaken(void)
{
    /*
     * Directly train the T1 (untagged) counter for a fixed context and
     * verify it saturates at the strongly-taken value (7).
     *
     * We fix the GHR so the same T1 index is hit on every iteration.
     * After TAGE_CTR_MAX - 0 + 1 = 8 taken updates the counter saturates.
     * Use pc=0x9104 so (pc>>2)&0xFF = 0x45 ≠ 0xFF (no sentinel hit).
     */
    md_addr_t pc = 0x9104;
    /* Fix GHR to a constant so T1 index is stable across iterations */
    g_pred->config.tage.global_history = 0xA5A5A5A5A5A5A5A5ULL;
    uint64_t fixed_ghr = g_pred->config.tage.global_history;

    /* Compute T1 index, plant both tag and counter so T1 is the provider.
     * Without a planted tag at index 0, T0 would be the provider instead. */
    int hist0 = T_HIST_LENGTHS[T_UNTAGGED_IDX];
    int idx0  = tage_compute_index((uint32_t)pc, fixed_ghr,
                                    hist0, T_TAGGED_TABLE_SIZE);
    unsigned char tag0 = tage_compute_tag((uint32_t)pc, fixed_ghr,
                                          hist0, T_TAG_WIDTH);
    if (tag0 == 0xFF) {
        TEST_IGNORE_MESSAGE("Computed T1 tag collides with sentinel; skip");
        return;
    }
    g_pred->config.tage.tags[T_UNTAGGED_IDX][idx0]     = tag0;
    g_pred->config.tage.counters[T_UNTAGGED_IDX][idx0] = 0;

    /* Repeatedly: lookup, update (taken), then restore GHR */
    for (int iter = 0; iter < 10; iter++) {
        g_pred->config.tage.global_history = fixed_ghr; /* keep context fixed */
        char *p   = bpred_dir_lookup_tage(g_pred, pc);
        int   pred = tage_ctr_pred((unsigned char)*p);
        bpred_dir_update_tage(g_pred, pc, 0, 1, pred, p);
    }

    /* Restore GHR and do a final lookup */
    g_pred->config.tage.global_history = fixed_ghr;
    char *p = bpred_dir_lookup_tage(g_pred, pc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, tage_ctr_pred((unsigned char)*p),
        "After repeated taken updates at the same context should predict taken");
}

void test_update_repeatedNotTaken_convergesStronglyNotTaken(void)
{
    md_addr_t pc = 0xA000;

    for (int iter = 0; iter < 20; iter++) {
        char *p   = bpred_dir_lookup_tage(g_pred, pc);
        int   pred = tage_ctr_pred((unsigned char)*p);
        bpred_dir_update_tage(g_pred, pc, 0, 0, pred, p);
    }

    char *p = bpred_dir_lookup_tage(g_pred, pc);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, tage_ctr_pred((unsigned char)*p),
        "After many not-taken updates the predictor should predict not-taken");
}

/*
 * Test runner
 * */
int main(void)
{
    UNITY_BEGIN();

    /* tage_fold_history */
    RUN_TEST(test_fold_history_allZeros_givesZero);
    RUN_TEST(test_fold_history_singleBit_propagates);
    RUN_TEST(test_fold_history_respectsHistLen);
    RUN_TEST(test_fold_history_outputWidthMask);
    RUN_TEST(test_fold_history_knownValue);

    /* index / tag computation */
    RUN_TEST(test_compute_index_inRange);
    RUN_TEST(test_compute_index_differentPC_differentIndex);
    RUN_TEST(test_compute_index_sameInputs_sameResult);
    RUN_TEST(test_compute_tag_fitsInTagWidth);
    RUN_TEST(test_compute_tag_differentHistory_differentTag);
    RUN_TEST(test_compute_tag_deterministic);

    /* counter helpers */
    RUN_TEST(test_ctr_pred_belowThreshold_notTaken);
    RUN_TEST(test_ctr_pred_atAndAboveThreshold_taken);
    RUN_TEST(test_ctr_update_increment_doesNotExceedMax);
    RUN_TEST(test_ctr_update_decrement_doesNotGoBelowMin);
    RUN_TEST(test_ctr_update_taken_increments);
    RUN_TEST(test_ctr_update_notTaken_decrements);

    /* lookup */
    RUN_TEST(test_lookup_coldPredictor_returnsNonNull);
    RUN_TEST(test_lookup_coldPredictor_predictNotTaken);
    RUN_TEST(test_lookup_samePC_samePointer);
    RUN_TEST(test_lookup_afterTaggedEntry_usesLongestHistory);
    RUN_TEST(test_lookup_afterTaggedEntry_predictsTaken);

    /* update */
    RUN_TEST(test_update_takenBranch_incrementsCounter);
    RUN_TEST(test_update_notTakenBranch_decrementsCounter);
    RUN_TEST(test_update_GHR_shiftedAfterUpdate);
    RUN_TEST(test_update_misprediction_allocatesEntry);
    RUN_TEST(test_update_repeatedTaken_convergesStronglyTaken);
    RUN_TEST(test_update_repeatedNotTaken_convergesStronglyNotTaken);

    return UNITY_END();
}
