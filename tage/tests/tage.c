
#include "tage.h"
#include "../../bpred.h"

/* TAGE lookup: returns pointer to prediction counter */
char *
bpred_dir_lookup_tage(struct bpred_dir_t *pred_dir, md_addr_t baddr)
{

unsigned char *p = NULL;

#ifdef TAGE_BIMOD
  return NULL; // TAGE predictor  falls through to bimodal table
#endif

#ifdef TAGE_TAKEN
  pred_dir->config.tage.counters[0][0] = 7; // predict taken with high confidence always
  p = pred_dir->config.tage.counters[0];
#endif

#ifdef TAGE_NOT_TAKEN
  pred_dir->config.tage.counters[0][0] = 0; // predict not taken with high confidence always
  p = pred_dir->config.tage.counters[0];
#endif

  return p;
}

/* TAGE update: updates predictor state and returns whether prediction was correct */
int
bpred_dir_update_tage(struct bpred_dir_t *pred_dir, md_addr_t baddr, 
                      md_addr_t btarget, int taken, int pred_taken, 
                      char *dir_update_ptr)
{

  //stuff
  return 0;
}

