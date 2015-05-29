#include "ftl_metadata.h"
#include "ftl_parameters.h"
#include "log.h"
#include "garbage_collection.h"
#include "heap.h" // decrementValidChunks

//#define ProgMergeFreeBlksThreshold (LOG_BLK_PER_BANK)
#if NO_FLASH_OPS
#define nand_page_ptread(a, b, c, d, e, f, g)
#define nand_page_read(a, b, c, d)
#define nand_page_program(a, b, c, d)
#define nand_page_ptprogram(a, b, c, d, e, f)
#define nand_page_ptprogram_from_host(a, b, c, d, e)     g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS; \
                            SETREG (BM_STACK_WRSET, g_ftl_write_buf_id); \
                            SETREG (BM_STACK_RESET, 0x01);
//#define nand_page_copyback(a, b, c, d, e)
//#define nand_block_erase(a, b)
#endif

//#define uart_print(x)
//#define uart_print_int(x)
#define callPM(x)

// Private methods
static void initRead(const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects, const UINT8 mode);
static UINT8 chunksInSameFlashPage(const UINT32 chunkA, const UINT32 chunkB);
static void chunkInvalid();
static void chunkInWDramBuf();
#if Overwrite
static void chunkInOWDramBuf();
#endif
static void chunkInFlashLog(UINT8 inOW);
static void setupFlashPageRead(UINT32 * chunksInPage, UINT32 * srcChunkByteOffsets, UINT32 * chunkIdxs, UINT8 *decreaseInOW);
static void readFlashPage(UINT32 *chunksInPage, UINT32 *srcChunkByteOffsets, UINT32 *chunkIdxs, UINT8 *decreaseInOW);
static void readCompletePage(UINT32 *chunksInPage, UINT32 *srcChunkByteOffsets, UINT32 *chunkIdxs, UINT8 *decreaseInOW);
static void readOneChunk(UINT32 *chunksInPage, UINT32 *srcChunkByteOffsets, UINT32 *chunkIdxs, UINT8 *decreaseInOW);
// Private data
UINT32 lpn_;
UINT32 sectOffset_;
UINT32 nSects_;
UINT8 chunksDone_[CHUNKS_PER_PAGE];
UINT8 mode_;
UINT32 lastChunk_;
UINT32 chunkIdx_;
UINT32 oldChunkAddr_;

/* IMPORTANT: added a flash_finish check, because FTL_BUF can be still in use in a previous write */
static void initRead(const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects, const UINT8 mode) {
    uart_print("initRead: lpn = "); uart_print_int(dataLpn);
    uart_print(", sectOffset = "); uart_print_int(sectOffset);
    uart_print(", nSects = "); uart_print_int(nSects);
    uart_print(", mode = ");
    flash_finish();
    if (mode == GcMode)
    {
        uart_print("GC\r\n");
    }
    else
    {
        uart_print("Read\r\n");
    }
    lpn_ = dataLpn;
    sectOffset_ = sectOffset;
    nSects_ = nSects;
    for (UINT32 i=0; i< CHUNKS_PER_PAGE; i++) chunksDone_[i] = 0;
    mode_ = mode;
    lastChunk_ = (sectOffset + nSects + SECTORS_PER_CHUNK -1) / SECTORS_PER_CHUNK;
    uart_print("lastChunk = "); uart_print_int(lastChunk_); uart_print("\r\n");
    //mem_set_dram(FTL_BUF(0), INVALID, BYTES_PER_PAGE);
}

static UINT8 chunksInSameFlashPage(const UINT32 chunkA, const UINT32 chunkB) {
    if (ChunkToBank(chunkA) == ChunkToBank(chunkB) &&
        ChunkToLbn(chunkA) == ChunkToLbn(chunkB) &&
        ChunkToPageOffset(chunkA) == ChunkToPageOffset(chunkB)){
        return 1;
    } else {
        return 0;
    }
}

