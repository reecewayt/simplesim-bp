/*
 * tage_core.h - Pure algorithmic helpers for the TAGE branch predictor.
 *
 * All functions are static inline so this header can be included in both
 * the integration implementation (tage/tests/tage.c) and the standalone
 * unit-test file (tage/tests/test_tage.c) without any external linkage
 * or simulator-infrastructure dependencies.
 */

#ifndef TAGE_CORE_H
#define TAGE_CORE_H

#include <stdint.h>

/*
 * tage_fold_history
 *
 * XOR-fold the lowest 'hist_len' bits of 'history' into 'out_bits' bits.
 *
 * This is the standard "folded history" technique used in TAGE to map a
 * long history register into a short index without aliasing bias:
 *
 *   result = history[0..out_bits-1]
 *          ^ history[out_bits..2*out_bits-1]
 *          ^ ...
 *
 * Only bits 0..(hist_len-1) of 'history' are considered.
 * 'out_bits' must be in [1, 32].
 * 'hist_len' must be in [1, 64].
 **/
static inline uint32_t
tage_fold_history(uint64_t history, int hist_len, int out_bits)
{
    /* Mask to keep only the relevant history bits */
    uint64_t hist_mask = (hist_len < 64) ? ((1ULL << hist_len) - 1ULL) : UINT64_MAX;
    uint64_t h         = history & hist_mask;

    uint32_t out_mask = (out_bits < 32) ? ((1u << out_bits) - 1u) : UINT32_MAX;
    uint32_t result   = 0;

    while (h) {
        result ^= (uint32_t)(h & (uint64_t)out_mask);
        h >>= out_bits;
    }
    return result & out_mask;
}

/*
 * tage_log2
 *
 * Returns ceil(log2(size)), i.e. the number of bits required to index a
 * table of 'size' entries.  Assumes size is a positive power of two.
 * */
static inline int
tage_log2(int size)
{
    int bits = 0;
    while ((1 << bits) < size)
        bits++;
    return bits;
}

/*
 * tage_compute_index
 *
 * Compute the row index into a tagged TAGE table.
 *
 *   index = (PC[31:2] XOR fold(GHR[0..hist_len-1], idx_bits)) & (table_size-1)
 *
 * Using PC >> 2 removes the always-zero low-order bits (instructions are
 * at least 4-byte aligned in PISA/Alpha).
 **/
static inline int
tage_compute_index(uint32_t pc, uint64_t history, int hist_len, int table_size)
{
    int      idx_bits = tage_log2(table_size);
    uint32_t folded   = tage_fold_history(history, hist_len, idx_bits);
    return (int)(((pc >> 2) ^ folded) & (uint32_t)(table_size - 1));
}

/*
 * tage_compute_tag
 *
 * Compute the tag for a given PC and GHR snapshot.
 *
 *   tag = (PC[31:2] XOR fold(GHR[0..hist_len-1], tag_width)) & tag_mask
 **/
static inline uint8_t
tage_compute_tag(uint32_t pc, uint64_t history, int hist_len, int tag_width)
{
    uint32_t folded   = tage_fold_history(history, hist_len, tag_width);
    uint32_t tag_mask = (1u << tag_width) - 1u;
    return (uint8_t)(((pc >> 2) ^ folded) & tag_mask);
}

/*
 * TAGE 3-bit counter helpers (unsigned storage, range 0-7)
 *
 * Encoding: values 0-3 → predict NOT TAKEN; 4-7 → predict TAKEN.
 * Threshold: taken if counter >= TAGE_CTR_THRESHOLD (4).
 **/
#define TAGE_CTR_MAX          7u
#define TAGE_CTR_MIN          0u
#define TAGE_CTR_THRESHOLD    4u  /* taken iff counter >= this value */
#define TAGE_CTR_WEAK_TAKEN   4u  /* newly allocated, outcome = taken     */
#define TAGE_CTR_WEAK_NOTTAKEN 3u /* newly allocated, outcome = not-taken */

static inline int
tage_ctr_pred(unsigned char ctr)
{
    return ctr >= TAGE_CTR_THRESHOLD;
}

static inline void
tage_ctr_update(unsigned char *p, int taken)
{
    if (taken) {
        if (*p < TAGE_CTR_MAX) (*p)++;
    } else {
        if (*p > TAGE_CTR_MIN) (*p)--;
    }
}

/* 1-bit usefulness saturating up/down (values: 0 = not useful, 1 = useful) */
static inline void tage_u_inc(unsigned char *p) { if (*p < 1) (*p)++; }
static inline void tage_u_dec(unsigned char *p) { if (*p > 0) (*p)--; }

#endif /* TAGE_CORE_H */
