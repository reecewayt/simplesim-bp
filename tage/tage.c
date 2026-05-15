
/*
 * tage/tage.c  –  TAGE branch-predictor implementation.
 *
 * This file is compiled by the main SimpleSim Makefile as:
 *
 *   gcc $(CFLAGS) -c $(TAGE_FLAG) tage/tage.c -o tage/tage.o
 *
 * Include path notes (paths relative to this file's location tage/):
 *   "../bpred.h"   →  simplesim-bp/bpred.h   (full bpred_dir_t definition)
 *   "tage_core.h"  →  simplesim-bp/tage/tage_core.h  (pure algo helpers)
 *
 * ── TAGE component layout ─────────────────────────────────────────────────
 *  Index  hist_len  Has tags  Role
 *  ─────  ────────  ────────  ─────────────────────────────────────────────
 *    0       8         no     T1 – short-history untagged fallback
 *    1      16        yes     T2 – tagged, short-medium history
 *    2      32        yes     T3 – tagged, medium history
 *    3      64        yes     T4 – tagged, longest history
 *
 *  T0 (base bimodal) is managed by bpred.c via pred->dirpred.bimod.
 *
 * ── Counter encoding ─────────────────────────────────────────────────────
 *  3-bit unsigned char, stored in [0, 7].
 *  Taken  ⟺  counter ≥ 4.   Saturates at 0 / 7.
 *  Newly allocated entry initialised to 4 (weak-taken) or 3 (weak-not-taken).
 *
 * ── Usefulness bits ──────────────────────────────────────────────────────
 *  1-bit per tagged entry (0 = replaceable, 1 = useful).
 *  Periodically reset every TAGE_RESET_PERIOD updates to free stale entries.
 */

#include "../bpred.h"   /* full bpred_dir_t, md_addr_t, etc.   */
#include "tage_core.h"  /* tage_fold_history, compute_index …   */

#include <string.h>        /* memset                               */

/* ── Tunables ────────────────────────────────────────────────────────────── */

/* How often to reset usefulness bits (power of two branches). */
#define TAGE_RESET_PERIOD  (1u << 18)

/*
 * Index of the "untagged" component (T1).  Its tags[] pointer is NULL;
 * it is indexed purely by folded history and PC.
 */
#define TAGE_UNTAGGED_IDX  0

/* ── Per-lookup state ────────────────────────────────────────────────────── */
/*
 * Stored between bpred_dir_lookup_tage() and bpred_dir_update_tage() so
 * that the update knows which component provided the prediction and which
 * component was the alternate provider.
 *
 * Thread-safety: not required – SimpleSim is single-threaded.
 */
typedef struct {
    int prov_table;  /* provider table index (0-3); -1 = no TAGE prediction */
    int prov_idx;    /* provider entry index within its table                */
    int alt_table;   /* altprov table index (0-3); -1 = T0 bimodal          */
    int alt_idx;     /* altprov entry index                                  */
    int alt_pred;    /* altprov prediction: 0=NT, 1=T, -1=unknown           */
} tage_state_t;

static tage_state_t g_state;

/**
 * @brief Probe the TAGE predictor and return a pointer to the provider's counter.
 *
 * Searches tagged components T4→T2 (longest history first) for a tag match.
 * Falls back to T1 (untagged) if no tagged component matches. Records provider
 * and alternate provider in g_state for use by bpred_dir_update_tage().
 *
 * @param pred_dir  TAGE direction predictor instance (config.tage must be initialized).
 * @param baddr     Branch PC being looked up.
 * @return char*    Pointer to the provider's 3-bit counter (range 0-7, taken if >= 4).
 *                  Never NULL — always falls back to at least T1.
 */
