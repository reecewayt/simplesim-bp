
#include "tage.h"
#include "../../bpred.h"

/* TAGE lookup: returns pointer to prediction counter */
char *
bpred_dir_lookup_tage(struct bpred_dir_t *pred_dir, md_addr_t baddr)
{
//stuff 
  return NULL;
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

