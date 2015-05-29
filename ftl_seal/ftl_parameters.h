#ifndef FTL_PARAMETERS_H
#define FTL_PARAMETERS_H
#include "jasmine.h"

#define UsedPagesPerLogBlk      (PAGES_PER_BLK - 1)
#define UsedPagesPerOwLogBlk    (PAGES_PER_BLK/2-1)
//#define OwCountersPerBlk        (PAGES_PER_VBLK/2) // Even though there are only PAGES_PER_VBLK/2-1 low pages usable, we keep PAGES_PER_VBLK/2 counters to have accesses aligned to 128 byte boundary required by mem_set_dram
#define OwCountersPerBlk        (PAGES_PER_VBLK) // Even though there are only PAGES_PER_VBLK/2-1 low pages usable, we keep PAGES_PER_VBLK/2 counters to have accesses aligned to 128 byte boundary required by mem_set_dram
#define LastPageInOwBlk         (PAGES_PER_BLK-5)
#define SECTORS_PER_OW_VBLK     (SECTORS_PER_PAGE * UsedPagesPerOwLogBlk)

//------------------------------------------
// Parameters for 4KB Log chunking
//------------------------------------------
#define CHUNKS_PER_PAGE         8  // 4KB chunks
#define CHUNKS_PER_BLK          (CHUNKS_PER_PAGE * PAGES_PER_BLK)
#define CHUNKS_PER_LOG_BLK      (UsedPagesPerLogBlk * CHUNKS_PER_PAGE)
#define CHUNKS_PER_OW_LOG_BLK   (UsedPagesPerOwLogBlk * CHUNKS_PER_PAGE)
#define CHUNK_ADDR_BYTES        (sizeof(UINT32))
#define SECTORS_PER_CHUNK       (SECTORS_PER_PAGE / CHUNKS_PER_PAGE)
#define BYTES_PER_CHUNK         (BYTES_PER_PAGE / CHUNKS_PER_PAGE)


//-------------------------------
// Free Blocks
//-------------------------------
#define FREE_BLK_PER_BANK   2
#define NUM_FREE_BLK        (FREE_BLK_PER_BANK * NUM_BANKS)
#define FREE_BMT_BYTES      ((NUM_FREE_BLK * sizeof(UINT16) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)
//-------------------------------
// Map Blocks
//-------------------------------
#define MAP_BLK_PER_BANK    3
#define NUM_MAP_BLK         (MAP_BLK_PER_BANK * NUM_BANKS)
//-------------------------------
// Log Blocks
//-------------------------------
#if Overwrite
    #define NUM_DATA_BLK    ( ((NUM_LPAGES + PAGES_PER_BLK - 1) / PAGES_PER_BLK)/2 )
#else
    #define NUM_DATA_BLK    ( (NUM_LPAGES + PAGES_PER_BLK - 1) / PAGES_PER_BLK )
#endif
#define DATA_BLK_PER_BANK   ( (NUM_DATA_BLK + NUM_BANKS - 1) / NUM_BANKS )

// ATTENTION!!!
// The number of total spare blocks should never be smaller than 2 + # of data block / pages per block rounded up
// That's because 2 blocks are required for current RW and SW blocks, and the rest is required because one page per block is used to store the lpns, therefore some extra space must be allocated
#define SW_LOG_BLK_PER_BANK     (1)
#define SPARE_LOG_BLK_PER_BANK  (DATA_BLK_PER_BANK/8 + SW_LOG_BLK_PER_BANK)
// Minimum requirements:
// (DATA_BLK_PER_BANK/2 / PAGES_PER_BLK)+1 blocks to host the extra pages occupied by list of lpns
// 1 block for overwrites because currently if there is a block allocated for overwrites, but not completed yet, the FTL is not capable of preempting it
// 1 block for minimum spare space
#if SPARE_LOG_BLK_PER_BANK < (2 + (DATA_BLK_PER_BANK)/PAGES_PER_BLK + 1)
    #undef SPARE_LOG_BLK_PER_BANK
    #define SPARE_LOG_BLK_PER_BANK (2 + (DATA_BLK_PER_BANK)/PAGES_PER_BLK + 1)
#endif

#define LOG_BLK_PER_BANK            ((DATA_BLK_PER_BANK + SPARE_LOG_BLK_PER_BANK))

#if LOG_BLK_PER_BANK > VBLKS_PER_BANK
    #define LOG_BLK_PER_BANK            VBLKS_PER_BANK
