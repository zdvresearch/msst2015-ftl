#ifndef BLK_MANAGEMENT_H
#define BLK_MANAGEMENT_H
#include "jasmine.h"

UINT32 get_free_vbn (UINT32 const bank);
void ret_free_vbn (UINT32 const bank, UINT32 const vblock);
#endif