// This function rebuilds a page into the FTL BUF. If one of the chunks is in the log buffer, returns 1, otherwise returns 0.
// If there's at least a chunk in log buffer, the page cannot be copied to data block, otherwise the lpn remains in the log buffer and will be reinserted when the log is flushed to flash.
void rebuildPageToFtlBuf(const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects, const UINT8 mode) {
    uart_print("rebuild page to FTL BUF lpn="); uart_print_int(dataLpn); uart_print("\r\n");
    initRead(dataLpn, sectOffset, nSects, mode);
    for (chunkIdx_ = sectOffset_ / SECTORS_PER_CHUNK; chunkIdx_<lastChunk_; chunkIdx_++) {
        uart_print("c "); uart_print_int(chunkIdx_); uart_print("\r\n");
        if (chunksDone_[chunkIdx_] == 0)
        {
            chunksDone_[chunkIdx_] = 1;
            //oldChunkAddr_ = getChunkAddr(node_, chunkIdx_);
            if (ChunksMapTable(dataLpn, chunkIdx_) > DRAM_BASE + DRAM_SIZE)
            {
                uart_print_level_1("ERROR in rebuildPageToFtlBuf 1: reading above DRAM address space\r\n");
            }
            oldChunkAddr_ = read_dram_32(ChunksMapTable(dataLpn, chunkIdx_));
            uart_print("oldChunkAddr is "); uart_print_int(oldChunkAddr_); uart_print("\r\n");
            switch (findChunkLocation(oldChunkAddr_))
            {
                case Invalid:
                {
                    uart_print(" not valid\r\n");
                    chunkInvalid();
                    continue;
                    break;
                }
                case FlashWLog:
                {
                    uart_print(" in flash w log\r\n");
                    chunkInFlashLog(FALSE);
                    continue;
                    break;
                }
                case DRAMWLog:
                {
                    uart_print(" in dram w log\r\n");
                    chunkInWDramBuf();
                    continue;
                    break;
                }
#if Overwrite
                case DRAMOwLog:
                {
                    uart_print(" in dram ow log\r\n");
                    oldChunkAddr_ = oldChunkAddr_ & 0x7FFFFFFF;
                    chunkInOWDramBuf();
                    break;
                }
                case FlashOwLog:
                {
                    uart_print(" in flash ow log\r\n");
                    oldChunkAddr_ = oldChunkAddr_ & 0x7FFFFFFF;
                    chunkInFlashLog(TRUE);
                    break;
                }
#endif
            }
        }
        else
        {
            uart_print(" already done\r\n");
        }
    }
}

static void chunkInvalid()
{
    uart_print("chunkInvalid\n");
    UINT32 dst = FTL_BUF(0) + (chunkIdx_*BYTES_PER_CHUNK);
    mem_set_dram (dst, INVALID, BYTES_PER_CHUNK);
}

static void chunkInWDramBuf()
{
    uart_print("chunkInWDramBuf\r\n");
    UINT32 dst = FTL_BUF(0) + (chunkIdx_*BYTES_PER_CHUNK);
    UINT32 src = LOG_BUF(ChunkToBank(oldChunkAddr_))+(ChunkToSectOffset(oldChunkAddr_)*BYTES_PER_SECTOR);
    mem_copy(dst, src, BYTES_PER_CHUNK);
    if (mode_ == GcMode) logBufMeta[ChunkToBank(oldChunkAddr_)].dataLpn[oldChunkAddr_ % CHUNKS_PER_PAGE] = INVALID;
}

#if Overwrite
static void chunkInOWDramBuf()
{
    uart_print("chunkInOWDramBuf\r\n");
    UINT32 dst = FTL_BUF(0) + (chunkIdx_*BYTES_PER_CHUNK);
    UINT32 src = OW_LOG_BUF(ChunkToBank(oldChunkAddr_))+(ChunkToSectOffset(oldChunkAddr_)*BYTES_PER_SECTOR);
    mem_copy(dst, src, BYTES_PER_CHUNK);
    if (mode_ == GcMode) owLogBufMeta[ChunkToBank(oldChunkAddr_)].dataLpn[oldChunkAddr_ % CHUNKS_PER_PAGE] = INVALID;
}
#endif

