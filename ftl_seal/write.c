#include "jasmine.h"
#include "ftl_metadata.h"
#include "ftl_parameters.h"
#include "log.h"
#include "garbage_collection.h"
#include "heap.h" // decrementValidChunks
#include "flash.h" // RETURN_ON_ISSUE RETURN_WHEN_DONE
#include "read.h" // rebuildPageToFtlBuf
#include "write.h" // need to use writeToLogBlk in writeToSWBlock
#include "cleanList.h" // cleanListSize

//#define ProgMergeFreeBlksThreshold (LOG_BLK_PER_BANK)
#if NO_FLASH_OPS
#define nand_page_ptread(a, b, c, d, e, f, g)
#define nand_page_read(a, b, c, d)
#define nand_page_program(a, b, c, d, e)
#define nand_page_ptprogram(a, b, c, d, e, f, g)
#define nand_page_ptprogram_from_host(a, b, c, d, e)     g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS; \
                            SETREG (BM_STACK_WRSET, g_ftl_write_buf_id); \
                            SETREG (BM_STACK_RESET, 0x01);
//#define nand_page_copyback(a, b, c, d, e)
//#define nand_block_erase(a, b)
#endif

static void flushLogBuffer();
static void flushLogBufferDuringGC(const UINT32 bank);
static void initWrite(const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects);
//static void manageOldPartialPage();
static void manageOldCompletePage();
//static void manageOldChunk(int chunkIdx);
static void manageOldChunkForCompletePageWrite(int chunkIdx);
static void updateDramBufMetadata();
static void updateDramBufMetadataDuringGc(const UINT32 bank, const UINT32 lpn, const UINT32 sectOffset);
static void updateChunkPtr();
static void updateChunkPtrDuringGC(const UINT32 bank);

static void syncWithWriteLimit();

static void writeCompletePage();
//static void writePartialPageNew();
static void writeChunkNew(UINT32 nSectsToWrite);
static void writePartialPageOld();
//static void writeChunkNotInDram();
static void writeChunkOld();
static void writePartialChunkWhenOldIsInWBuf(UINT32 nSectsToWrite, UINT32 oldChunkAddr);
#if Overwrite
static void writePartialChunkWhenOldIsInOWBuf(UINT32 nSectsToWrite, UINT32 oldChunkAddr);
#endif
static void writePartialChunkWhenOldChunkIsInFlashLog(UINT32 nSectsToWrite, UINT32 oldChunkAddr);

static void appendPageToSWBlock (const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects);

// Data members
static UINT32 bank_;
static UINT32 lpn_;
static UINT32 sectOffset_;
static UINT32 nSects_;
static UINT32 remainingSects_;

BOOL32 g_inc_pmerge_interval_flag[NUM_BANKS];