char *
bpred_dir_lookup_tage(struct bpred_dir_t *pred_dir, md_addr_t baddr)
{
    int      i;
    int      n_tagged = pred_dir->config.tage.num_tables - 1; /* 4 */
    int      tbl_size = pred_dir->config.tage.tagged_table_size;
    uint64_t ghr      = pred_dir->config.tage.global_history;
    uint32_t pc       = (uint32_t)baddr;

    /* Initialise state: assume no tagged match (will use T1 untagged) */
    g_state.prov_table = -1;
    g_state.prov_idx   = -1;
    g_state.alt_table  = -1;
    g_state.alt_idx    = -1;
    g_state.alt_pred   = -1;

    /*
     * Search tagged components T4 → T2 (longest history first).
     * T1 (index 0) has no tags and is treated separately below.
     */
    for (i = n_tagged - 1; i >= 1; i--) {
        int           hist_len = pred_dir->config.tage.hist_lengths[i];
        int           idx      = tage_compute_index(pc, ghr, hist_len, tbl_size);
        unsigned char tag      = tage_compute_tag(pc, ghr, hist_len,
                                                  TAGE_TAG_WIDTH);

        if (pred_dir->config.tage.tags[i] == NULL)
            continue; /* safety: skip if not allocated */

        if (pred_dir->config.tage.tags[i][idx] != tag)
            continue; /* tag miss */

        /* Tag hit */
        if (g_state.prov_table == -1) {
            /* First (longest-history) match → provider */
            g_state.prov_table = i;
            g_state.prov_idx   = idx;
        } else {
            /* Second match → alternate provider */
            // Alt provider is describe in original paper to
            // help with newly-allocated entries with longer history
            // but doesn't have enough history yet to make an accurate prediction
            g_state.alt_table = i;
            g_state.alt_idx   = idx;
            g_state.alt_pred  = tage_ctr_pred(
                pred_dir->config.tage.counters[i][idx]);
            break; /* no need to look further */
        }
    }

    /*
     * Resolve altprovider when provider is in a tagged table but no second
     * tagged match was found: fall back to T1 (untagged) as altprovider.
     */
    if (g_state.prov_table >= 1 && g_state.alt_table == -1) {
        int hist0 = pred_dir->config.tage.hist_lengths[TAGE_UNTAGGED_IDX];
        int idx0  = tage_compute_index(pc, ghr, hist0, tbl_size);
        g_state.alt_table = TAGE_UNTAGGED_IDX;
        g_state.alt_idx   = idx0;
        g_state.alt_pred  = tage_ctr_pred(
            pred_dir->config.tage.counters[TAGE_UNTAGGED_IDX][idx0]);
    }

    /*
     * If no tagged component matched, use T1 (untagged) as provider.
     * T1 is always available (indexed by 8-bit folded history, no tag check).
     * The T0 bimodal (managed by bpred.c) acts as the implicit altprovider
     * and is represented here as alt_table = -1.
     */
    if (g_state.prov_table == -1) {
        int hist0 = pred_dir->config.tage.hist_lengths[TAGE_UNTAGGED_IDX];
        int idx0  = tage_compute_index(pc, ghr, hist0, tbl_size);
        g_state.prov_table = TAGE_UNTAGGED_IDX;
        g_state.prov_idx   = idx0;
        /* alt_table remains -1 (bimodal handled externally) */
    }

    return (char *)&pred_dir->config.tage.counters
                              [g_state.prov_table][g_state.prov_idx];
}