static void chunkInFlashLog(UINT8 inOW)
{
    uart_print("chunkInFlashLog\r\n");
    UINT32 chunksInPage=1;
    UINT32 srcChunkByteOffsets[CHUNKS_PER_PAGE];
    UINT32 chunkIdxs[CHUNKS_PER_PAGE];
    UINT8 decreaseInOW[CHUNKS_PER_PAGE];
    srcChunkByteOffsets[0]=ChunkToSectOffset(oldChunkAddr_)*BYTES_PER_SECTOR;
    chunkIdxs[0]=chunkIdx_;
    decreaseInOW[0] = inOW;
    setupFlashPageRead(&chunksInPage, srcChunkByteOffsets, chunkIdxs, decreaseInOW);
    readFlashPage(&chunksInPage, srcChunkByteOffsets, chunkIdxs, decreaseInOW);
}

static void setupFlashPageRead(UINT32 * chunksInPage, UINT32 * srcChunkByteOffsets, UINT32 * chunkIdxs, UINT8 *decreaseInOW)
{
    uart_print("setupFlashPageRead\r\n");
    for (UINT32 i=chunkIdx_+1; i<lastChunk_; i++)
    {
        uart_print("next chunk: "); uart_print_int(i); uart_print("\r\n");
        if (chunksDone_[i] == 0)
        {
            //UINT32 nextChunkAddr = getChunkAddr(node_, i);
            if (ChunksMapTable(lpn_, i) > DRAM_BASE + DRAM_SIZE)
            {
                uart_print_level_1("ERROR in setupFlashPageRead 1: reading above DRAM address space\r\n");
            }
            UINT32 nextChunkAddr = read_dram_32(ChunksMapTable(lpn_, i));
            switch (findChunkLocation(nextChunkAddr))
            {
                case Invalid:
                {
                    uart_print(" not valid\r\n");
                    continue;
                    break;
                }
                case FlashWLog:
                {
                    uart_print(" in flash w log\r\n");
                    if (chunksInSameFlashPage(oldChunkAddr_, nextChunkAddr))
                    {
                        uart_print(" in same flash log\r\n");
                        srcChunkByteOffsets[*chunksInPage]=ChunkToSectOffset(nextChunkAddr)*BYTES_PER_SECTOR;
                        chunkIdxs[*chunksInPage]=i;
                        decreaseInOW[*chunksInPage]=FALSE;
                        (*chunksInPage)++;
                    }
                    else
                    {
                        uart_print(" in different flash log\r\n");
                    }
                    continue;
                    break;
                }
                case DRAMWLog:
                {
                    uart_print(" in dram w log\r\n");
                    continue;
                    break;
                }
#if Overwrite
                case DRAMOwLog:
                {
                    uart_print(" in dram ow log\r\n");
                    continue;
                    break;
                }
                case FlashOwLog:
                {
                    nextChunkAddr = nextChunkAddr & 0x7FFFFFFF;
                    if (chunksInSameFlashPage(oldChunkAddr_, nextChunkAddr)) {
                        uart_print(" in same flash log\r\n");
                        srcChunkByteOffsets[*chunksInPage]=ChunkToSectOffset(nextChunkAddr)*BYTES_PER_SECTOR;
                        chunkIdxs[*chunksInPage]=i;
                        decreaseInOW[*chunksInPage]=TRUE;
                        (*chunksInPage)++;
                    }
                    else
                    {
                        uart_print(" in different flash log\r\n");
                    }
                    break;
                }
#endif
            }
        }
        else
        {
            uart_print(" already done\r\n");
        }
    }
}

static void readFlashPage(UINT32 *chunksInPage, UINT32 *srcChunkByteOffsets, UINT32 *chunkIdxs, UINT8 *decreaseInOW)
{
    uart_print("readFlashPage\r\n");
    if(*chunksInPage > 2)
    { // it's convenient to read the entire page
        readCompletePage(chunksInPage, srcChunkByteOffsets, chunkIdxs, decreaseInOW);
    }
    else
    { // read just one chunk
        readOneChunk(chunksInPage, srcChunkByteOffsets, chunkIdxs, decreaseInOW);
    }
}