static void flushLogBuffer()
{
    uart_print("bank ");
    uart_print_int(bank_);
    uart_print(" flushLogBuffer write lpn ");
    UINT32 newLogLpn = getRWLpn(bank_); // TODO: completely ignoring SW Log. Should use prepare_to_new_write if we want to use it
    uart_print("new log lpn=");
    uart_print_int(newLogLpn);
    uart_print("\r\n");
    UINT32 vBlk = get_log_vbn(bank_, LogPageToLogBlk(newLogLpn));
    UINT32 pageOffset = LogPageToOffset(newLogLpn);
    nand_page_program(bank_, vBlk, pageOffset, LOG_BUF(bank_), RETURN_ON_ISSUE);
    for(int i=0; i<CHUNKS_PER_PAGE; i++)
    {
        UINT32 lChunkAddr = (newLogLpn * CHUNKS_PER_PAGE) + i;
        if( (chunkInLpnsList(RWCtrl[bank_].lpnsListPtr, LogPageToOffset(newLogLpn), i)) >=(DRAM_BASE + DRAM_SIZE))
        {
            uart_print_level_1("ERROR in write::flushLogBuffer 1: writing to "); uart_print_level_1_int(chunkInLpnsList(RWCtrl[bank_].lpnsListPtr, LogPageToOffset(newLogLpn), i)); uart_print_level_1("\r\n");
        }
        write_dram_32(chunkInLpnsList(RWCtrl[bank_].lpnsListPtr, LogPageToOffset(newLogLpn), i), logBufMeta[bank_].dataLpn[i]);
        if (logBufMeta[bank_].dataLpn[i] != INVALID)
        {
            write_dram_32(ChunksMapTable(logBufMeta[bank_].dataLpn[i], logBufMeta[bank_].chunkIdx[i]), (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + lChunkAddr);
        }
        else
        {
            decrementValidChunks(&heapDataWrite, bank_, LogPageToLogBlk(newLogLpn)); // decrement blk with previous copy
        }
    }
    increaseRWLpn(bank_);
}

static void flushLogBufferDuringGC(const UINT32 bank)
{
    uart_print("flushLogBufferDuringGC bank="); uart_print_int(bank); uart_print("\r\n");
    UINT32 newLogLpn = getRWLpn(bank); // TODO: completely ignoring SW Log. Should use prepare_to_new_write if we want to use it
    uart_print("FlushLog to lpn="); uart_print_int(newLogLpn); uart_print("\r\n");
    UINT32 vBlk = get_log_vbn(bank, LogPageToLogBlk(newLogLpn));
    UINT32 pageOffset = LogPageToOffset(newLogLpn);
    nand_page_program(bank, vBlk, pageOffset, LOG_BUF(bank), RETURN_ON_ISSUE);
    for(int i=0; i<CHUNKS_PER_PAGE; i++)
    {
        UINT32 lChunkAddr = (newLogLpn * CHUNKS_PER_PAGE) + i;
        if( (chunkInLpnsList(RWCtrl[bank].lpnsListPtr, LogPageToOffset(newLogLpn), i)) >= (DRAM_BASE + DRAM_SIZE))
        {
            uart_print_level_1("ERROR in write::flushLogBufferDuringGC 1: writing to "); uart_print_level_1_int(chunkInLpnsList(RWCtrl[bank].lpnsListPtr, LogPageToOffset(newLogLpn), i)); uart_print_level_1("\r\n");
        }
        write_dram_32(chunkInLpnsList(RWCtrl[bank].lpnsListPtr, LogPageToOffset(newLogLpn), i), logBufMeta[bank].dataLpn[i]);
        if (logBufMeta[bank].dataLpn[i] != INVALID)
        {
            write_dram_32(ChunksMapTable(logBufMeta[bank].dataLpn[i], logBufMeta[bank].chunkIdx[i]), (bank * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + lChunkAddr);
        }
        else
        {
            decrementValidChunks(&heapDataWrite, bank, LogPageToLogBlk(newLogLpn)); // decrement blk with previous copy
        }
    }
    increaseRWLpn(bank);
}

UINT8 writeChunkOnLogBlockDuringGC(const UINT32 dataLpn, const UINT32 dataChunkOffset, const UINT32 chunkOffsetInBuf, const UINT32 bufAddr)
{
    /* Need this function because during GC can't use the global variables, because those might be related to an outstanding write (which triggered the GC) */
    UINT32 bank = chooseNewBank();
    uart_print("writeChunkOnLogBlockDuringGC, bank="); uart_print_int(bank); uart_print(" dataLpn="); uart_print_int(dataLpn);
    uart_print(", dataChunkOffset="); uart_print_int(dataChunkOffset); uart_print("\r\n");
    int sectOffset = dataChunkOffset * SECTORS_PER_CHUNK;
    UINT32 src = bufAddr + (chunkOffsetInBuf * BYTES_PER_CHUNK);
    UINT32 dst = LOG_BUF(bank) + (chunkPtr[bank] * BYTES_PER_CHUNK); // base address of the destination chunk
    waitBusyBank(bank);
    mem_copy(dst, src, BYTES_PER_CHUNK);
    updateDramBufMetadataDuringGc(bank, dataLpn, sectOffset);
    updateChunkPtrDuringGC(bank);
    return 0;
}

static void updateDramBufMetadataDuringGc(const UINT32 bank, const UINT32 lpn, const UINT32 sectOffset)
{
    uart_print("updateDramBufMetadataDuringGc\r\n");
    UINT32 chunkIdx = sectOffset / SECTORS_PER_CHUNK;
    logBufMeta[bank].dataLpn[chunkPtr[bank]]=lpn;
    logBufMeta[bank].chunkIdx[chunkPtr[bank]]=chunkIdx;
    //int ret = shashtblUpdate(lpn, chunkIdx, (bank * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (LogBufLpn * CHUNKS_PER_PAGE) + chunkPtr[bank]); // Optimize: already have node, why use hash update???
    write_dram_32(ChunksMapTable(lpn, chunkIdx), (bank * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (LogBufLpn * CHUNKS_PER_PAGE) + chunkPtr[bank]);
    /*if (ret != 0)
    {
        uart_print_level_1("ERROR: in updateDramBufMetadataDuringGc failed to update node.\r\n");
        uart_print_level_1("lpn="); uart_print_level_1_int(lpn); uart_print_level_1("\r\n");
        uart_print_level_1("node lpn="); uart_print_level_1_int(read_dram_32(&node->key)); uart_print_level_1("\r\n");
        while(1);
    }
    */
}

/*
UINT32 chooseNewBank()
{
    uart_print("chooseNewBank w log: ");
    UINT32 maxFreePages = 0;
    UINT32 candidateBank = 0;
    for (UINT32 i = 0; i < NUM_BANKS; ++i)
    {
        UINT32 cleanPages = cleanListSize(&cleanListDataWrite, i) * UsedPagesPerLogBlk;
        //UINT32 SWLpn = SWCtrl[i].logLpn != INVALID ? LogPageToOffset(SWCtrl[i].logLpn) : UsedPagesPerLogBlk;
        UINT32 RWLpn = RWCtrl[i].logLpn != INVALID ? LogPageToOffset(RWCtrl[i].logLpn) : UsedPagesPerLogBlk;
#if Overwrite
        UINT32 OWLpn = OWCtrl[i].logLpn != INVALID ? LogPageToOffset(OWCtrl[i].logLpn)/2 : UsedPagesPerLogBlk;
#else
        UINT32 OWLpn = UsedPagesPerLogBlk;
#endif
        //uart_print(" cleanPages: "); uart_print_int(cleanPages);
        //uart_print(" SWLpn: "); uart_print_int(UsedPagesPerLogBlk - SWLpn);
        //uart_print(" RWLpn: "); uart_print_int(UsedPagesPerLogBlk - RWLpn); uart_print("\r\n");
        //UINT32 validChunksInWriteHeap = getVictimValidPagesNumber(&heapDataWrite, i) == INVALID ? CHUNKS_PER_LOG_BLK : getVictimValidPagesNumber(&heapDataWrite, i);
        //UINT32 validChunksInOverwriteHeap = getVictimValidPagesNumber(&heapDataOverwrite, i) == INVALID ? CHUNKS_PER_LOG_BLK : getVictimValidPagesNumber(&heapDataOverwrite, i);
        //UINT32 freePages = cleanPages + (UsedPagesPerLogBlk - OWLpn) + (UsedPagesPerLogBlk - SWLpn) + (UsedPagesPerLogBlk - RWLpn) + ((CHUNKS_PER_LOG_BLK - validChunksInWriteHeap)/CHUNKS_PER_PAGE) + ((CHUNKS_PER_LOG_BLK - validChunksInOverwriteHeap)/CHUNKS_PER_PAGE);
        //UINT32 freePages = cleanPages + (UsedPagesPerLogBlk - OWLpn) + (UsedPagesPerLogBlk - SWLpn) + (UsedPagesPerLogBlk - RWLpn);
        UINT32 freePages = cleanPages + (UsedPagesPerLogBlk - OWLpn) + (UsedPagesPerLogBlk - RWLpn);
        if (freePages > maxFreePages)
        {
            {
                maxFreePages = freePages;
                candidateBank = i;
            }
        }
    }
    uart_print("chosen bank="); uart_print_int(candidateBank); uart_print("\r\n");
    return(candidateBank);
}
*/
UINT32 referenceBank=0;
UINT32 chooseNewBank()
{
    referenceBank = (referenceBank + 1) % NUM_BANKS;
    return referenceBank;
}


static void initWrite(const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects)
{
    uart_print("initWrite\r\n");
    lpn_ = dataLpn;
    sectOffset_ = sectOffset;
    nSects_ = nSects;
    remainingSects_ = nSects;
}


/*static void manageOldPartialPage()
{
    uart_print("manageOldPartialPage\r\n");
    UINT32 firstChunk = sectOffset_ / SECTORS_PER_CHUNK;
    UINT32 lastChunk = (sectOffset_ + nSects_ - 1) / SECTORS_PER_CHUNK;
    for (int chunkIdx=firstChunk; chunkIdx<=lastChunk; chunkIdx++)
    {
        uart_print("Chunk "); uart_print_int(chunkIdx); uart_print("\r\n");
        manageOldChunkForCompletePageWrite(chunkIdx);
        //manageOldChunk(chunkIdx);
    }
}*/

static void manageOldCompletePage()
{
    uart_print("manageOldCompletePage\r\n");
    for (int chunkIdx=0; chunkIdx<CHUNKS_PER_PAGE; chunkIdx++)
    {
        uart_print("Chunk "); uart_print_int(chunkIdx); uart_print("\r\n");
        manageOldChunkForCompletePageWrite(chunkIdx);
    }
}

/* Completely invalid every metadata related to the chunk, because no GC can happen before the new page written since it is a complete page write */
static void manageOldChunkForCompletePageWrite(int chunkIdx)
{
    uart_print("manageOldChunkForCompletePageWrite\r\n");
    //UINT32 oldChunkAddr = getChunkAddr(node_, chunkIdx);
    if (ChunksMapTable(lpn_, chunkIdx) > DRAM_BASE + DRAM_SIZE)
    {
        uart_print_level_1("ERROR in manageOldChunkForCompletePageWrite 1: reading above DRAM address space\r\n");
    }
    UINT32 oldChunkAddr = read_dram_32(ChunksMapTable(lpn_, chunkIdx));
    switch (findChunkLocation(oldChunkAddr))
    {
        case Invalid:
        {
            uart_print(" invalid\r\n");
            return;
        }
        case FlashWLog:
        {
            uart_print(" in w log\r\n");
            UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
            decrementValidChunks(&heapDataWrite, oldChunkBank, ChunkToLbn(oldChunkAddr));
            return;
        }
        case DRAMWLog:
        {
            uart_print(" in w DRAM buf\r\n");
            UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
            logBufMeta[oldChunkBank].dataLpn[oldChunkAddr % CHUNKS_PER_PAGE]=INVALID;
            return;
        }
#if Overwrite
        case DRAMOwLog:
        {
            uart_print(" in ow DRAM buf\r\n");
            oldChunkAddr = oldChunkAddr & 0x7FFFFFFF;
            UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
            owLogBufMeta[oldChunkBank].dataLpn[oldChunkAddr % CHUNKS_PER_PAGE]=INVALID;
            return;
        }
        case FlashOwLog:
        {
            uart_print(" in ow log\r\n");
            oldChunkAddr = oldChunkAddr & 0x7FFFFFFF;
            UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
            decrementValidChunks(&heapDataOverwrite, oldChunkBank, ChunkToLbn(oldChunkAddr));
            return;
        }
#endif
    }
}
/*static void manageOldChunkForCompletePageWrite(int chunkIdx) {
    uart_print("manageOldChunkForCompletePageWrite\r\n");
    UINT32 oldChunkAddr = getChunkAddr(node_, chunkIdx);
    if (oldChunkAddr == INVALID) {
        uart_print("invalid\r\n");
        return;
    }
    UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
    UINT32 oldChunkOffsetInBank = ChunkToChunkOffsetInBank(oldChunkAddr);
    if (oldChunkOffsetInBank >= LogBufLpn*CHUNKS_PER_PAGE){
        uart_print(" in w DRAM buf\r\n");
        logBufMeta[oldChunkBank].dataLpn[oldChunkAddr % CHUNKS_PER_PAGE]=INVALID;
        return;
    } else{
        uart_print(" in w log\r\n");
        decrementValidChunks(&heapDataWrite, oldChunkBank, ChunkToLbn(oldChunkAddr)); // decrement blk with previous copy
    }
}*/

//TODO: this was the old version of the manage old chunk function, which doesn't invalidate the chunk in DRAM for reasons that I'm not sure about.
//      For now this is substituted with the manageOldChunkForCompletePageWrite, which is updated to new mapping of OW chunk, and invalidates everything.
//      Needs to check if this is the correct behaviour
/*static void manageOldChunk(int chunkIdx)
{
    uart_print("manageOldChunk\r\n");
    UINT32 oldChunkAddr = getChunkAddr(node_, chunkIdx);
    if (oldChunkAddr == INVALID)
    {
        uart_print("invalid\r\n");
        return;
    }
    UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
    UINT32 oldChunkOffsetInBank = ChunkToChunkOffsetInBank(oldChunkAddr);
    if (oldChunkOffsetInBank >= LogBufLpn*CHUNKS_PER_PAGE)
    {
        uart_print(" in w DRAM buf\r\n");
        //logBufMeta[oldChunkBank].dataLpn[oldChunkAddr % CHUNKS_PER_PAGE]=INVALID; // Should not invalidate here, because if this write causes a flush, this chunk won't be copied
        return;
    }
    else
    {
        uart_print(" in w log\r\n");
        decrementValidChunks(&heapDataWrite, oldChunkBank, ChunkToLbn(oldChunkAddr)); // decrement blk with previous copy
    }
}*/


static void updateDramBufMetadata()
{
    uart_print("updateDramBufMetadata\r\n");
    UINT32 chunkIdx = sectOffset_ / SECTORS_PER_CHUNK;
    logBufMeta[bank_].dataLpn[chunkPtr[bank_]]=lpn_;
    logBufMeta[bank_].chunkIdx[chunkPtr[bank_]]=chunkIdx;
    //shashtblUpdate(lpn_, chunkIdx, (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (LogBufLpn * CHUNKS_PER_PAGE) + chunkPtr[bank_]); // Optimize: already have node, why use hash update???
    write_dram_32(ChunksMapTable(lpn_, chunkIdx), (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (LogBufLpn * CHUNKS_PER_PAGE) + chunkPtr[bank_]);
}

static void updateChunkPtr()
{
    uart_print("updateChunkPtr\r\n");
    chunkPtr[bank_] = (chunkPtr[bank_] + 1) % CHUNKS_PER_PAGE;
    uart_print("new chunkPtr for bank "); uart_print_int(bank_); uart_print(" is "); uart_print_int(chunkPtr[bank_]); uart_print("\r\n");
    if (chunkPtr[bank_] == 0)
        flushLogBuffer();
}

static void updateChunkPtrDuringGC(const UINT32 bank)
{
    uart_print("updateChunkPtrDuringGC\r\n");
    chunkPtr[bank] = (chunkPtr[bank] + 1) % CHUNKS_PER_PAGE;
    uart_print("new chunkPtr for bank "); uart_print_int(bank); uart_print(" is "); uart_print_int(chunkPtr[bank]); uart_print("\r\n");
    if (chunkPtr[bank] == 0)
        flushLogBufferDuringGC(bank);
}

static void syncWithWriteLimit() {
    int count=0;
    while(g_ftl_write_buf_id != GETREG(BM_WRITE_LIMIT)) {
        count++;
        if (count == 100000) {
            count=0;
            uart_print_level_1("*\r\n");
            uart_print_level_1("FTL Buf Id: "); uart_print_level_1_int(g_ftl_write_buf_id); uart_print_level_1("\r\n");
            uart_print_level_1("SATA Buf Id: "); uart_print_level_1_int(GETREG(BM_WRITE_LIMIT)); uart_print_level_1("\r\n");
        }
    }
}

/*
void writeToSWBlk(const UINT32 lba, const UINT32 nSects)
{
    uart_print("writeToSWBlk lba="); uart_print_int(lba);
    uart_print(", nSects="); uart_print_int(nSects); uart_print("\r\n");
    syncWithWriteLimit();
    UINT32 lpn = lba / SECTORS_PER_PAGE;
    BOOL8 found = FALSE;
    for(UINT32 bank=0; bank<NUM_BANKS; ++bank)
    {
        if(SWCtrl[bank].nextDataLpn == lpn)
        {
            found = TRUE;
            uart_print("Found stripe continuation on bank "); uart_print_int(bank); uart_print("\r\n");
            bank_ = bank;
            break;
        }
    }
    if (!found) {bank_ = chooseNewBank();}
    UINT32 sectOffset = lba % SECTORS_PER_PAGE;
    UINT32 remainingSects = nSects;
    if (sectOffset != 0)
    { // Deal with first incomplete page
        uart_print("Sequential stripe not aligned on chunk boundary\r\n");
        UINT32 nSectsToWrite = SECTORS_PER_PAGE - sectOffset;
        appendPageToSWBlock(lpn, sectOffset, nSectsToWrite);
        remainingSects-=nSectsToWrite;
        sectOffset=0;
        lpn++;
    }
    int count=0;
    while (remainingSects >= SECTORS_PER_PAGE)
    {
        count++;
        if (count > 100000)
        {
            uart_print_level_1("Warning in writeToSwBlk\r\n");
            count=0;
        }
        //TODO: when here should already have chosen a bank, this is probably unnecessary
        if(SWCtrl[bank_].nextDataLpn == INVALID) {bank_ = chooseNewBank();}
        UINT32 nSectsToWrite = ((sectOffset + remainingSects) < SECTORS_PER_PAGE) ? remainingSects : (SECTORS_PER_PAGE - sectOffset);
        UINT32 logLpn = getSWLpn(bank_);
        UINT32 vBlk = get_log_vbn(bank_, LogPageToLogBlk(logLpn));
        nand_page_program_from_host(bank_, vBlk, LogPageToOffset(logLpn));
        manageOldCompletePage();
        for(int i=sectOffset/SECTORS_PER_CHUNK; i<CHUNKS_PER_PAGE; i++)
        {
            UINT32 lChunkAddr = (logLpn * CHUNKS_PER_PAGE) + i;
            if((chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), i)) >=(DRAM_BASE + DRAM_SIZE))
            {
                uart_print_level_1("ERROR in write::writeToSWBlk 1: writing to "); uart_print_level_1_int(chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), i)); uart_print_level_1("\r\n");
            }
            write_dram_32(chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), i), lpn);
            //shashtblUpdate(lpn, i, (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + lChunkAddr);
            write_dram_32(ChunksMapTable(lpn, i), (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + lChunkAddr);
        }
        remainingSects-=nSectsToWrite;
        sectOffset=0;
        lpn++;
        SWCtrl[bank_].nextDataLpn=lpn;
        increaseSWLpn(bank_);
    }
    // Deal with last incomplete page
    if (remainingSects > 0) writeToLogBlk(lpn, 0, remainingSects);
}
*/

/* NOTE: This function calls rebuildPageToFtlBuf with GcMode, therefore the valid chunks counters of old blocks are already managed.
 * Do not call manageOldChunks before calling this!
 */
static void appendPageToSWBlock (const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects)
{
    uart_print("appendPageToSWBlock dataLpn="); uart_print_int(dataLpn);
    uart_print(", sectOffset="); uart_print_int(sectOffset);
    uart_print(", nSects="); uart_print_int(nSects); uart_print("\r\n");
    UINT32 nSectsToWrite = SECTORS_PER_PAGE - sectOffset;
    UINT32 logLpn = getSWLpn(bank_);
    UINT32 vBlk = get_log_vbn(bank_, LogPageToLogBlk(logLpn));
    UINT32 dst = FTL_BUF(0) + (sectOffset*BYTES_PER_SECTOR);
    UINT32 src = WR_BUF_PTR(g_ftl_write_buf_id)+(sectOffset*BYTES_PER_SECTOR);
    rebuildPageToFtlBuf(dataLpn, 0, SECTORS_PER_PAGE, GcMode); // Rebuild rest of the page in FTL buffer (rebuild entire page to be sure that all chunks are correctly garbage collected, especially if they are in DRAM)
    //waitBusyBank(bank_);
    flash_finish();
    mem_copy(dst, src, nSectsToWrite * BYTES_PER_SECTOR);                                       // Fill FTL buffer with new data
    //TODO: this program shouldn't be sincronous, need a global variable storing last bank writing data from FTL_BUF(0)
    nand_page_program(bank_, vBlk, LogPageToOffset(logLpn), FTL_BUF(0), RETURN_WHEN_DONE);      // Write FTL buffer to the next sequential page
    UINT32 chunkIdx;
    for(chunkIdx=0; chunkIdx<sectOffset / SECTORS_PER_CHUNK; ++chunkIdx)
    { // For sector before the start of new data we update only if previously there was some valid data, which is now in the new page, otherwise we insert invalid in the lpns list to speed up GC later
        if (ChunksMapTable(dataLpn, chunkIdx) > DRAM_BASE + DRAM_SIZE)
        {
            uart_print_level_1("ERROR in appendPageToSWBlk 1: reading above DRAM address space\r\n");
        }
        if (read_dram_32(ChunksMapTable(dataLpn, chunkIdx)) != INVALID)
        {
            UINT32 lChunkAddr = (logLpn * CHUNKS_PER_PAGE) + chunkIdx;
            if((chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), chunkIdx)) >=(DRAM_BASE + DRAM_SIZE))
            {
                uart_print_level_1("ERROR in write::appendPageToSWBlk 1: writing to "); uart_print_level_1_int(chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), chunkIdx)); uart_print_level_1("\r\n");
            }
            write_dram_32(chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), chunkIdx), dataLpn);
            write_dram_32(ChunksMapTable(dataLpn, chunkIdx), (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + lChunkAddr);
        }
        else
        { //Decrement valid chunks in the blk we're going to write in because we inserted null data
            if((chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), chunkIdx)) >=(DRAM_BASE + DRAM_SIZE))
            {
                uart_print_level_1("ERROR in write::appendPageToSWBlk 2: writing to "); uart_print_level_1_int(chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), chunkIdx)); uart_print_level_1("\r\n");
            }
            write_dram_32(chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), chunkIdx), INVALID);
            decrementValidChunks(&heapDataWrite, bank_, LogPageToLogBlk(logLpn));
        }
    }
    for( ; chunkIdx < CHUNKS_PER_PAGE; ++chunkIdx)
    { // The new sectors are instead all valid, therefore we don't bother checking if they were valid before
            UINT32 lChunkAddr = (logLpn * CHUNKS_PER_PAGE) + chunkIdx;
            if((chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), chunkIdx)) >=(DRAM_BASE + DRAM_SIZE))
            {
                uart_print_level_1("ERROR in write::appendPageToSWBlk 3: writing to "); uart_print_level_1_int(chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), chunkIdx)); uart_print_level_1("\r\n");
            }
            write_dram_32(chunkInLpnsList(SWCtrl[bank_].lpnsListPtr, LogPageToOffset(logLpn), chunkIdx), dataLpn);
            write_dram_32(ChunksMapTable(dataLpn, chunkIdx), (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + lChunkAddr);
    }
    SWCtrl[bank_].nextDataLpn=dataLpn+1;
    increaseSWLpn(bank_);
    g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
    SETREG (BM_STACK_WRSET, g_ftl_write_buf_id);
    SETREG (BM_STACK_RESET, 0x01);
}

