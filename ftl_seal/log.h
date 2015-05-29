#ifndef LOG_H
#define LOG_H
#include "jasmine.h" // PAGES_PER_BLK
#include "ftl_parameters.h" // LOG_BLK_PER_BANK, ISOL_BLK_PER_BANK

#define get_log_lbn(log_lpn)                 ((log_lpn) / PAGES_PER_BLK)

// Public Functions
void set_log_vbn (UINT32 const bank, UINT32 const log_lbn, UINT32 const vblock);
UINT32 get_log_vbn (UINT32 const bank, UINT32 const log_lbn);
void initLog();
UINT32 getRWLpn(const UINT32 bank);
void increaseRWLpn(const UINT32 bank);
UINT32 getSWLpn(const UINT32 bank);
void increaseSWLpn(const UINT32 bank);
UINT32 getOWLpn(const UINT32 bank);
void increaseOWLpn(const UINT32 bank);
chunkLocation findChunkLocation(const UINT32 chunkAddr);
void sealOWBlk(const UINT32 bank);

#endif
