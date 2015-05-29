#include "jasmine.h"
#include "ftl_metadata.h"
#include "ftl_parameters.h"
#include "log.h"
#include "garbage_collection.h"
#include "heap.h" // decrementValidChunks
#include "read.h" // rebuildPageToFtlBuf
#include "flash.h" // RETURN_ON_ISSUE RETURN_WHEN_DONE
#include "write.h"

#if Overwrite

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

//#define callPM

//#define progressiveMerge(x, y)
//#define resetProgressiveMerge(x)


// Private methods
//static inline UINT32 popCleanQueue(const UINT32 bank);
//#ifndef callPM
//static inline void callPM(const UINT32 bank);
//static inline void callPM(const UINT32 bank);
//#endif
/*****************************
 *       OVERWRITE
 ****************************/
static void chooseNewBank_();
static void initWrite(const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects);
//static void overwriteCompletePage();
//static void overwriteCompletePageInWLog();
static void overwriteCompletePageInOwLog();
static void overwriteCompletePageOld();
static UINT8 oldVersionIsInOwLogInOrder();
static void overwritePageOldInOrderInOwLog();
//static void overwritePartialPageNew();
static void overwritePartialPageOld();
static void overwritePartialPageOldNotInOrder();
static void overwriteChunkOldInOwBuf(UINT32 chunkAddr);
static void overwriteChunkOldInOwLog(UINT32 chunkAddr);
static void overwriteChunkOldInWBuf(UINT32 chunkAddr);
static void overwriteChunkOldInWLog(UINT32 chunkAddr);
static void overwritePartialChunkWhenOldChunkIsInWLog(UINT32 nSectsToWrite, UINT32 oldChunkAddr);
static void overwritePartialChunkWhenOldChunkIsInExhaustedOWLog(UINT32 nSectsToWrite, UINT32 oldChunkAddr);
static void overwriteChunkNew();
static void overwriteCompleteChunkNew();
static void overwritePartialChunkNew(UINT32 nSectsToWrite);

static void manageOldCompletePage();
static void manageOldChunkForCompletePageWrite(int chunkIdx);

static void updateOwChunkPtr();
static void updateOwDramBufMetadata();
static void flushOwLogBuffer();
static void syncWithWriteLimit();
static void increaseOwCounter(UINT32 bank, UINT32 lbn, UINT32 pageOffset);
static UINT8 readOwCounter(UINT32 bank, UINT32 lbn, UINT32 pageOffset);
//static void flushOwLogBufferToWLog();

// Data members
static UINT32 bank_ = 1;
static UINT32 lpn_;
static UINT32 sectOffset_;
static UINT32 nSects_;
static UINT32 remainingSects_;

static UINT32 lastBankUsingFtlBuf1=INVALID;

static void increaseOwCounter(UINT32 bank, UINT32 lbn, UINT32 pageOffset)
{
    UINT32 page = pageOffset == 0 ? 0 : (pageOffset+1)/2;
    UINT8 counter = read_dram_8(OwCounter(bank, lbn, page));
    counter = counter + 1;
    uart_print("increaseOwCounter bank="); uart_print_int(bank);
    uart_print(" lbn="); uart_print_int(lbn);
    uart_print(" pageOffset="); uart_print_int(pageOffset);
    uart_print(" => new counter="); uart_print_int(counter); uart_print("\r\n");
    write_dram_8(OwCounter(bank, lbn, page), counter);
}

