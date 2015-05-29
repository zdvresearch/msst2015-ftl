#ifndef FTL_METADATA_H
#define FTL_METADATA_H
#include "jasmine.h"
#include "ftl_parameters.h"

//----------------------------------
// macro
//----------------------------------
#define SW_LOG_LBN          0
#define MISCBLK_VBN         0x1    // vblock #1 <- misc metadata
#define META_BLKS_PER_BANK  (1 + 1 + MAP_BLK_PER_BANK)    // include block #0, misc, map block

typedef struct SWCtrlBlock
{
    UINT32 logLpn;
    UINT32 nextDataLpn;
    UINT32 lpnsListPtr;
} SWCtrlBlock;

typedef struct RWCtrlBlock
{
    UINT32 logLpn;
    UINT32 lpnsListPtr;
    UINT8 isOwBlk;
} RWCtrlBlock;

typedef struct OWCtrlBlock
{
    UINT32 logLpn;
    UINT32 lpnsListPtr;
} OWCtrlBlock;

typedef struct heapData
{
    //UINT32 validChunksHeapPositions[NUM_BANKS][LOG_BLK_PER_BANK];
    UINT32 positionsPtr;
    UINT32 nElInHeap[NUM_BANKS];
    UINT32 dramStartAddr;
    UINT32 logBlksPerBank;
    UINT32 firstLbn;
} heapData;

typedef struct listData
{
    logListNode* cleanListHead[NUM_BANKS];
    logListNode* cleanListTail[NUM_BANKS];
    logListNode* cleanListUnusedNodes[NUM_BANKS];
    UINT32 size[NUM_BANKS];
} listData;

typedef struct logBufMetaT
{
    UINT32 dataLpn[CHUNKS_PER_PAGE];
    UINT32 chunkIdx[CHUNKS_PER_PAGE];
} logBufMetaT;

typedef struct cleanQueueElementT
{
    UINT32 dataLpn;
    struct cleanQueueElementT* next;
} cleanQueueElementT;
//----------------------------------
// FTL metadata (maintain in SRAM)
//----------------------------------
extern UINT32 g_bsp_isr_flag[NUM_BANKS];
extern UINT8 g_mem_to_clr[PAGES_PER_BLK / 8];
extern UINT8 g_mem_to_set[PAGES_PER_BLK / 8];
extern UINT32 g_ftl_read_buf_id;
extern UINT32 g_ftl_write_buf_id;
extern logBufMetaT logBufMeta[NUM_BANKS];
#if Overwrite
extern logBufMetaT owLogBufMeta[NUM_BANKS];
extern UINT32 owChunkPtr[NUM_BANKS];
extern OWCtrlBlock OWCtrl[NUM_BANKS];
extern heapData heapDataOverwrite;
#endif
extern UINT32 chunkPtr[NUM_BANKS];
extern heapData heapDataWrite;
extern listData cleanListDataWrite;
extern UINT32 userSecWrites;
extern UINT32 totSecWrites;
extern SWCtrlBlock SWCtrl[NUM_BANKS];
extern RWCtrlBlock RWCtrl[NUM_BANKS];
extern UINT32 free_list_head[NUM_BANKS];
extern UINT32 free_list_tail[NUM_BANKS];
extern UINT8 gcNestLevel[NUM_BANKS];

#if Overwrite
#define SizeOwMetadata      ((sizeof(logBufMetaT) * NUM_BANKS)      + \
                             (sizeof(UINT32) * NUM_BANKS)           + \
                             (sizeof(OWCtrlBlock) * NUM_BANKS)      + \
                             (sizeof(heapData))                       \
                            )
#else
#define SizeOwMetadata      0
#endif

#define SizeSRAMMetadata    ((sizeof(UINT32) * NUM_BANKS)            + \
                             (sizeof(UINT8) * (PAGES_PER_BLK/8))    + \
                             (sizeof(UINT8) * (PAGES_PER_BLK/8))    + \
                             (sizeof(UINT32))                       + \
                             (sizeof(UINT32))                       + \
                             (sizeof(logBufMetaT) * NUM_BANKS)      + \
                             (sizeof(UINT32) * NUM_BANKS)           + \
                             (sizeof(heapData))                     + \
                             (sizeof(listData))                     + \
                             (sizeof(UINT32))                       + \
                             (sizeof(UINT32))                       + \
                             (sizeof(SWCtrlBlock) * NUM_BANKS)      + \
                             (sizeof(RWCtrlBlock) * NUM_BANKS)      + \
                             (sizeof(UINT32) * NUM_BANKS)           + \
                             (sizeof(UINT32) * NUM_BANKS)           + \
                             (sizeof(UINT8) * NUM_BANKS)            + \
                             (SizeOwMetadata)                         \
                            )
//----------------------------------
// General Purpose Macros
//----------------------------------
#define DataPageToDataBlk(dataLpn)      (((dataLpn) / NUM_BANKS) / PAGES_PER_BLK)
#define DataPageToOffset(dataLpn)       (((dataLpn) / NUM_BANKS) % PAGES_PER_BLK)
#define PageToBank(lpn)                 ((lpn) % NUM_BANKS)
#define VPageToOffset(vpn)              ((vpn) % PAGES_PER_BLK)
#define VPageToVBlk(vpn)                ((vpn) / PAGES_PER_BLK)
#define LogPageToOffset(logLpn)         ((logLpn) % PAGES_PER_BLK)
#define LogPageToLogBlk(logLpn)         ((logLpn) / PAGES_PER_BLK)

#endif
