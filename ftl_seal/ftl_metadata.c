#include "ftl_metadata.h"

//----------------------------------
// FTL metadata (maintain in SRAM)
//----------------------------------
//misc_metadata g_misc_meta[NUM_BANKS];
//ftl_statistics g_ftl_statistics[NUM_BANKS];
// volatile metadata
//UINT32 g_bad_blk_count[NUM_BANKS];
UINT32 g_bsp_isr_flag[NUM_BANKS];

//BOOL32 g_gc_flag[NUM_BANKS];
UINT8 g_mem_to_clr[PAGES_PER_BLK / 8];
UINT8 g_mem_to_set[PAGES_PER_BLK / 8];

// SATA read/write buffer pointer id
UINT32 g_ftl_read_buf_id;
UINT32 g_ftl_write_buf_id;

UINT32 chunkPtr[NUM_BANKS];
logBufMetaT logBufMeta[NUM_BANKS];

#if Overwrite
UINT32 owChunkPtr[NUM_BANKS];
logBufMetaT owLogBufMeta[NUM_BANKS];
OWCtrlBlock OWCtrl[NUM_BANKS];
heapData heapDataOverwrite;
#endif


heapData heapDataWrite;

listData cleanListDataWrite;

UINT32 userSecWrites = 0;
UINT32 totSecWrites = 0;

SWCtrlBlock SWCtrl[NUM_BANKS];
RWCtrlBlock RWCtrl[NUM_BANKS];

UINT32 free_list_head[NUM_BANKS];
UINT32 free_list_tail[NUM_BANKS];


UINT8 gcNestLevel[NUM_BANKS];