static void readCompletePage(UINT32 *chunksInPage, UINT32 *srcChunkByteOffsets, UINT32 *chunkIdxs, UINT8 *decreaseInOW)
{
    uart_print("readCompletePage\r\n");
    UINT32 oldLogBank = ChunkToBank(oldChunkAddr_);
    UINT32 oldLogVbn = get_log_vbn(oldLogBank, ChunkToLbn(oldChunkAddr_));
    UINT32 oldLogPageOffset = ChunkToPageOffset(oldChunkAddr_);
    nand_page_ptread(oldLogBank, oldLogVbn, oldLogPageOffset, 0, SECTORS_PER_PAGE, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
    for (int i=0; i<*chunksInPage; i++)
    {
        UINT32 src = TEMP_BUF_ADDR + srcChunkByteOffsets[i];
        UINT32 dst = FTL_BUF(0) + chunkIdxs[i] * BYTES_PER_CHUNK;
        mem_copy(dst, src, BYTES_PER_CHUNK);
        chunksDone_[chunkIdxs[i]]=1;
        if(mode_ == GcMode)
        {
#if Overwrite
            if (decreaseInOW[i])
            {
                decrementValidChunks(&heapDataOverwrite, oldLogBank, ChunkToLbn(oldChunkAddr_));
            }
            else
#endif
            {
                decrementValidChunks(&heapDataWrite, oldLogBank, ChunkToLbn(oldChunkAddr_));
            }
        }
    }
}

static void readOneChunk(UINT32 *chunksInPage, UINT32 *srcChunkByteOffsets, UINT32 *chunkIdxs, UINT8 *decreaseInOW)
{
    uart_print("readOneChunk\r\n");
    UINT32 bank = ChunkToBank(oldChunkAddr_);
    UINT32 vbn = get_log_vbn(bank, ChunkToLbn(oldChunkAddr_));
    UINT32 pageOffset = ChunkToPageOffset(oldChunkAddr_);
    UINT32 dst = FTL_BUF(0) + (chunkIdxs[0]*BYTES_PER_CHUNK) - srcChunkByteOffsets[0]; // buf addr + dst - src
    nand_page_ptread(bank, vbn, pageOffset, srcChunkByteOffsets[0]/BYTES_PER_SECTOR, SECTORS_PER_CHUNK, dst, RETURN_WHEN_DONE);
    if(mode_ == GcMode)
    {
#if Overwrite
        if (decreaseInOW[0])
        {
            decrementValidChunks(&heapDataOverwrite, bank, ChunkToLbn(oldChunkAddr_));
        }
        else
#endif
        {
            decrementValidChunks(&heapDataWrite, bank, ChunkToLbn(oldChunkAddr_));
        }
    }
}

void readFromLogBlk (UINT32 const dataLpn, UINT32 const sectOffset, UINT32 const nSects){
    uart_print("readFromLogBlk dataLpn="); uart_print_int(dataLpn);
    uart_print(", sect_offset="); uart_print_int(sectOffset);
    uart_print(", num_sectors="); uart_print_int(nSects); uart_print("\r\n");

    UINT32 dst = RD_BUF_PTR(g_ftl_read_buf_id)+(sectOffset*BYTES_PER_SECTOR);
    UINT32 src = FTL_BUF(0)+(sectOffset*BYTES_PER_SECTOR);
    rebuildPageToFtlBuf(dataLpn, sectOffset, nSects, ReadMode);
    mem_copy(dst, src, nSects*BYTES_PER_SECTOR);
    g_ftl_read_buf_id = (g_ftl_read_buf_id + 1) % NUM_RD_BUFFERS;
    SETREG (BM_STACK_RDSET, g_ftl_read_buf_id);    // change bm_read_limit
    SETREG (BM_STACK_RESET, 0x02);    // change bm_read_limit
    //UINT32 bank = choose new bank
    //int bankToGarbageCollect = (bank + NUM_BANKS - 1) % NUM_BANKS;
    //callPM(bankToGarbageCollect);
}