#endif

//#if (LOG_BLK_PER_BANK < HASHED_LOG_BLK_PER_BANK)
//#define LOG_BLK_PER_BANK HASHED_LOG_BLK_PER_BANK
//#endif

//#define NUM_LOG_BLK         (LOG_BLK_PER_BANK * NUM_BANKS)
#define LOG_BMT_BYTES       ((NUM_BANKS * LOG_BLK_PER_BANK * sizeof(UINT16) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)
//-------------------------------------
// VC, SC and Flip Bitmaps
//-------------------------------------
#define VC_BITMAP_BYTES    (((NUM_BANKS * DATA_BLK_PER_BANK * PAGES_PER_BLK / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)
#define C_BITMAP_BYTES    (((NUM_BANKS * DATA_BLK_PER_BANK * PAGES_PER_BLK / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)
#if OPTION_OUT_OF_ORDER_WRITES
#define FlipBytesPerBlk        ((PAGES_PER_BLK)/2/8)
#define FLIP_BITMAP_BYTES (((NUM_BANKS * DATA_BLK_PER_BANK * FlipBytesPerBlk) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)
#endif
//#define VC_BITMAP_BYTES    (((NUM_BANKS * DATA_BLK_PER_BANK * PAGES_PER_BLK / 8) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)
//#define SC_BITMAP_BYTES (((NUM_BANKS * LOG_BLK_PER_BANK * PAGES_PER_BLK / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)
//#define FLIP_BITMAP_BYTES (((NUM_BANKS * DATA_BLK_PER_BANK * 64 / 8) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

//#define FTL_BMT_BYTES       ((DATA_BMT_BYTES + LOG_BMT_BYTES + ISOL_BMT_BYTES + FREE_BMT_BYTES + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)
#define FTL_BMT_BYTES       ((DATA_BMT_BYTES + LOG_BMT_BYTES + FREE_BMT_BYTES + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

//-------------------------------------
// DRAM buffers
//-------------------------------------
#define NUM_COPY_BUFFERS        NUM_BANKS_MAX
#define NUM_FTL_BUFFERS         NUM_BANKS
#define NUM_GC_BUFFERS          (2*NUM_BANKS)
#define NUM_HIL_BUFFERS         1
#define NUM_TEMP_BUFFERS        1
#define NUM_LOG_BUFFERS         NUM_BANKS
#define NUM_OW_LOG_BUFFERS      NUM_BANKS

#define COPY_BUF_BYTES                      (NUM_COPY_BUFFERS * BYTES_PER_PAGE)                                                                           // 1 MB
#define FTL_BUF_BYTES                       (NUM_FTL_BUFFERS * BYTES_PER_PAGE)                                                                             // 1 MB
#define GC_BUF_BYTES                        (NUM_GC_BUFFERS * BYTES_PER_PAGE)                                                                               // 2 MB
#define HIL_BUF_BYTES                       (NUM_HIL_BUFFERS * BYTES_PER_PAGE)                                                                             // 32 KB
#define TEMP_BUF_BYTES                      (NUM_TEMP_BUFFERS * BYTES_PER_PAGE)                                                                           // 32 KB
#define LOG_BUF_BYTES                       (NUM_LOG_BUFFERS * BYTES_PER_PAGE)                                                                             // 1 MB
#define LPNS_IN_LOG_BYTES                   ((NUM_BANKS * CHUNKS_PER_BLK * CHUNK_ADDR_BYTES + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)          // 132KB
#define VICTIM_LPN_LIST_BYTES               (2* NUM_BANKS * BYTES_PER_PAGE)                                                                                // 2 MB
#define BAD_BLK_BMP_BYTES                   (((NUM_VBLKS / 8) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)
#define HEAP_VALID_CHUNKS_BYTES             ((NUM_BANKS * LOG_BLK_PER_BANK * sizeof(heapEl) + DRAM_ECC_UNIT -1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)         // 2,560 B
#define HEAP_VALID_CHUNKS_POSITIONS_BYTES   ((NUM_BANKS * LOG_BLK_PER_BANK * sizeof(UINT32) + DRAM_ECC_UNIT - 1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)   // 8 KB
#define CLEAN_LIST_NODES_BYTES              ((NUM_BANKS * LOG_BLK_PER_BANK * sizeof(logListNode) + DRAM_ECC_UNIT -1) / DRAM_ECC_UNIT * DRAM_ECC_UNIT)     // 2,560 B
#if Overwrite
#define OW_COUNT_BYTES                      ( ( ( NUM_BANKS * LOG_BLK_PER_BANK * OwCountersPerBlk * sizeof(UINT8) ) + DRAM_ECC_UNIT - 1 ) / DRAM_ECC_UNIT * DRAM_ECC_UNIT )
#else
#define OW_COUNT_BYTES                      0
#endif