void writeToLogBlk (UINT32 const dataLpn, UINT32 const sectOffset, UINT32 const nSects)
{
    //uart_print_level_1("2 ");
    uart_print("writeToLogBlk dataLpn="); uart_print_int(dataLpn);
    uart_print(", sect_offset="); uart_print_int(sectOffset);
    uart_print(", num_sectors="); uart_print_int(nSects); uart_print("\r\n");
    if ( sectOffset + nSects == SECTORS_PER_PAGE )
    {
        for (UINT32 bank=0; bank < NUM_BANKS; ++bank)
        {
            if(SWCtrl[bank].nextDataLpn == dataLpn)
            {
                uart_print("Found stripe continuation on bank "); uart_print_int(bank); uart_print("\r\n");
                bank_ = bank;
                appendPageToSWBlock (dataLpn, sectOffset, nSects);
                return;
            }
        }
    }
    bank_ = chooseNewBank();
    initWrite(dataLpn, sectOffset, nSects);
    //if (!isNewWrite_)
    //{
        if (nSects_ != SECTORS_PER_PAGE)
        {
            //manageOldPartialPage();
            syncWithWriteLimit();
            writePartialPageOld();
        }
        else
        { // writing entire page, so write directly to
            manageOldCompletePage();
            writeCompletePage();
        }
    //}
    //else
    //{
        //if (nSects_ != SECTORS_PER_PAGE)
        //{
            //syncWithWriteLimit();
            //writePartialPageNew();
        //}
        //else
        //{ // writing entire page, so write directly to
            //writeCompletePage();
        //}
    //}
}