/* ── bpred_dir_update_tage ─────────────────────────────────────────────── */
int
bpred_dir_update_tage(struct bpred_dir_t *pred_dir,
                      md_addr_t           baddr,
                      md_addr_t           btarget,
                      int                 taken,
                      int                 pred_taken,
                      char               *dir_update_ptr)
{
    int      i;
    int      n_tagged  = pred_dir->config.tage.num_tables - 1; /* 4 */
    int      tbl_size  = pred_dir->config.tage.tagged_table_size;
    uint64_t ghr       = pred_dir->config.tage.global_history;
    uint32_t pc        = (uint32_t)baddr;

    int prov_table = g_state.prov_table;
    int prov_idx   = g_state.prov_idx;
    int alt_pred   = g_state.alt_pred;

    (void)btarget;       /* direction predictor does not use branch target */
    (void)dir_update_ptr; /* we recover the counter from g_state directly  */

    /* ── 1. Update provider counter ────────────────────────────────────── */
    if (prov_table >= 0 && prov_idx >= 0)
        tage_ctr_update(&pred_dir->config.tage.counters[prov_table][prov_idx],
                        taken);

    /* Provider's prediction (what was actually predicted) */
    int prov_pred = !!pred_taken;

    /* ── 2. Update usefulness bit of provider ───────────────────────────── */
    /*
     * Only update for properly-tagged components (indices 1-3).
     * Usefulness is incremented when P is correct and AltP is wrong;
     * decremented when P is wrong and AltP is correct.
     */
    if (prov_table >= 1 &&
        prov_idx   >= 0 &&
        pred_dir->config.tage.usefulness[prov_table] != NULL &&
        alt_pred != -1 &&
        alt_pred != prov_pred)          /* provider and altprov disagreed */
    {
        if (prov_pred == !!taken)
            tage_u_inc(&pred_dir->config.tage.usefulness[prov_table][prov_idx]);
        else
            tage_u_dec(&pred_dir->config.tage.usefulness[prov_table][prov_idx]);
    }

    /* ── 3. Allocate on misprediction ───────────────────────────────────── */
    if (prov_pred != !!taken) {
        /*
         * Determine the first candidate table: we can only allocate in a
         * table with *longer* history than the current provider.
         *   - If provider is T1 (idx 0, untagged): candidates are 1, 2, 3.
         *   - Otherwise: candidates are prov_table+1 … n_tagged-1.
         */
        int start = (prov_table == TAGE_UNTAGGED_IDX) ? 1 : prov_table + 1;

        if (start < n_tagged) {
            /* Look for the shortest-history candidate whose entry is free (u=0) */
            int alloc_table = -1;

            for (i = start; i < n_tagged; i++) {
                if (pred_dir->config.tage.tags[i]        == NULL) continue;
                if (pred_dir->config.tage.usefulness[i]  == NULL) continue;

                int hist_len  = pred_dir->config.tage.hist_lengths[i];
                int cand_idx  = tage_compute_index(pc, ghr, hist_len, tbl_size);

                if (pred_dir->config.tage.usefulness[i][cand_idx] == 0) {
                    alloc_table = i;
                    break; /* take the shortest-history free slot */
                }
            }

            if (alloc_table >= 0) {
                /* Allocate: write counter, tag, and clear usefulness */
                int           hist_len = pred_dir->config.tage.hist_lengths[alloc_table];
                int           new_idx  = tage_compute_index(pc, ghr,
                                                            hist_len, tbl_size);
                unsigned char new_tag  = tage_compute_tag(pc, ghr,
                                                          hist_len, TAGE_TAG_WIDTH);

                pred_dir->config.tage.counters[alloc_table][new_idx] =
                    taken ? TAGE_CTR_WEAK_TAKEN : TAGE_CTR_WEAK_NOTTAKEN;
                pred_dir->config.tage.tags[alloc_table][new_idx]        = new_tag;
                pred_dir->config.tage.usefulness[alloc_table][new_idx]  = 0;
            } else {
                /*
                 * No free slot found: decrement u of all candidate entries.
                 * This ages out useful bits so that future allocations succeed.
                 */
                for (i = start; i < n_tagged; i++) {
                    if (pred_dir->config.tage.tags[i]       == NULL) continue;
                    if (pred_dir->config.tage.usefulness[i] == NULL) continue;

                    int hist_len = pred_dir->config.tage.hist_lengths[i];
                    int cand_idx = tage_compute_index(pc, ghr, hist_len, tbl_size);
                    tage_u_dec(&pred_dir->config.tage.usefulness[i][cand_idx]);
                }
            }
        }
    }

    /* ── 4. Shift actual outcome into GHR ───────────────────────────────── */
    {
        uint64_t new_bit = (uint64_t)(!!taken);
        uint64_t mask    = (TAGE_HISTORY_REG_SIZE < 64)
                         ? ((1ULL << TAGE_HISTORY_REG_SIZE) - 1ULL)
                         : UINT64_MAX;
        pred_dir->config.tage.global_history = ((ghr << 1) | new_bit) & mask;
    }

    /* ── 5. Periodic usefulness reset ───────────────────────────────────── */
    {
        static unsigned int s_branch_count = 0;
        if (++s_branch_count == TAGE_RESET_PERIOD) {
            s_branch_count = 0;
            for (i = 1; i < n_tagged; i++) {
                if (pred_dir->config.tage.usefulness[i] != NULL)
                    memset(pred_dir->config.tage.usefulness[i], 0,
                           (size_t)tbl_size * sizeof(unsigned char));
            }
        }
    }

    return 0;
}