#define CHUNKS_MAP_TABLE_BYTES              (NUM_BANKS * DATA_BLK_PER_BANK * PAGES_PER_VBLK * CHUNKS_PER_PAGE * sizeof(UINT32))

#define DRAM_BYTES_OTHER    (COPY_BUF_BYTES + \
                            FTL_BUF_BYTES + \
                            GC_BUF_BYTES + \
                            HIL_BUF_BYTES + \
                            TEMP_BUF_BYTES + \
                            LOG_BUF_BYTES + \
                            LOG_BUF_BYTES + \
                            LPNS_IN_LOG_BYTES + \
                            LPNS_IN_LOG_BYTES + \
                            LPNS_IN_LOG_BYTES + \
                            VICTIM_LPN_LIST_BYTES + \
                            BAD_BLK_BMP_BYTES + \
                            LOG_BMT_BYTES + \
                            FREE_BMT_BYTES + \
                            CHUNKS_MAP_TABLE_BYTES + \
                            HEAP_VALID_CHUNKS_BYTES + \
                            HEAP_VALID_CHUNKS_POSITIONS_BYTES + \
                            HEAP_VALID_CHUNKS_BYTES + \
                            HEAP_VALID_CHUNKS_POSITIONS_BYTES + \
                            CLEAN_LIST_NODES_BYTES + \
                            OW_COUNT_BYTES)

#define LOG_METADATA_BYTES      ((NUM_FTL_BUFFERS + NUM_GC_BUFFERS + NUM_LOG_BUFFERS + NUM_OW_LOG_BUFFERS) * BYTES_PER_PAGE)
#define HASH_METADATA_BYTES     (HASH_BUCKET_BYTES + HASH_NODE_BYTES)
#define NUM_RW_BUFFERS          ((DRAM_SIZE - DRAM_BYTES_OTHER) / BYTES_PER_PAGE - 1)
#define NUM_RD_BUFFERS          (((NUM_RW_BUFFERS / 2) + NUM_BANKS - 1) / NUM_BANKS * NUM_BANKS)
#define NUM_WR_BUFFERS          (NUM_RW_BUFFERS - NUM_RD_BUFFERS)

#define RD_BUF_BYTES        (NUM_RD_BUFFERS * BYTES_PER_PAGE)
#define WR_BUF_BYTES        (NUM_WR_BUFFERS * BYTES_PER_PAGE)


//------------------------------
// 1. address mapping information
//------------------------------
// map block mapping table
// NOTE:
//   vbn #0 : super block
// misc blk
//   vbn #1: maintain misc. DRAM metadata
// map blk
//   vbn #2: maintain data/log/isol/free BMT
//   vbn #3: maintain log page mapping hash table
//   vbn #4: bitmap info, block age



// log page mapping table (linear array structure) - NOT USED
//
// NOTE;
//  - Basically, whenever we need to find the position(i.e., log LPN) of the target LPN(i.e., data LPN) in log blocks,
//    we linearly search the log page mapping table. (e.g., new data write op., read op., merge op., etc.)
//    However, if the storage's capacity is getting larger, it might take a long time even we use Memory utility H/W engine (max. 180us to search 32KB range).
//    Thus, Alternatively we adopt the static hash structure for log page mapping table although the memory requirement is quite increased (almost 3 times).
//
// #define LOG_PMT_ADDR         (FREE_BMT_ADDR + FREE_BMT_BYTES)
// #define LOG_PMT_BYTES        ((((LOG_BLK_PER_BANK + ISOL_BLK_PER_BANK) * PAGES_PER_BLK * NUM_BANKS * sizeof(UINT32)) + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR * BYTES_PER_SECTOR)

// log page mapping table (static hash structure)

//--------------------------------------
// 2. additional FTL metadata
//--------------------------------------
// validation check bitmap table
//
// NOTE;
//   - To figure out that the valid page of target LPN is in log blocks or not , we just check this bit information.
//     If the bit of target LPN is set, we can obviously know the up-to-date data is existed in log blocks despite not acessing log page mapping table.