static void writeCompletePage() {
    //uart_print_level_1("3 ");
    uart_print("writeCompletePage\r\n");
    UINT32 newLogLpn = getRWLpn(bank_);
    UINT32 vBlk = get_log_vbn(bank_, LogPageToLogBlk(newLogLpn));
    UINT32 pageOffset = LogPageToOffset(newLogLpn);
    nand_page_ptprogram_from_host (bank_, vBlk, pageOffset, 0, SECTORS_PER_PAGE); // write new data (make sure that the new data is ready in the write buffer frame) (c.f FO_B_SATA_W flag in flash.h)
    for(UINT32 i=0; i<CHUNKS_PER_PAGE; i++)
    {
        if((chunkInLpnsList(RWCtrl[bank_].lpnsListPtr, LogPageToOffset(newLogLpn), i)) >=(DRAM_BASE + DRAM_SIZE))
        {
            uart_print_level_1("ERROR in write::writeCompletePage 1: writing to "); uart_print_level_1_int(chunkInLpnsList(RWCtrl[bank_].lpnsListPtr, LogPageToOffset(newLogLpn), i)); uart_print_level_1("\r\n");
        }
        write_dram_32(chunkInLpnsList(RWCtrl[bank_].lpnsListPtr, LogPageToOffset(newLogLpn), i), lpn_);
        //shashtblUpdate(lpn_, i, (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (newLogLpn * CHUNKS_PER_PAGE) + i);
        write_dram_32(ChunksMapTable(lpn_, i), (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (newLogLpn * CHUNKS_PER_PAGE) + i);
    }
    increaseRWLpn(bank_);
}

/*
static void writePartialPageNew() {
    //uart_print_level_1("4 ");
    uart_print("writePartialPageNew\r\n");
    int count=0;
    while (remainingSects_ != 0) {
        count++;
        if (count > 100000) {
            count=0;
            uart_print_level_1("Warning in writePartialPageNew\r\n");
        }
        UINT32 nSectsToWrite = (((sectOffset_ % SECTORS_PER_CHUNK) + remainingSects_) < SECTORS_PER_CHUNK) ? remainingSects_ :
                                                (SECTORS_PER_CHUNK - (sectOffset_ % SECTORS_PER_CHUNK));
        writeChunkNew(nSectsToWrite);
        updateDramBufMetadata();
        updateChunkPtr(RETURN_ON_ISSUE);
        sectOffset_ += nSectsToWrite;
        remainingSects_ -= nSectsToWrite;
    }
    // SATA buffer management
    g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
    SETREG (BM_STACK_WRSET, g_ftl_write_buf_id);
    SETREG (BM_STACK_RESET, 0x01);
}
*/

static void writeChunkNew(UINT32 nSectsToWrite)
{
    uart_print("writeChunkNew\r\n");
    UINT32 src = WR_BUF_PTR(g_ftl_write_buf_id)+(sectOffset_*BYTES_PER_SECTOR);
    UINT32 dst = LOG_BUF(bank_)+(chunkPtr[bank_]*BYTES_PER_CHUNK); // base address of the destination chunk
    waitBusyBank(bank_);
    if (nSectsToWrite != SECTORS_PER_CHUNK)
    {
        mem_set_dram (dst, 0xFFFFFFFF, BYTES_PER_CHUNK); // Initialize chunk in dram log buffer with 0xFF
    }
    mem_copy(dst+((sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR), src, nSectsToWrite*BYTES_PER_SECTOR);
}

static void writePartialPageOld()
{
    //uart_print_level_1("5 ");
    uart_print("writePartialPageOld\r\n");
    int count=0;
    while (remainingSects_ != 0)
    {
        count++;
        if (count > 100000)
        {
            count=0;
            uart_print_level_1("Warning in writePartialPageOld\r\n");
        }
        writeChunkOld();
    }
    // SATA buffer management
    g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
    SETREG (BM_STACK_WRSET, g_ftl_write_buf_id);
    SETREG (BM_STACK_RESET, 0x01);
}

static void writeChunkOld()
{
    //uart_print_level_1("7 ");
    uart_print("writeChunkOld\r\n");
    UINT32 nSectsToWrite = (((sectOffset_ % SECTORS_PER_CHUNK) + remainingSects_) < SECTORS_PER_CHUNK) ? remainingSects_ : (SECTORS_PER_CHUNK - (sectOffset_ % SECTORS_PER_CHUNK));
    UINT32 chunkIdx = sectOffset_ / SECTORS_PER_CHUNK;
    if (ChunksMapTable(lpn_, chunkIdx) > DRAM_BASE + DRAM_SIZE)
    {
        uart_print_level_1("ERROR in writeChunkOld 1: reading above DRAM address space\r\n");
    }
    UINT32 oldChunkAddr = read_dram_32(ChunksMapTable(lpn_, chunkIdx));
    uart_print("Old chunk is ");
    switch (findChunkLocation(oldChunkAddr))
    {
        case Invalid:
        {
            uart_print(" invalid\r\n");

            writeChunkNew(nSectsToWrite);
            updateDramBufMetadata();
            updateChunkPtr();
            sectOffset_ += nSectsToWrite;
            remainingSects_ -= nSectsToWrite;
            return;

            break;
        }
        case FlashWLog:
        {
            uart_print(" in w log\r\n");
            if (nSectsToWrite == SECTORS_PER_CHUNK)
            {
                writeChunkNew(nSectsToWrite);
            }
            else
            {
                writePartialChunkWhenOldChunkIsInFlashLog(nSectsToWrite, oldChunkAddr);
            }
            UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
            decrementValidChunks(&heapDataWrite, oldChunkBank, ChunkToLbn(oldChunkAddr));
            updateDramBufMetadata();
            updateChunkPtr();
            sectOffset_ += nSectsToWrite;
            remainingSects_ -= nSectsToWrite;
            return;
            break;
        }
        case DRAMWLog:
        {
            uart_print(" in w DRAM buf\r\n");
            writePartialChunkWhenOldIsInWBuf(nSectsToWrite, oldChunkAddr);
            sectOffset_ += nSectsToWrite;
            remainingSects_ -= nSectsToWrite;
            break;
        }
#if Overwrite
        case DRAMOwLog:
        {
            uart_print(" in ow DRAM buf\r\n");
            oldChunkAddr = oldChunkAddr & 0x7FFFFFFF;
            if (nSectsToWrite == SECTORS_PER_CHUNK)
            {
                writeChunkNew(nSectsToWrite);
            }
            else
            {
                writePartialChunkWhenOldIsInOWBuf(nSectsToWrite, oldChunkAddr);
            }
            UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
            owLogBufMeta[oldChunkBank].dataLpn[oldChunkAddr % CHUNKS_PER_PAGE]=INVALID;
            updateDramBufMetadata();
            updateChunkPtr();
            sectOffset_ += nSectsToWrite;
            remainingSects_ -= nSectsToWrite;
            break;
        }
        case FlashOwLog:
        {
            uart_print(" in ow log\r\n");
            oldChunkAddr = oldChunkAddr & 0x7FFFFFFF;
            if (nSectsToWrite == SECTORS_PER_CHUNK)
            {
                writeChunkNew(nSectsToWrite);
            }
            else
            {
                writePartialChunkWhenOldChunkIsInFlashLog(nSectsToWrite, oldChunkAddr);
            }
            UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
            decrementValidChunks(&heapDataOverwrite, oldChunkBank, ChunkToLbn(oldChunkAddr));
            updateDramBufMetadata();
            updateChunkPtr();
            sectOffset_ += nSectsToWrite;
            remainingSects_ -= nSectsToWrite;
            break;
        }
#endif
    }
}

/*static void writeChunkNotInDram(UINT32 nSectsToWrite) {
    //uart_print_level_1("6 ");
    uart_print("writeChunkNotInDram\r\n");
    UINT32 src = WR_BUF_PTR(g_ftl_write_buf_id)+(sectOffset_*BYTES_PER_SECTOR);
    UINT32 dst = LOG_BUF(bank_)+(chunkPtr[bank_]*BYTES_PER_CHUNK); // base address of the destination chunk
    if (nSectsToWrite != SECTORS_PER_CHUNK) {
        mem_set_dram (dst, 0xFFFFFFFF, BYTES_PER_CHUNK); // Initialize chunk in dram log buffer with 0xFF
    }
    waitBusyBank(bank_);
    mem_copy(dst+((sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR), src, nSectsToWrite*BYTES_PER_SECTOR);
}*/


static void writePartialChunkWhenOldIsInWBuf(UINT32 nSectsToWrite, UINT32 oldChunkAddr) {
    uart_print("writePartialChunkWhenOldIsInWBuf\r\n");
    // Old Chunk Location
    UINT32 oldBank = ChunkToBank(oldChunkAddr);
    UINT32 oldSectOffset = ChunkToSectOffset(oldChunkAddr);
    // Buffers
    UINT32 dstWBufStart = LOG_BUF(oldBank)+(oldSectOffset*BYTES_PER_SECTOR); // location of old chunk
    UINT32 srcSataBufStart = WR_BUF_PTR(g_ftl_write_buf_id)+((sectOffset_ / SECTORS_PER_CHUNK)*BYTES_PER_CHUNK);
    // Sizes
    UINT32 startOffsetWrite = (sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR;
    //waitBusyBank(bank_);
    waitBusyBank(oldBank);
    mem_copy(dstWBufStart+startOffsetWrite, srcSataBufStart+startOffsetWrite, nSectsToWrite*BYTES_PER_SECTOR);
    #if MeasureDramAbsorb
    uart_print_level_1("WRDRAM "); uart_print_level_1_int(nSectsToWrite); uart_print_level_1("\r\n");
    #endif
}

#if Overwrite
static void writePartialChunkWhenOldIsInOWBuf(UINT32 nSectsToWrite, UINT32 oldChunkAddr)
{
    uart_print("writePartialChunkWhenOldIsInOWBuf\r\n");
    // Old Chunk Location
    UINT32 oldBank = ChunkToBank(oldChunkAddr);
    UINT32 oldSectOffset = ChunkToSectOffset(oldChunkAddr);
    // Buffers
    UINT32 dstWBufStart = LOG_BUF(bank_) + (chunkPtr[bank_] * BYTES_PER_CHUNK); // base address of the destination chunk
    UINT32 srcOWBufStart = OW_LOG_BUF(oldBank)+(oldSectOffset*BYTES_PER_SECTOR); // location of old chunk
    // Sizes
    waitBusyBank(bank_);
    mem_copy(dstWBufStart, srcOWBufStart, BYTES_PER_CHUNK);                                                         // First copy old data from OW Buf
    UINT32 startOffsetWrite = (sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR;
    UINT32 srcSataBufStart = WR_BUF_PTR(g_ftl_write_buf_id)+((sectOffset_ / SECTORS_PER_CHUNK)*BYTES_PER_CHUNK);
    mem_copy(dstWBufStart+startOffsetWrite, srcSataBufStart+startOffsetWrite, nSectsToWrite*BYTES_PER_SECTOR);      // Then copy new data from SATA Buf
}
#endif

static void writePartialChunkWhenOldChunkIsInFlashLog(UINT32 nSectsToWrite, UINT32 oldChunkAddr) {
    uart_print("writePartialChunkWhenOldChunkIsInFlashLog\r\n");
    UINT32 src = WR_BUF_PTR(g_ftl_write_buf_id)+((sectOffset_ / SECTORS_PER_CHUNK)*BYTES_PER_CHUNK);
    UINT32 dstWBufChunkStart = LOG_BUF(bank_) + (chunkPtr[bank_] * BYTES_PER_CHUNK); // base address of the destination chunk
    UINT32 startOffsetWrite = (sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR;
    // Old Chunk Location
    UINT32 oldBank = ChunkToBank(oldChunkAddr);
    UINT32 oldVbn = get_log_vbn(oldBank, ChunkToLbn(oldChunkAddr));
    UINT32 oldPageOffset = ChunkToPageOffset(oldChunkAddr);
    UINT32 oldSectOffset = ChunkToSectOffset(oldChunkAddr);
    // Offsets
    UINT32 dstByteOffset = chunkPtr[bank_] * BYTES_PER_CHUNK;
    UINT32 srcByteOffset = ChunkToChunkOffset(oldChunkAddr) * BYTES_PER_CHUNK;
    UINT32 alignedWBufAddr = LOG_BUF(bank_) + dstByteOffset - srcByteOffset;
    waitBusyBank(bank_);
    nand_page_ptread(oldBank, oldVbn, oldPageOffset, oldSectOffset, SECTORS_PER_CHUNK, alignedWBufAddr, RETURN_WHEN_DONE);
    mem_copy(dstWBufChunkStart + startOffsetWrite, src + startOffsetWrite, nSectsToWrite*BYTES_PER_SECTOR);
}
