#ifndef TEST_PARAM_HH
#define TEST_PARAM_HH

#define N_BANKS             32
#define OPTION_2_PLANE      1
#define ADDR_BANKS          (N_BANKS)
#define SECT_SIZE           512
#define VBLK_PER_BANK       64
#define OW_VBLK_PER_BANK    0
#define PAGES_PER_VBLK      128
#define PAGES_PER_OW_VBLK   64
#define REAL_PAGES_PER_VBLK      127
#define REAL_PAGES_PER_OW_VBLK   63
#if OPTION_2_PLANE
    #define SECT_PER_PAGE       64
#else
    #define SECT_PER_PAGE       32
#endif
#define CHUNKS_PER_PAGE     8
#define SECT_PER_CHUNK      (SECT_PER_PAGE / CHUNKS_PER_PAGE)
#define SECT_PER_VBLK       (PAGES_PER_VBLK * SECT_PER_PAGE)
#define SECT_PER_BANK       (SECT_PER_VBLK * VBLK_PER_BANK)
#define PAGE_SIZE           (SECT_PER_PAGE * SECT_SIZE)
#define CHUNK_SIZE           (SECT_PER_CHUNK * SECT_SIZE)
#define VBLK_SIZE           (PAGES_PER_VBLK * PAGE_SIZE)
#define BANK_SIZE           (VBLK_PER_BANK * VBLK_SIZE)
#define CHUNKS_PER_BLK      (CHUNKS_PER_PAGE * PAGES_PER_VBLK)
#define CHUNKS_PER_BANK     (CHUNKS_PER_PAGE * PAGES_PER_VBLK * VBLK_PER_BANK)

// Current test parameters
#define BANKS_FOR_TEST 1
#define BLKS_PER_BANK_FOR_TEST 5
#define PAGES_PER_VBLK_FOR_TEST PAGES_PER_VBLK

#define HALF_BLK 1	//0: test all blocks. 1: test only first half of a block (64 pages)
#define WHOLE_PAGES 1  //0: test with random sect_cnt. 1: test with whole page writes
#define BANK_UNDER_TEST 0

#define NUM_BANKS N_BANKS
#define DATA_BLK_PER_BANK (VBLK_PER_BANK * 2)
//#define DATA_BLK_PER_BANK VBLK_PER_BANK
//
#if NUM_BANKS * DATA_BLK_PER_BANK > 8192
#define overwriteBitPosition 	26 // > 32GB => > 64M sectors => 26th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 4096
#define overwriteBitPosition 	25 // > 16GB => > 32M sectors => 25th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 2048
#define overwriteBitPosition 	24 // > 8GB => > 16M sectors => 24th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 1024
#define overwriteBitPosition 	23 // > 4GB => > 8M sectors => 23th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 512
#define overwriteBitPosition 	22 // > 2GB => > 4M sectors => 22th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 256
#define overwriteBitPosition 	21 // > 1GB => > 2M sectors => 21th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 128
#define overwriteBitPosition 	20 // > 512 MB = 1M sectors => 20th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 64
#define overwriteBitPosition 	19 // > 256 MB = 512K sectors => 19th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 32
#define overwriteBitPosition 	18 //  > 128 MB => > 256K sectors => 18th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 16
#define overwriteBitPosition 	17 //  > 64 MB => > 128K sectors => 17th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 8
#define overwriteBitPosition 	16 //  > 32 MB => > 64K sectors => 16th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 4
#define overwriteBitPosition 	15 //  > 16 MB => > 32K sectors => 14th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 2
#define overwriteBitPosition 	14 //  > 8 MB => > 16K sectors => 13th bit is the highest address bit
#elif NUM_BANKS * DATA_BLK_PER_BANK > 1
#define overwriteBitPosition 	13 //  > 4 MB => > 8K sectors => 12th bit is the highest address bit
#else
#define overwriteBitPosition 	12
#endif

#endif