//------------------------------------------
// Free Chunks Heap
//------------------------------------------
typedef struct heapEl
{
    UINT16 value;
    UINT16 lbn;
}heapEl;

//------------------------------------------
// Log List
//------------------------------------------
typedef struct logListNode
{
    UINT32 lbn;
    struct logListNode* next;
}logListNode;

//------------------------------------------
// Log Address Map
//------------------------------------------
// |----LOG----|LogBufLpn|OwLogBuf|~~~~~~|---OW LOG---|
#define StartLogLpn    0
#define LogBufLpn    ((LOG_BLK_PER_BANK)*(PAGES_PER_BLK)-1)
#define OwLogBufLpn    (LogBufLpn + 1)
#define StartOwLogLpn (1 << 31)
//#define PagesInLogBuf    1
//#define PagesInOwLogBuf    1
//#define GcBufLpn1    (LogBufLpn+PagesInLogBuf)
//#define GcBufLpn2    (GcBufLpn1+1)
//#define PagesInGcBuf    2
//#define DirtyLpn    (GcBufLpn1+PageInGcBuf)
//#define DataChunkAddr    (INVALID-1)
//
#if Overwrite
typedef enum {Invalid, FlashWLog, DRAMWLog, DRAMOwLog, FlashOwLog} chunkLocation;
#else
typedef enum {Invalid, FlashWLog, DRAMWLog} chunkLocation;
#endif

#define ChunkToBank(chunk)                  ((chunk) / (LOG_BLK_PER_BANK * CHUNKS_PER_BLK))
#define ChunkToChunkOffsetInBank(chunk)     ((chunk) % (LOG_BLK_PER_BANK * CHUNKS_PER_BLK))
#define ChunkToLbn(chunk)                   (ChunkToChunkOffsetInBank(chunk) / CHUNKS_PER_PAGE / PAGES_PER_BLK)
#define ChunkToLpn(chunk)                   (ChunkToChunkOffsetInBank(chunk) / CHUNKS_PER_PAGE)
#define ChunkToPageOffset(chunk)            ((ChunkToChunkOffsetInBank(chunk) / CHUNKS_PER_PAGE) % PAGES_PER_BLK)
#define ChunkToSectOffset(chunk)            ((ChunkToChunkOffsetInBank(chunk) % CHUNKS_PER_PAGE) * SECTORS_PER_CHUNK)
#define ChunkToChunkOffset(chunk)           ((chunk) % CHUNKS_PER_PAGE)


#define GcMode      0
#define ReadMode    1

#if Overwrite
#define OwLimit     8
#endif

//--------------------------------------
// Overwrite bit management in address
//--------------------------------------
#if Overwrite
    #define DataBlksPerBankSeenByUser      (DATA_BLK_PER_BANK * 2)
    #if NUM_BANKS * DataBlksPerBankSeenByUser > 8192
        #define overwriteBitPosition     26 // > 32GB => > 64M sectors => 26th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 4096
        #define overwriteBitPosition     25 // > 16GB => > 32M sectors => 25th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 2048
        #define overwriteBitPosition     24 // > 8GB => > 16M sectors => 24th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 1024
        #define overwriteBitPosition     23 // > 4GB => > 8M sectors => 23th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 512
        #define overwriteBitPosition     22 // > 2GB => > 4M sectors => 22th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 256
        #define overwriteBitPosition     21 // > 1GB => > 2M sectors => 21th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 128
        #define overwriteBitPosition     20 // > 512 MB = 1M sectors => 20th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 64
        #define overwriteBitPosition     19 // > 256 MB = 512K sectors => 19th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 32
        #define overwriteBitPosition     18 //  > 128 MB => > 256K sectors => 18th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 16
        #define overwriteBitPosition     17 //  > 64 MB => > 128K sectors => 17th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 8
        #define overwriteBitPosition     16 //  > 32 MB => > 64K sectors => 16th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 4
        #define overwriteBitPosition     15 //  > 16 MB => > 32K sectors => 14th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 2
        #define overwriteBitPosition     14 //  > 8 MB => > 16K sectors => 13th bit is the highest address bit
    #elif NUM_BANKS * DataBlksPerBankSeenByUser > 1
        #define overwriteBitPosition     13 //  > 4 MB => > 8K sectors => 12th bit is the highest address bit
    #else
        #define overwriteBitPosition     12
    #endif
#endif

#endif
