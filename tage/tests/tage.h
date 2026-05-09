#ifndef TAGE_H
#define TAGE_H

#define NUM_TAGE_TABLES 5
#define TAGE_BASE_TABLE_SIZE 4096
#define TAGE_TAGGED_TABLE_SIZE 1024
#define TAGE_TAG_WIDTH 8
#define TAGE_GEOMETRIC_FACTOR 2
#define TAGE_HISTORY_REG_SIZE 64

#include <stdlib.h>
#include "../../machine.h"

struct bpred_dir_t;

char * bpred_dir_lookup_tage(struct bpred_dir_t *pred_dir, md_addr_t baddr); // returns pointer to prediction counter for TAGE predictor

int bpred_dir_update_tage(struct bpred_dir_t *pred_dir, 
                          md_addr_t baddr, 
                          md_addr_t btarget,
                          int taken,
                          int pred_taken,
                          char *dir_update_ptr); // updates TAGE predictor state based on actual outcome and prediction


#endif /* TAGE_H */
