#ifndef DRAM_LAYOUT_H
#define DRAM_LAYOUT_H
#include "jasmine.h"
#include "ftl_parameters.h"

///////////////////////////////
// DRAM segmentation
///////////////////////////////

#define RD_BUF_ADDR                             DRAM_BASE

#define WR_BUF_ADDR                             (RD_BUF_ADDR + RD_BUF_BYTES)    // base address of SATA write buffers

#define COPY_BUF_ADDR                           (WR_BUF_ADDR + WR_BUF_BYTES)    // base address of flash copy buffers

#define FTL_BUF_ADDR                            (COPY_BUF_ADDR + COPY_BUF_BYTES)    // a buffer dedicated to FTL internal purpose

#define GC_BUF_ADDR                             (FTL_BUF_ADDR + FTL_BUF_BYTES)

#define HIL_BUF_ADDR                            (GC_BUF_ADDR + GC_BUF_BYTES)    // a buffer dedicated to HIL internal purpose

#define TEMP_BUF_ADDR                           (HIL_BUF_ADDR + HIL_BUF_BYTES)    // general purpose buffer

#define LOG_BUF_ADDR                            (TEMP_BUF_ADDR + TEMP_BUF_BYTES)

#define OW_LOG_BUF_ADDR                         (LOG_BUF_ADDR + LOG_BUF_BYTES)

#define LPNS_IN_LOG_1_ADDR                      (OW_LOG_BUF_ADDR + LOG_BUF_BYTES)

#define LPNS_IN_LOG_2_ADDR                      (LPNS_IN_LOG_1_ADDR + LPNS_IN_LOG_BYTES)

#define LPNS_IN_LOG_3_ADDR                      (LPNS_IN_LOG_2_ADDR + LPNS_IN_LOG_BYTES)

#define VICTIM_LPN_LIST_ADDR                    (LPNS_IN_LOG_3_ADDR + LPNS_IN_LOG_BYTES)

#define BAD_BLK_BMP_ADDR                        (VICTIM_LPN_LIST_ADDR + VICTIM_LPN_LIST_BYTES)    // bitmap of initial bad blocks

#define LOG_BMT_ADDR                            (BAD_BLK_BMP_ADDR + BAD_BLK_BMP_BYTES)

#define FREE_BMT_ADDR                           (LOG_BMT_ADDR + LOG_BMT_BYTES)

#define CHUNKS_MAP_TABLE_ADDR                   (FREE_BMT_ADDR + FREE_BMT_BYTES)

#define HEAP_VALID_CHUNKS_ADDR_WRITE            (CHUNKS_MAP_TABLE_ADDR + CHUNKS_MAP_TABLE_BYTES)

#define HEAP_VALID_CHUNKS_POSITIONS_WRITE       (HEAP_VALID_CHUNKS_ADDR_WRITE + HEAP_VALID_CHUNKS_BYTES)

#define HEAP_VALID_CHUNKS_ADDR_OVERWRITE        (HEAP_VALID_CHUNKS_POSITIONS_WRITE + HEAP_VALID_CHUNKS_POSITIONS_BYTES)

#define HEAP_VALID_CHUNKS_POSITIONS_OVERWRITE   (HEAP_VALID_CHUNKS_ADDR_OVERWRITE + HEAP_VALID_CHUNKS_BYTES)

#define CLEAN_LIST_NODES_ADDR                   (HEAP_VALID_CHUNKS_POSITIONS_OVERWRITE + HEAP_VALID_CHUNKS_POSITIONS_BYTES)

#define OW_COUNT_ADDR                           (CLEAN_LIST_NODES_ADDR + CLEAN_LIST_NODES_BYTES)

#define END_ADDR                                (OW_COUNT_ADDR + OW_COUNT_BYTES)

//////////////////////////
// Buffer access macros //
//////////////////////////
#define WR_BUF_PTR(BUF_ID)                              (WR_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define WR_BUF_ID(BUF_PTR)                              ((((UINT32)BUF_PTR) - WR_BUF_ADDR) / BYTES_PER_PAGE)
#define RD_BUF_PTR(BUF_ID)                              (RD_BUF_ADDR + ((UINT32)(BUF_ID)) * BYTES_PER_PAGE)
#define RD_BUF_ID(BUF_PTR)                              ((((UINT32)BUF_PTR) - RD_BUF_ADDR) / BYTES_PER_PAGE)
#define _COPY_BUF(RBANK)                                (COPY_BUF_ADDR + (RBANK) * BYTES_PER_PAGE)
#define COPY_BUF(BANK)                                  _COPY_BUF(REAL_BANK(BANK))
#define FTL_BUF(BANK)                                   (FTL_BUF_ADDR + ((BANK) * BYTES_PER_PAGE))
#define GC_BUF(BANK, NestLevel)                         (GC_BUF_ADDR + (((NestLevel)-1) * NUM_BANKS * BYTES_PER_PAGE) + ((BANK) * BYTES_PER_PAGE))
#define LOG_BUF(BANK)                                   (LOG_BUF_ADDR + ((BANK) * BYTES_PER_PAGE))
#define OW_LOG_BUF(BANK)                                (OW_LOG_BUF_ADDR + ((BANK) * BYTES_PER_PAGE))
#define LPNS_BUF_BASE_1(bank)                           (LPNS_IN_LOG_1_ADDR + ((bank)*CHUNKS_PER_BLK*CHUNK_ADDR_BYTES))
#define LPNS_BUF_BASE_2(bank)                           (LPNS_IN_LOG_2_ADDR + ((bank)*CHUNKS_PER_BLK*CHUNK_ADDR_BYTES))
#define LPNS_BUF_BASE_3(bank)                           (LPNS_IN_LOG_3_ADDR + ((bank)*CHUNKS_PER_BLK*CHUNK_ADDR_BYTES))
#define chunkInLpnsList(base, logPageOffset, chunk)     ((base) + ((logPageOffset)*CHUNKS_PER_PAGE*CHUNK_ADDR_BYTES) + ((chunk) * CHUNK_ADDR_BYTES))
#define VICTIM_LPN_LIST(bank, NestLevel)                (VICTIM_LPN_LIST_ADDR + (((NestLevel)-1) * NUM_BANKS * BYTES_PER_PAGE) + ((bank) * BYTES_PER_PAGE))
#define ValidChunksAddr(startAddr, bank, pos)           ((startAddr) + ((bank) * LOG_BLK_PER_BANK * sizeof(heapEl)) + (pos) * sizeof(heapEl))
#define CleanList(bank)                                 (CLEAN_LIST_NODES_ADDR + ((bank) * LOG_BLK_PER_BANK * sizeof(logListNode)))
#define HeapPositions(base, bank, blk)                  ( (base) + ( ( (bank) * LOG_BLK_PER_BANK + (blk) ) * sizeof(UINT32) ) )
#define ChunksMapTable(lpn, chunkIdx)                   (CHUNKS_MAP_TABLE_ADDR + (lpn) * CHUNKS_PER_PAGE * sizeof(UINT32) + (chunkIdx) * sizeof(UINT32))

#define OwCounter(bank, blk, page)                      ( OW_COUNT_ADDR + ( ( (bank) * LOG_BLK_PER_BANK + blk ) * OwCountersPerBlk + (page) ) * sizeof(UINT8) )
#define resetOwCounter(bank, blk)                       ( mem_set_dram(OW_COUNT_ADDR + ( ( (bank) * LOG_BLK_PER_BANK + blk) * OwCountersPerBlk ) * sizeof(UINT8), 0, OwCountersPerBlk * sizeof(UINT8) ) )

#endif
