/*
 * tage/tage.h  –  Integration header for the TAGE branch predictor.
 *
 * This header is included by bpred.h (path: "tage/tage.h").
 * It intentionally has NO dependency on bpred.h (that would be circular);
 * instead it forward-declares struct bpred_dir_t and relies on bpred.h
 * having already pulled in machine.h (which defines md_addr_t) before
 * including this header.
 *
 * Table layout within bpred_dir_t.config.tage
 * ─────────────────────────────────────────────
 *  Index  hist_len  Tags?  Description
 *  ─────  ────────  ─────  ────────────────────────────────────────────────
 *    0       8        no   T1 – short-history untagged fallback component
 *    1      16       yes   T2 – first fully-tagged component
 *    2      32       yes   T3
 *    3      64       yes   T4 – longest-history component
 *
 *  T0 (base bimodal) lives in pred->dirpred.bimod and is managed by bpred.c.
 */

#ifndef TAGE_H
#define TAGE_H

/* ── Table-size constants (must match bpred.c's bpred_dir_create) ───────── */
#define NUM_TAGE_TABLES        5    /* 1 bimodal base + 4 tagged components */
#define TAGE_BASE_TABLE_SIZE   4096 /* entries in the T0 bimodal table       */
#define TAGE_TAGGED_TABLE_SIZE 1024 /* entries in each tagged component      */
#define TAGE_TAG_WIDTH         8    /* tag bits per entry                    */
#define TAGE_GEOMETRIC_FACTOR  2    /* history-length geometric ratio         */
#define TAGE_HISTORY_REG_SIZE  64   /* GHR width in bits                     */

#include <stdint.h>  /* uint64_t */

/*
 * Forward declaration – avoids the circular dependency
 * (bpred.h defines the full struct; tage.c includes bpred.h).
 */
struct bpred_dir_t;

/*
 * md_addr_t is defined in machine.h, which bpred.h includes before this
 * header.  When this header is included standalone (e.g. in unit tests),
 * the caller must ensure md_addr_t is defined first.
 */

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * bpred_dir_lookup_tage
 *
 * Probe all TAGE tagged components for a matching entry.  Returns a pointer
 * to the 3-bit prediction counter (unsigned char, range 0-7, taken ≥ 4)
 * of the provider component, or NULL if no tagged component matches (caller
 * falls back to the bimodal base).
 *
 * Side-effect: updates internal per-lookup state used by bpred_dir_update_tage.
 */
char *bpred_dir_lookup_tage(struct bpred_dir_t *pred_dir, md_addr_t baddr);

/*
 * bpred_dir_update_tage
 *
 * Update TAGE state after a branch is resolved:
 *   – updates the provider component's prediction counter,
 *   – updates the provider's usefulness bit,
 *   – allocates a new entry on misprediction (if a longer component is free),
 *   – shifts the new outcome into the global history register.
 *
 * Returns 0 (currently unused).
 */
int bpred_dir_update_tage(struct bpred_dir_t *pred_dir,
                          md_addr_t           baddr,
                          md_addr_t           btarget,
                          int                 taken,
                          int                 pred_taken,
                          char               *dir_update_ptr);

#endif /* TAGE_H */