static UINT8 readOwCounter(UINT32 bank, UINT32 lbn, UINT32 pageOffset)
{
    UINT32 page = pageOffset == 0 ? 0 : (pageOffset+1)/2;
    UINT8 counter = read_dram_8(OwCounter(bank, lbn, page));
    uart_print("readOwCounter bank="); uart_print_int(bank);
    uart_print(" lbn="); uart_print_int(lbn);
    uart_print(" pageOffset="); uart_print_int(pageOffset);
    uart_print(" => counter="); uart_print_int(counter); uart_print("\r\n");
    return counter;
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


void overwriteToLogBlk (UINT32 const dataLpn, UINT32 const sectOffset, UINT32 const nSects)
{
    uart_print("overwriteToLogBlk dataLpn="); uart_print_int(dataLpn);
    uart_print(", sect_offset="); uart_print_int(sectOffset);
    uart_print(", num_sectors="); uart_print_int(nSects); uart_print("\r\n");
    initWrite(dataLpn, sectOffset, nSects);
    if (nSects_ != SECTORS_PER_PAGE)
    {
        syncWithWriteLimit();
        overwritePartialPageOld();
    }
    else
    {
        overwriteCompletePageOld();
    }
}

/*void flushOverwriteLog() {
    uart_print("flushOverwriteLog\r\n");
    for(UINT32 bank=0; bank<NUM_BANKS; bank++) {
        if (owChunkPtr[bank] > 0) {
            uart_print("bank "); uart_print_int(bank); uart_print("\r\n");
            UINT32 newLogLpn = getOWLpn(bank);
            UINT32 vBlk = get_log_vbn(bank, LogPageToLogBlk(newLogLpn));
            UINT32 pageOffset = LogPageToOffset(newLogLpn);
            nand_page_ptprogram(bank, vBlk, pageOffset, 0, owChunkPtr[bank] * SECTORS_PER_CHUNK, OW_LOG_BUF(bank), RETURN_WHEN_DONE);
            for(int i=0; i<owChunkPtr[bank]; i++){
                UINT32 lChunkAddr = (newLogLpn * CHUNKS_PER_PAGE) + i;
                write_dram_32(chunkInOwLpnsBuf(bank, LogPageToOffset(newLogLpn), i), owLogBufMeta[bank].dataLpn[i]);
                if (owLogBufMeta[bank].dataLpn[i] != INVALID){
                    shashtblUpdate(owLogBufMeta[bank].dataLpn[i], owLogBufMeta[bank].chunkIdx[i], (bank * TOT_LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + lChunkAddr);
                }
            }
            for(int i=owChunkPtr[bank]; i<CHUNKS_PER_PAGE; i++){
                write_dram_32(chunkInOwLpnsBuf(bank, LogPageToOffset(newLogLpn), i), INVALID);
            }
            increaseOwLogPagePtr(bank);
            owChunkPtr[bank]=0;
        }
    }
}*/

static void flushOwLogBuffer()
{
    uart_print("bank "); uart_print_int(bank_);
    uart_print(" flushOwLogBuffer\r\n");
    UINT32 newLogLpn = getOWLpn(bank_);
    if (newLogLpn == INVALID)
    {
        uart_print_level_1("ERROR in flushOwLogBuffer: got INVALID lpn\r\n");
        while(1);
    }
    uart_print("new log lpn="); uart_print_int(newLogLpn); uart_print("\r\n");
    UINT32 lbn = LogPageToLogBlk(newLogLpn);
    UINT32 vBlk = get_log_vbn(bank_, lbn);
    UINT32 pageOffset = LogPageToOffset(newLogLpn);
    nand_page_program(bank_, vBlk, pageOffset, OW_LOG_BUF(bank_), RETURN_ON_ISSUE);
    increaseOwCounter(bank_, lbn, pageOffset);
#if MeasureOwEfficiency
    write_dram_32(OwEffBuf(bank_, LogPageToLogBlk(newLogLpn)), read_dram_32(OwEffBuf(bank_, LogPageToLogBlk(newLogLpn))) + SECTORS_PER_PAGE);
#endif
    for(UINT32 i=0; i<CHUNKS_PER_PAGE; i++)
    {
        uart_print("Chunk "); uart_print_int(i); uart_print(" ");
        UINT32 lChunkAddr = ( (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (newLogLpn * CHUNKS_PER_PAGE) + i );
        uart_print("lChunkAddr "); uart_print_int(lChunkAddr); uart_print(" ");
        lChunkAddr = lChunkAddr | StartOwLogLpn;
        uart_print("full "); uart_print_int(lChunkAddr); uart_print("\r\n");
        write_dram_32(chunkInLpnsList(OWCtrl[bank_].lpnsListPtr, LogPageToOffset(newLogLpn), i), owLogBufMeta[bank_].dataLpn[i]);
        if (owLogBufMeta[bank_].dataLpn[i] != INVALID)
        {
            write_dram_32(ChunksMapTable(owLogBufMeta[bank_].dataLpn[i], owLogBufMeta[bank_].chunkIdx[i]), lChunkAddr);
        }
        else
        {
            decrementValidChunks(&heapDataOverwrite, bank_, LogPageToLogBlk(newLogLpn));
        }
    }
    increaseOWLpn(bank_);
}

/*static void flushOwLogBufferToWLog(){
    uart_print("bank "); uart_print_int(bank_);
    uart_print(" flushOwLogBufferToWLog write lpn ");
    UINT32 newLogLpn = get_cur_write_rwlog_lpn(bank_);
    uart_print("new log lpn="); uart_print_int(newLogLpn); uart_print("\r\n");
    UINT32 vBlk = get_log_vbn(bank_, LogPageToLogBlk(newLogLpn));
    UINT32 pageOffset = LogPageToOffset(newLogLpn);
    nand_page_program(bank_, vBlk, pageOffset, OW_LOG_BUF(bank_), RETURN_ON_ISSUE);
    for(int i=0; i<CHUNKS_PER_PAGE; i++) {
        UINT32 lChunkAddr = (newLogLpn * CHUNKS_PER_PAGE) + i;
        write_dram_32(chunkInLpnsBuf(bank_, LogPageToOffset(newLogLpn), i), owLogBufMeta[bank_].dataLpn[i]);
        if (owLogBufMeta[bank_].dataLpn[i] != INVALID) {
            shashtblUpdate(owLogBufMeta[bank_].dataLpn[i], owLogBufMeta[bank_].chunkIdx[i], (bank_ * TOT_LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + lChunkAddr);
        }
    }
    newLogLpn = assign_cur_write_rwlog_lpn(bank_);
    increaseOwLogPagePtr(bank_); // OW Log is full, try to trigger GC
    uart_print_level_1("FLW\r\n");
}*/

static void updateOwChunkPtr()
{
    uart_print("updateOwChunkPtr\r\n");
    owChunkPtr[bank_] = (owChunkPtr[bank_] + 1) % CHUNKS_PER_PAGE;
    uart_print("new owChunkPtr for bank "); uart_print_int(bank_); uart_print(" is "); uart_print_int(owChunkPtr[bank_]); uart_print("\r\n");
    if (owChunkPtr[bank_] == 0)
    {
        flushOwLogBuffer();
    }
}

static void updateOwDramBufMetadata() {
    uart_print("updateOwDramBufMetadata\r\n");
    UINT32 chunkIdx = sectOffset_ / SECTORS_PER_CHUNK;
    owLogBufMeta[bank_].dataLpn[owChunkPtr[bank_]]=lpn_;
    owLogBufMeta[bank_].chunkIdx[owChunkPtr[bank_]]=chunkIdx;
    //shashtblUpdate(lpn_, chunkIdx, ( (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (LogBufLpn * CHUNKS_PER_PAGE) + owChunkPtr[bank_] ) | StartOwLogLpn); // Optimize: already have node, why use hash update???
    write_dram_32(ChunksMapTable(lpn_, chunkIdx), ( (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (LogBufLpn * CHUNKS_PER_PAGE) + owChunkPtr[bank_] ) | StartOwLogLpn);
}

static void chooseNewBank_() {
    /*
    uart_print("chooseNewBank_ ow log: ");
    for (UINT32 tentativeBank = (bank_ + 1) % NUM_BANKS; tentativeBank != bank_; tentativeBank = (tentativeBank + 1) % NUM_BANKS) {
        if (isBankBusy(tentativeBank) == FALSE) {
            bank_ = tentativeBank;
            uart_print("found idle bank="); uart_print_int(bank_); uart_print("\r\n");
            return;
        }
    }
    uart_print("all banks are busy! ");
    */
    bank_ = chooseNewBank();
    //do {
        //bank_ = (bank_ + 1) % NUM_BANKS;
    //} while (bank_ == 1);
    uart_print("bank="); uart_print_int(bank_); uart_print("\r\n");
}

static void initWrite(const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects) {
    uart_print("initWrite\r\n");
    lpn_ = dataLpn;
    sectOffset_ = sectOffset;
    nSects_ = nSects;
    remainingSects_ = nSects;
}

static void manageOldCompletePage() {
    uart_print("manageOldCompletePage\r\n");
    for (int chunkIdx=0; chunkIdx<CHUNKS_PER_PAGE; chunkIdx++){
        uart_print("Chunk "); uart_print_int(chunkIdx); uart_print("\r\n");
        manageOldChunkForCompletePageWrite(chunkIdx);
    }
}

/* Completely invalid every metadata related to the chunk, because no GC can happen before the new page written since it is a complete page write */
static void manageOldChunkForCompletePageWrite(int chunkIdx) {
    uart_print("manageOldChunkForCompletePageWrite\r\n");
    //UINT32 oldChunkAddr = getChunkAddr(node_, chunkIdx);
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
    }
}

/*static void overwriteCompletePage() {
    //uart_print_level_1("12 ");
    uart_print("overwriteCompletePage\r\n");
    chooseNewBank_();
    UINT32 newLogLpn = getOWLpn(bank_);
    manageOldCompletePage(); // Invalidates even if write in ow log, because it's not overwriting but writes to a new complete page to try and keep the whole page together
    if (newLogLpn == INVALID) {
        overwriteCompletePageInWLog();
        increaseOwLogPagePtr(bank_); // OW Log is full, try to trigger GC
    } else {
        overwriteCompletePageInOwLog();
    }
}*/

static void overwriteCompletePageInOwLog() {
#if DetailedOwStats == 1
    uart_print_level_1("-\r\n");
#endif
    uart_print("overwriteCompletePageInOwLog\r\n");
    #if MeasureDetailedOverwrite
    start_interval_measurement(TIMER_CH3, TIMER_PRESCALE_0);
    #endif
    chooseNewBank_();
    manageOldCompletePage();
    UINT32 newLogLpn = getOWLpn(bank_);
    UINT32 lbn = LogPageToLogBlk(newLogLpn);
    UINT32 vBlk = get_log_vbn(bank_, lbn);
    UINT32 pageOffset = LogPageToOffset(newLogLpn);
    nand_page_ptprogram_from_host(bank_, vBlk, pageOffset, 0, SECTORS_PER_PAGE);
    increaseOwCounter(bank_, lbn, pageOffset);
    #if MeasureOwEfficiency
    write_dram_32(OwEffBuf(bank_, LogPageToLogBlk(newLogLpn)), read_dram_32(OwEffBuf(bank_, LogPageToLogBlk(newLogLpn))) + SECTORS_PER_PAGE);
    #endif
    for(UINT32 i=0; i<CHUNKS_PER_PAGE; i++)
    {
        write_dram_32(chunkInLpnsList(OWCtrl[bank_].lpnsListPtr, LogPageToOffset(newLogLpn), i), lpn_);
        write_dram_32(ChunksMapTable(lpn_, i), ( (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (newLogLpn * CHUNKS_PER_PAGE) + i ) | StartOwLogLpn);
    }
    increaseOWLpn(bank_);
    #if MeasureDetailedOverwrite
    UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH3);
    UINT32 nTicks = 0xFFFFFFFF - timerValue;
    uart_print_level_2("OPN0 "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
    #endif
}

/*static void overwriteCompletePageInWLog() {
    //uart_print_level_1("3 ");
    uart_print("writeCompletePageInWLog\r\n");
    UINT32 newLogLpn = getOWLpn(bank_);
    UINT32 vBlk = get_log_vbn(bank_, LogPageToLogBlk(newLogLpn));
    UINT32 pageOffset = LogPageToOffset(newLogLpn);
    nand_page_ptprogram_from_host (bank_, vBlk, pageOffset, 0, SECTORS_PER_PAGE);
    for(UINT32 i=0; i<CHUNKS_PER_PAGE; i++)
    {
        write_dram_32(chunkInLpnsBuf(bank_, LogPageToOffset(newLogLpn), i), lpn_);
        shashtblUpdate(lpn_, i, ( (bank_ * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (newLogLpn * CHUNKS_PER_PAGE) + i ) | StartOwLogLpn);
    }
    increaseOWLpn(bank_);
    uart_print_level_1("WPWLOG\r\n");
}*/

static void overwriteCompletePageOld()
{
    uart_print("overwriteCompletePageOld\r\n");
    if (oldVersionIsInOwLogInOrder())
    {
        overwritePageOldInOrderInOwLog();
    }
    else
    {
        overwriteCompletePageInOwLog();
    }
}

static UINT8 oldVersionIsInOwLogInOrder()
{
    uart_print("oldVersionIsInOwLogInOrder\r\n");
    UINT32 firstChunk = sectOffset_ / SECTORS_PER_CHUNK;
    UINT32 lastChunk = (sectOffset_ + nSects_ - 1) / SECTORS_PER_CHUNK;
    UINT32 chunk = read_dram_32(ChunksMapTable(lpn_, firstChunk));
    if (findChunkLocation(chunk) == FlashOwLog)
    {
        chunk = chunk & 0x7FFFFFFF;
        if (ChunkToChunkOffset(chunk) != firstChunk) return FALSE;
        UINT32 chunkOffsetInBank = ChunkToChunkOffsetInBank(chunk);
        UINT32 bank = ChunkToBank(chunk);
        for (int i = firstChunk+1; i<=lastChunk; i++)
        {
            chunk = read_dram_32(ChunksMapTable(lpn_, i));
            if (findChunkLocation(chunk) == FlashOwLog)
            {
                chunk = chunk & 0x7FFFFFFF;
                if (ChunkToBank(chunk) != bank)
                {
                    uart_print("Not in order\r\n");
                    return FALSE;
                }
                if (ChunkToChunkOffsetInBank(chunk) != chunkOffsetInBank+1)
                {
                    uart_print("Not in order\r\n");
                    return FALSE;
                }
                chunkOffsetInBank++;
            }
            else
            {
                uart_print("Not in order\r\n");
                return FALSE;
            }
        }
        uart_print("In order\r\n");
        return TRUE;
    }
    else
    {
        uart_print("Not in order\r\n");
        return FALSE;
    }
}

static void overwritePageOldInOrderInOwLog()
{
    #if MeasureDetailedOverwrite
    start_interval_measurement(TIMER_CH3, TIMER_PRESCALE_0);
    #endif
    uart_print("overwritePageOldInOrderInOwLog\r\n");
    UINT32 firstChunk = sectOffset_ / SECTORS_PER_CHUNK;
    UINT32 chunk = read_dram_32(ChunksMapTable(lpn_, firstChunk));
    chunk = chunk & 0x7FFFFFFF;
    UINT32 bank = ChunkToBank(chunk);
    UINT32 lbn = ChunkToLbn(chunk);
    UINT32 vBlk = get_log_vbn(bank, lbn);
    UINT32 pageOffset = ChunkToPageOffset(chunk);
    if(readOwCounter(bank, lbn, pageOffset) < OwLimit)
    {
#if DetailedOwStats == 1
        uart_print_level_1("*\r\n");
#endif
        uart_print("Can overwrite in place\r\n");
        nand_page_ptprogram_from_host(bank, vBlk, pageOffset, sectOffset_, nSects_);
        increaseOwCounter(bank, lbn, pageOffset);
        #if MeasureOwEfficiency
        write_dram_32(OwEffBuf(bank_, ChunkToLbn(chunk)), read_dram_32(OwEffBuf(bank_, ChunkToLbn(chunk))) + nSects_);
        #endif
    }
    else
    {
        uart_print("Exceeding limit, must find a new page\r\n");
        if (remainingSects_ == SECTORS_PER_PAGE)
        {
            overwriteCompletePageInOwLog();
        }
        else
        {
            syncWithWriteLimit();
            UINT16 invalidChunksToDecrement = 0;
            chooseNewBank_();
            while(remainingSects_)
            {
                invalidChunksToDecrement++;
                UINT32 nSectsToWrite = (((sectOffset_ % SECTORS_PER_CHUNK) + remainingSects_) < SECTORS_PER_CHUNK) ? remainingSects_ :
                                                                                                                     (SECTORS_PER_CHUNK - (sectOffset_ % SECTORS_PER_CHUNK));
                if(nSectsToWrite == SECTORS_PER_CHUNK)
                {
                    uart_print("Copy chunk "); uart_print_int( (sectOffset_ % SECTORS_PER_CHUNK) / SECTORS_PER_CHUNK); uart_print(" to OW_LOG_BUF\r\n");
                    overwriteCompleteChunkNew();
                    updateOwDramBufMetadata();
                    updateOwChunkPtr();
                }
                else
                {
                    UINT32 chunkIdx = sectOffset_ / SECTORS_PER_CHUNK;
                    chunk = read_dram_32(ChunksMapTable(lpn_, chunkIdx));
                    chunk = chunk & 0x7FFFFFFF;
                    overwritePartialChunkWhenOldChunkIsInExhaustedOWLog(nSectsToWrite, chunk);
                    updateOwDramBufMetadata();
                    updateOwChunkPtr();
                }
                sectOffset_ += nSectsToWrite;
                remainingSects_ -= nSectsToWrite;
            }
            decrementValidChunksByN(&heapDataOverwrite, bank, lbn, invalidChunksToDecrement);
            g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
            SETREG (BM_STACK_WRSET, g_ftl_write_buf_id);
            SETREG (BM_STACK_RESET, 0x01);
        }
    }
    #if MeasureDetailedOverwrite
    UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH3);
    UINT32 nTicks = 0xFFFFFFFF - timerValue;
    uart_print_level_2("OPIO "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
    #endif
}

/*
static void overwritePartialPageNew()
{
    //uart_print_level_1("17 ");
    uart_print("overwritePartialPageNew\r\n");
    while (remainingSects_ != 0)
    {
        overwriteChunkNew();
    }
    // SATA buffer management
    g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
    SETREG (BM_STACK_WRSET, g_ftl_write_buf_id);
    SETREG (BM_STACK_RESET, 0x01);
}
*/

static void overwritePartialPageOld() {
    //uart_print_level_1("18 ");
    uart_print("overwritePartialPageOld\r\n");
    if (oldVersionIsInOwLogInOrder()) {
        overwritePageOldInOrderInOwLog();
    } else {
        overwritePartialPageOldNotInOrder();
    }
}

static void overwritePartialPageOldNotInOrder()
{
    //uart_print_level_1("19 ");
    uart_print("overwritePartialPageOldNotInOrder\r\n");
    while (remainingSects_ != 0)
    {
        UINT32 chunkIdx = sectOffset_ / SECTORS_PER_CHUNK;
        //UINT32 chunkAddr = getChunkAddr(node_, chunkIdx);
        UINT32 chunkAddr = read_dram_32(ChunksMapTable(lpn_, chunkIdx));
        #if MeasureDetailedOverwrite
        start_interval_measurement(TIMER_CH3, TIMER_PRESCALE_0);
        #endif
        switch (findChunkLocation(chunkAddr))
        {
            case Invalid:
            {
                uart_print(" invalid\r\n");
                overwriteChunkNew();
                break;
            }
            case FlashWLog:
            {
                uart_print(" in w log\r\n");
                overwriteChunkOldInWLog(chunkAddr);
                #if MeasureDetailedOverwrite
                UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH3);
                UINT32 nTicks = 0xFFFFFFFF - timerValue;
                uart_print_level_2("OCO3 "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
                #endif
                break;
            }
            case DRAMWLog:
            {
                uart_print(" in w DRAM buf\r\n");
                overwriteChunkOldInWBuf(chunkAddr);
                #if MeasureDetailedOverwrite
                UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH3);
                UINT32 nTicks = 0xFFFFFFFF - timerValue;
                uart_print_level_2("OCO2 "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
                #endif
                break;
            }
            case DRAMOwLog:
            {
                uart_print(" in ow DRAM buf\r\n");
                chunkAddr = chunkAddr & 0x7FFFFFFF;
                overwriteChunkOldInOwBuf(chunkAddr);
                #if MeasureDetailedOverwrite
                UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH3);
                UINT32 nTicks = 0xFFFFFFFF - timerValue;
                uart_print_level_2("OCO0 "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
                #endif
                break;
            }
            case FlashOwLog:
            {
                uart_print(" in ow log\r\n");
                chunkAddr = chunkAddr & 0x7FFFFFFF;
                overwriteChunkOldInOwLog(chunkAddr);
                #if MeasureDetailedOverwrite
                UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH3);
                UINT32 nTicks = 0xFFFFFFFF - timerValue;
                uart_print_level_2("OCO1 "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
                #endif
                break;
            }
        }
    }
    // SATA buffer management
    g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS;
    SETREG (BM_STACK_WRSET, g_ftl_write_buf_id);
    SETREG (BM_STACK_RESET, 0x01);
}


static void overwriteChunkOldInOwBuf(UINT32 chunkAddr) {
    //uart_print_level_1("21 ");
    uart_print("overwriteChunkOldInOwBuf\r\n");
    UINT32 nSectsToWrite = (((sectOffset_ % SECTORS_PER_CHUNK) + remainingSects_) < SECTORS_PER_CHUNK) ?     remainingSects_ :
                                                            (SECTORS_PER_CHUNK - (sectOffset_ % SECTORS_PER_CHUNK));
    UINT32 startOffset = (sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR;
    UINT32 oldBank = ChunkToBank(chunkAddr);
    UINT32 oldSectOffset = ChunkToSectOffset(chunkAddr);
    UINT32 startBufAddr = OW_LOG_BUF(oldBank)+(oldSectOffset*BYTES_PER_SECTOR)+startOffset; // location of old chunk, overwrite in place
    UINT32 startSataAddr = WR_BUF_PTR(g_ftl_write_buf_id) + (sectOffset_*BYTES_PER_SECTOR);
    mem_copy(startBufAddr, startSataAddr, nSectsToWrite*BYTES_PER_SECTOR);
    sectOffset_ += nSectsToWrite;
    remainingSects_ -= nSectsToWrite;
    #if MeasureDramAbsorb
    uart_print_level_1("WRDRAM "); uart_print_level_1_int(nSectsToWrite); uart_print_level_1("\r\n");
    #endif
}

static void overwriteChunkOldInOwLog(UINT32 chunkAddr)
{
    //uart_print_level_1("22 ");
    uart_print("overwriteChunkOldInOwLog\r\n");
    UINT32 nSectsToWrite = (((sectOffset_ % SECTORS_PER_CHUNK) + remainingSects_) < SECTORS_PER_CHUNK) ?     remainingSects_ :
                                                            (SECTORS_PER_CHUNK - (sectOffset_ % SECTORS_PER_CHUNK));
    UINT32 bank = ChunkToBank(chunkAddr);
    UINT32 lbn = ChunkToLbn(chunkAddr);
    UINT32 vbn = get_log_vbn(bank, lbn);
    UINT32 pageOffset = ChunkToPageOffset(chunkAddr);
    if (readOwCounter(bank, lbn, pageOffset) < OwLimit)
    { // Can overwrite in place
        UINT32 sectOffset = ChunkToSectOffset(chunkAddr) + (sectOffset_ % SECTORS_PER_CHUNK);
        //UINT32 src = WR_BUF_PTR(g_ftl_write_buf_id) + (sectOffset_ * BYTES_PER_SECTOR) - (sectOffset * BYTES_PER_SECTOR); // startBuf + srcOffset - dstOffset
        if (lastBankUsingFtlBuf1 != INVALID)
        {
            waitBusyBank(lastBankUsingFtlBuf1);
        }
        mem_copy(FTL_BUF(1)+(sectOffset_*BYTES_PER_SECTOR), WR_BUF_PTR(g_ftl_write_buf_id) + (sectOffset_*BYTES_PER_SECTOR), nSectsToWrite*BYTES_PER_SECTOR);
        UINT32 src = FTL_BUF(1) + (sectOffset_ * BYTES_PER_SECTOR) - (sectOffset * BYTES_PER_SECTOR); // startBuf + srcOffset - dstOffset
        lastBankUsingFtlBuf1 = bank;
        nand_page_ptprogram(bank, vbn, pageOffset, sectOffset, nSectsToWrite, src, RETURN_ON_ISSUE);
        increaseOwCounter(bank, lbn, pageOffset);
    }
    else
    { // Need a new page
        if (nSectsToWrite == SECTORS_PER_CHUNK)
        { // Write chunk in ow log and decrease valid chunks in previous ow blk
            decrementValidChunks(&heapDataOverwrite, bank, lbn);
            overwriteCompleteChunkNew();
        }
        else
        { // Must read old chunk and update in ow log
            decrementValidChunks(&heapDataOverwrite, bank, lbn);
            overwritePartialChunkWhenOldChunkIsInExhaustedOWLog(nSectsToWrite, chunkAddr);
        }
        updateOwDramBufMetadata();
        updateOwChunkPtr();
    }
    #if MeasureOwEfficiency
    write_dram_32(OwEffBuf(bank_, ChunkToLbn(chunkAddr)), read_dram_32(OwEffBuf(bank_, ChunkToLbn(chunkAddr))) + nSectsToWrite);
    #endif
    sectOffset_ += nSectsToWrite;
    remainingSects_ -= nSectsToWrite;
}

static void overwriteChunkOldInWBuf(UINT32 chunkAddr) {
    //uart_print_level_1("23 ");
    /* Question: is it better to copy the ow buf or to overwrite in place in w buf?
      * Current implementation: copy to the ow buf, because probably will be overwritten again in the future.
      */
    uart_print("overwriteChunkOldInWBuf\r\n");
    chooseNewBank_();
    UINT32 nSectsToWrite = (((sectOffset_ % SECTORS_PER_CHUNK) + remainingSects_) < SECTORS_PER_CHUNK) ?     remainingSects_ :
                                                            (SECTORS_PER_CHUNK - (sectOffset_ % SECTORS_PER_CHUNK));
    UINT32 srcBank = ChunkToBank(chunkAddr);
    UINT32 srcChunkIdx = ChunkToChunkOffset(chunkAddr);
    UINT32 startOffsetOverwrite = (sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR;
    UINT32 endOffsetOverwrite =  ((sectOffset_ % SECTORS_PER_CHUNK)  + nSectsToWrite) * BYTES_PER_SECTOR;
    UINT32 wBufAddr = LOG_BUF(srcBank) + srcChunkIdx*BYTES_PER_CHUNK;
    UINT32 owBufAddr = OW_LOG_BUF(bank_) + owChunkPtr[bank_] * BYTES_PER_CHUNK;
    UINT32 sataBufAddr = WR_BUF_PTR(g_ftl_write_buf_id)+(sectOffset_*BYTES_PER_SECTOR);
    UINT32 leftHoleSize = startOffsetOverwrite;
    UINT32 rightHoleSize =  BYTES_PER_CHUNK - endOffsetOverwrite;
    waitBusyBank(bank_); // (Fabio) probably should wait. In contrast to overwriteChunkOldInOwBuf, here we are writing to a new chunk in ow buf, thus it might be that a previous operation involving ow buf is in flight
    mem_copy(owBufAddr + leftHoleSize, sataBufAddr, nSectsToWrite*BYTES_PER_SECTOR);
    if(leftHoleSize > 0) {
        uart_print("copy left hole\r\n");
        mem_copy(owBufAddr, wBufAddr, leftHoleSize); // copy left hole
    }
    if(rightHoleSize > 0) {
        uart_print("copy right hole\r\n");
        mem_copy(owBufAddr+endOffsetOverwrite, wBufAddr+endOffsetOverwrite, rightHoleSize); // copy right hole
    }
    logBufMeta[srcBank].dataLpn[srcChunkIdx]=INVALID; // invalidate in w buf
    updateOwDramBufMetadata();
    updateOwChunkPtr();
    sectOffset_ += nSectsToWrite;
    remainingSects_ -= nSectsToWrite;
    #if MeasureDramAbsorb
    uart_print_level_1("OWDRAM "); uart_print_level_1_int(nSectsToWrite); uart_print_level_1("\r\n");
    #endif
}

static void overwriteChunkOldInWLog(UINT32 chunkAddr) {
    //uart_print_level_1("24 ");
    uart_print("overwriteChunkOldInWLog\r\n");
    UINT32 nSectsToWrite = (((sectOffset_ % SECTORS_PER_CHUNK) + remainingSects_) < SECTORS_PER_CHUNK) ?     remainingSects_ :
                                                            (SECTORS_PER_CHUNK - (sectOffset_ % SECTORS_PER_CHUNK));
    if (nSectsToWrite != SECTORS_PER_CHUNK) {
        // Copy holes from w log
        overwritePartialChunkWhenOldChunkIsInWLog(nSectsToWrite, chunkAddr);
    } else {
       // Ignore version in w log
        overwriteCompleteChunkNew();
    }
    UINT32 oldBank = ChunkToBank(chunkAddr);
    UINT32 oldLpn = ChunkToLbn(chunkAddr);
    decrementValidChunks(&heapDataWrite, oldBank, oldLpn); // decrement blk with previous copy
    updateOwDramBufMetadata();
    updateOwChunkPtr();
    sectOffset_ += nSectsToWrite;
    remainingSects_ -= nSectsToWrite;
}

static void overwritePartialChunkWhenOldChunkIsInWLog(UINT32 nSectsToWrite, UINT32 oldChunkAddr) {
    //uart_print_level_1("24a ");
    uart_print("overwritePartialChunkWhenOldChunkIsInWLog\r\n");
    chooseNewBank_();
    UINT32 src = WR_BUF_PTR(g_ftl_write_buf_id)+((sectOffset_ / SECTORS_PER_CHUNK)*BYTES_PER_CHUNK);
    UINT32 dstOwBufChunkStart = OW_LOG_BUF(bank_) + (owChunkPtr[bank_] * BYTES_PER_CHUNK); // base address of the destination chunk
    UINT32 startOffsetWrite = (sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR;
    // Old Chunk Location
    UINT32 oldBank = ChunkToBank(oldChunkAddr);
    UINT32 oldVbn = get_log_vbn(oldBank, ChunkToLbn(oldChunkAddr));
    UINT32 oldPageOffset = ChunkToPageOffset(oldChunkAddr);
    UINT32 oldSectOffset = ChunkToSectOffset(oldChunkAddr);
    // Offsets
    UINT32 dstByteOffset = owChunkPtr[bank_] * BYTES_PER_CHUNK;
    UINT32 srcByteOffset = ChunkToChunkOffset(oldChunkAddr) * BYTES_PER_CHUNK;
    UINT32 alignedWBufAddr = OW_LOG_BUF(bank_) + dstByteOffset - srcByteOffset;
    waitBusyBank(bank_);
    nand_page_ptread(oldBank, oldVbn, oldPageOffset, oldSectOffset, SECTORS_PER_CHUNK, alignedWBufAddr, RETURN_WHEN_DONE);
    mem_copy(dstOwBufChunkStart + startOffsetWrite, src + startOffsetWrite, nSectsToWrite*BYTES_PER_SECTOR);
}

static void overwriteChunkNew()
{
    //uart_print_level_1("25 ");
    uart_print("overwriteChunkNew\r\n");
    UINT32 nSectsToWrite = (((sectOffset_ % SECTORS_PER_CHUNK) + remainingSects_) < SECTORS_PER_CHUNK) ? remainingSects_ : (SECTORS_PER_CHUNK - (sectOffset_ % SECTORS_PER_CHUNK));

#if MeasureDetailedOverwrite
    start_interval_measurement(TIMER_CH3, TIMER_PRESCALE_0);
#endif

    if ((sectOffset_ % SECTORS_PER_CHUNK == 0) && (((sectOffset_ + nSectsToWrite) % SECTORS_PER_CHUNK) == 0))
    {
        overwriteCompleteChunkNew();
        #if MeasureDetailedOverwrite
        UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH3);
        UINT32 nTicks = 0xFFFFFFFF - timerValue;
        uart_print_level_2("OCN0 "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
        #endif
    }
    else
    {
        overwritePartialChunkNew(nSectsToWrite);
        #if MeasureDetailedOverwrite
        UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH3);
        UINT32 nTicks = 0xFFFFFFFF - timerValue;
        uart_print_level_2("OCN1 "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
        #endif
    }
    updateOwDramBufMetadata();
    updateOwChunkPtr();
    sectOffset_ += nSectsToWrite;
    remainingSects_ -= nSectsToWrite;
}

static void overwriteCompleteChunkNew() {
    //uart_print_level_1("26 ");
    uart_print("overwriteCompleteChunkNew\r\n");
    chooseNewBank_();
    UINT32 src = WR_BUF_PTR(g_ftl_write_buf_id)+(sectOffset_*BYTES_PER_SECTOR);
    UINT32 dst = OW_LOG_BUF(bank_)+(owChunkPtr[bank_]*BYTES_PER_CHUNK); // base address of the destination chunk
    waitBusyBank(bank_);
    mem_copy(dst, src, BYTES_PER_CHUNK);
}

static void overwritePartialChunkNew(UINT32 nSectsToWrite) {
    //uart_print_level_1("27 ");
    uart_print("overwritePartialChunkNew\r\n");
    chooseNewBank_();
    UINT32 src = WR_BUF_PTR(g_ftl_write_buf_id)+(sectOffset_*BYTES_PER_SECTOR);
    UINT32 chunkBufStartAddr = OW_LOG_BUF(bank_)+(owChunkPtr[bank_]*BYTES_PER_CHUNK); // base address of the destination chunk
    waitBusyBank(bank_);
    mem_set_dram (chunkBufStartAddr, 0xFFFFFFFF, BYTES_PER_CHUNK);
    UINT32 dst = chunkBufStartAddr + (sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR;
    mem_copy(dst, src, nSectsToWrite * BYTES_PER_SECTOR);
}



//#ifndef callPM
//static inline void callPM(const UINT32 bank){
    //uart_print("callPM\r\n");

    //if(g_misc_meta[bank].rwlog_free_blk_cnt < 2){
        //#ifdef uart_print(x)
        //#undef uart_print(x)
        //uart_print("X ");
        //#define uart_print(x)
        //#endif
        //progressiveMergeMeasured(bank, FlashOpsEmergency);
        ////garbageCollectLog(bank);
    //}
    //else{
        //if(g_misc_meta[bank].rwlog_free_blk_cnt < RedFreeBlksThreshold){
            //#ifdef uart_print(x)
            //#undef uart_print(x)
            //uart_print("R ");
            //#define uart_print(x)
            //#endif
            //progressiveMergeMeasured(bank, FlashOpsRed);
        //}
        //else{
            //if(g_misc_meta[bank].rwlog_free_blk_cnt < YellowFreeBlksThreshold){
                //yellowMergeCnt[bank]++;
                //if (yellowMergeCnt[bank]%4==0){
                    //#ifdef uart_print(x)
                    //#undef uart_print(x)
                    //uart_print("Y ");
                    //#define uart_print(x)
                    //#endif
                    //progressiveMergeMeasured(bank, FlashOpsYellow);
                    //yellowMergeCnt[bank]=0;
                //}
            //}
            //else{
            //for(UINT32 pmBank=(bank+NUM_BANKS-4)%NUM_BANKS; pmBank<bank; pmBank=(pmBank+1)%NUM_BANKS){
                //if(g_misc_meta[bank].rwlog_free_blk_cnt < GreenFreeBlksThreshold){
                    //UINT32 rbank = REAL_BANK(bank);
                    //if(_BSP_FSM(rbank) == BANK_IDLE){
                        //greenMergeCnt[bank]++;
                        //if (greenMergeCnt[bank]%8==0){
                            //#ifdef uart_print(x)
                            //#undef uart_print(x)
                            //uart_print("G ");
                            //#define uart_print(x)
                            //#endif
                            //progressiveMergeMeasured(bank, FlashOpsGreen);
                            //greenMergeCnt[bank]=0;
                        //}
                    //}
                //}
            //}
            //}
        //}
    //}
//}
//
static void overwritePartialChunkWhenOldChunkIsInExhaustedOWLog(UINT32 nSectsToWrite, UINT32 oldChunkAddr)
{
    uart_print("overwritePartialChunkWhenOldChunkIsInExhaustedOWLog\r\n");
    chooseNewBank_();
    UINT32 src = WR_BUF_PTR(g_ftl_write_buf_id)+((sectOffset_ / SECTORS_PER_CHUNK)*BYTES_PER_CHUNK);
    UINT32 dstOwBufChunkStart = OW_LOG_BUF(bank_) + (owChunkPtr[bank_] * BYTES_PER_CHUNK); // base address of the destination chunk
    UINT32 startOffsetWrite = (sectOffset_ % SECTORS_PER_CHUNK) * BYTES_PER_SECTOR;
    // Old Chunk Location
    UINT32 oldBank = ChunkToBank(oldChunkAddr);
    UINT32 oldVbn = get_log_vbn(oldBank, ChunkToLbn(oldChunkAddr));
    UINT32 oldPageOffset = ChunkToPageOffset(oldChunkAddr);
    UINT32 oldSectOffset = ChunkToSectOffset(oldChunkAddr);
    // Offsets
    UINT32 dstByteOffset = owChunkPtr[bank_] * BYTES_PER_CHUNK;
    UINT32 srcByteOffset = ChunkToChunkOffset(oldChunkAddr) * BYTES_PER_CHUNK;
    UINT32 alignedWBufAddr = OW_LOG_BUF(bank_) + dstByteOffset - srcByteOffset;
    waitBusyBank(bank_);
    nand_page_ptread(oldBank, oldVbn, oldPageOffset, oldSectOffset, SECTORS_PER_CHUNK, alignedWBufAddr, RETURN_WHEN_DONE);
    mem_copy(dstOwBufChunkStart + startOffsetWrite, src + startOffsetWrite, nSectsToWrite*BYTES_PER_SECTOR);
}
#endif

