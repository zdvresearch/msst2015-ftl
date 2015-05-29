#include "log.h"
#include "ftl_parameters.h"
#include "dram_layout.h"
#include "ftl_metadata.h"
#include "garbage_collection.h"  //TODO: this probably shouldn't be here
#include "heap.h"
#include "cleanList.h"
#include "flash.h" // Flash operations and flags

#define Write_log_bmt(bank, lbn, vblock) write_dram_16 (LOG_BMT_ADDR + ((bank * LOG_BLK_PER_BANK + lbn) * sizeof (UINT16)), vblock)
#define Read_log_bmt(bank, lbn) read_dram_16 (LOG_BMT_ADDR + ((bank * LOG_BLK_PER_BANK + lbn) * sizeof (UINT16)))

#define RWToOWSwapThreshold         (LOG_BLK_PER_BANK / 2)      // When RW blocks are below this threshold always swap

#if NO_FLASH_OPS
#define nand_page_ptread(a, b, c, d, e, f, g)
#define nand_page_read(a, b, c, d)
#define nand_page_program(a, b, c, d)
#define nand_page_ptprogram(a, b, c, d, e, f, g)
#define nand_page_ptprogram_from_host (a, b, c, d, e)     g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS; \
                            SETREG (BM_STACK_WRSET, g_ftl_write_buf_id); \
                            SETREG (BM_STACK_RESET, 0x01);
#define nand_page_copyback(a, b, c, d, e)
//#define nand_block_erase(a, b)
//#define nand_block_erase_sync (a, b)
#endif


//#define uart_print(x)
//#define uart_print_int(x)
//#define garbageCollectLog(x)

//Private functions

// search lpn of target valid page from log page mapping table
// using static hash table
// return: log_lpn

/*
hashnode_ptr search_valid_in_log_area (UINT32 const bank, UINT32 const dataLpn)
{
    hashnode_ptr logLpnNode = shashtblGetNode(dataLpn);
    return logLpnNode;
}
*/

// set log vbn to log block mapping table
void set_log_vbn (UINT32 const bank, UINT32 const log_lbn, UINT32 const vblock)
{
    uart_print("set_log_vbn(bank="); uart_print_int(bank);
    uart_print(", log_lbn="); uart_print_int(log_lbn);
    uart_print(", vblock="); uart_print_int(vblock);
    uart_print(")\r\n");
    Write_log_bmt(bank, log_lbn, vblock);
    //write_dram_16 (LOG_BMT_ADDR + ((bank * LOG_BLK_PER_BANK + log_lbn) * sizeof (UINT16)), vblock);
}

// get log vbn from log block mapping table
UINT32 get_log_vbn (UINT32 const bank, UINT32 const logLbn)
{
    uart_printf("get_log_vbn(bank=%d, log_lbn=%d)", bank, log_lbn);
    uart_printf("\treading BMT: log_lbn=%d=>vbn=%d", log_lbn, Read_log_bmt(bank, log_lbn));
    if (logLbn < LOG_BLK_PER_BANK)
        return Read_log_bmt(bank, logLbn);
    return INVALID;
}

void initLog(){
    uart_print("Initializing Write Log Space...\r\n");
    uart_print("Initializing clean list...");
    //testCleanList();
    cleanListInit(&cleanListDataWrite, CleanList(0), LOG_BLK_PER_BANK);
    uart_print("done\r\n");
    for(int bank=0; bank<NUM_BANKS; bank++)
    {
        for(int lbn=0; lbn<LOG_BLK_PER_BANK; lbn++)
        {
            cleanListPush(&cleanListDataWrite, bank, lbn);
        }
        //UINT32 newLogLbn = cleanListPop(&cleanListDataWrite, bank);
        //set_cur_write_rwlog_lpn(bank, (newLogLbn * PAGES_PER_BLK));
        //RWCtrl[bank].logLpn = newLogLbn * PAGES_PER_BLK;
        RWCtrl[bank].logLpn = INVALID;
        RWCtrl[bank].lpnsListPtr = LPNS_BUF_BASE_1(bank);
        RWCtrl[bank].isOwBlk = FALSE;
        //newLogLbn = cleanListPop(&cleanListDataWrite, bank);
        //SWCtrl[bank].logLpn = newLogLbn * PAGES_PER_BLK;
        SWCtrl[bank].logLpn = INVALID;
        SWCtrl[bank].nextDataLpn = INVALID;
        SWCtrl[bank].lpnsListPtr = LPNS_BUF_BASE_2(bank);


        chunkPtr[bank]=0;
#if Overwrite
        owChunkPtr[bank]=0;
        OWCtrl[bank].logLpn = INVALID;
        OWCtrl[bank].lpnsListPtr = LPNS_BUF_BASE_3(bank);
#endif
        gcNestLevel[bank]=0;
    }
}

UINT32 getRWLpn(const UINT32 bank)
{
    uart_print("getRWLpn bank="); uart_print_int(bank); uart_print(":");
    UINT32 lpn = RWCtrl[bank].logLpn;
    if (lpn != INVALID)
    {
        uart_print_int(lpn); uart_print("\r\n");
        return lpn;
    }
    else
    {
        int count=0;
        uart_print("getRWLpn may need to GC to get a new blk\r\n");
        //uart_print_level_1("Get RW Lpn\r\n");
        //cleanListDump(&cleanListDataWrite, bank);
        if(cleanListSize(&cleanListDataWrite, bank) <= 1)
        {
            count++;
            if(count > 10)
            {
                uart_print_level_1("Warning in getRWLpn: could not free a block after 10 GC\r\n");
                while(1);
            }
#if Overwrite
            UINT32 nValidChunksFromHeap = getVictimValidPagesNumber(&heapDataWrite, bank);
            UINT32 nValidChunksFromHeapOW = getVictimValidPagesNumber(&heapDataOverwrite, bank);
            if (nValidChunksFromHeapOW < 504)
            {
                uart_print_level_1("ERROR: in getRWLpn on OW blk found less than 504 valid pages, that should not be possible\r\n");
                uart_print_level_1("Found "); uart_print_level_1_int(nValidChunksFromHeap); uart_print_level_1(" valid chunks\r\n");
                while(1);
            }
            nValidChunksFromHeapOW-=504;  // Adjust value considering that all high pages will be found invalid, and they are 63, hence the - 504
            if (nValidChunksFromHeapOW < nValidChunksFromHeap)
            {   // There are no full RW/SW blks, or it is convenient to gc OW blocks
                sealOWBlk(bank);
            }
            //if (heapDataWrite.nElInHeap[bank]<RWToOWSwapThreshold)
            //{
                //sealOWBlk(bank);
            //}
            else
            {
#endif
                if(SWCtrl[bank].logLpn != INVALID)
                { // Switch SWBlock to RW Block
                    uart_print("Swap RW and SW blocks\r\n");
                    RWCtrl[bank].logLpn = SWCtrl[bank].logLpn;
                    UINT32 tmpPtr = RWCtrl[bank].lpnsListPtr;
                    RWCtrl[bank].lpnsListPtr = SWCtrl[bank].lpnsListPtr;
                    SWCtrl[bank].logLpn = INVALID;
                    SWCtrl[bank].nextDataLpn = INVALID;
                    SWCtrl[bank].lpnsListPtr = tmpPtr;
                }
                else
                {
                    UINT32 lbn = cleanListPop(&cleanListDataWrite, bank); // Note: Page mapped GC needs a new logBlk to copy valid chunks from victim logBlk
                    RWCtrl[bank].logLpn = lbn * PAGES_PER_BLK;
                    RWCtrl[bank].isOwBlk = FALSE;
                    #if MeasureGc
                    start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_2);
                    int nValidChunks = garbageCollectLog(bank);
                    UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH1);
                    UINT32 nTicks = 0xFFFFFFFF - timerValue;
                    uart_print_level_2("GCW "); uart_print_level_2_int(bank);
                    uart_print_level_2(" "); uart_print_level_2_int(nTicks);
                    uart_print_level_2(" "); uart_print_level_2_int(nValidChunks);
                    uart_print_level_2("\r\n");
                    #else
                    garbageCollectLog(bank);
                    #endif
                }
#if Overwrite
            }
#endif
        }
        else
        {
            UINT32 lbn = cleanListPop(&cleanListDataWrite, bank);
            RWCtrl[bank].logLpn = lbn * PAGES_PER_BLK;
            RWCtrl[bank].isOwBlk = FALSE;
        }
        uart_print("Finished getRWLpn bank="); uart_print_int(bank); uart_print(":");
        uart_print(" returning "); uart_print_int(RWCtrl[bank].logLpn); uart_print("\r\n");
        //cleanListDump(&cleanListDataWrite, bank);
        return RWCtrl[bank].logLpn;
    }
}

void increaseRWLpn (UINT32 const bank)
{
    //uart_print_level_1("29 ");
    uart_print("increaseRWLpn bank "); uart_print_int(bank); uart_print("\r\n");
    UINT32 lpn = getRWLpn(bank);
    if (lpn == INVALID)
    {
        uart_print_level_1("Error in increaseRWLpn: trying to increase RW lpn when there's no RW block allocated!\r\n");
        while(1);
    }
    uart_print("Current log lpn: "); uart_print_int(lpn); uart_print("\r\n");
    if (LogPageToOffset(lpn) == UsedPagesPerLogBlk-1)
    { // current rw log block is full
        UINT32 lbn = get_log_lbn(lpn);
#if NO_FLASH_OPS
#undef nand_page_ptprogram(a, b, c, d, e, f, g)
#endif
        uart_print("Block full\r\n");
        nand_page_ptprogram(bank, get_log_vbn(bank, lbn), PAGES_PER_BLK - 1, 0, (CHUNK_ADDR_BYTES * CHUNKS_PER_LOG_BLK + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR, RWCtrl[bank].lpnsListPtr, RETURN_WHEN_DONE);
#if NO_FLASH_OPS
#define nand_page_ptprogram(a, b, c, d, e, f, g)
#endif
        mem_set_dram(RWCtrl[bank].lpnsListPtr, INVALID, (CHUNKS_PER_BLK * CHUNK_ADDR_BYTES));
        insertBlkInHeap(&heapDataWrite, bank, lbn);
        //uart_print_level_1("New RW Blk\r\n");
        //cleanListDump(&cleanListDataWrite, bank);
        if(cleanListSize(&cleanListDataWrite, bank) <= 1)
        {
#if Overwrite
            UINT32 nValidChunksFromHeap = getVictimValidPagesNumber(&heapDataWrite, bank);
            UINT32 nValidChunksFromHeapOW = getVictimValidPagesNumber(&heapDataOverwrite, bank);
            if (nValidChunksFromHeapOW < 504)
            {
                uart_print_level_1("ERROR: in getRWLpn on OW blk found less than 504 valid pages, that should not be possible\r\n");
                uart_print_level_1("Found "); uart_print_level_1_int(nValidChunksFromHeap); uart_print_level_1(" valid chunks\r\n");
                while(1);
            }
            nValidChunksFromHeapOW-=504;  // Adjust value considering that all high pages will be found invalid, and they are 63, hence the - 504
            if (nValidChunksFromHeapOW < nValidChunksFromHeap)
            {   // There are no full RW/SW blks, or it is convenient to gc OW blocks
                sealOWBlk(bank);
            }
            //if (heapDataWrite.nElInHeap[bank]<RWToOWSwapThreshold)
            //{
                //sealOWBlk(bank);
            //}
            else
            {
#endif
                if(SWCtrl[bank].logLpn != INVALID)
                { // Switch SWBlock to RW Block
                    uart_print("Swapping RW and SW blocks\r\n");
                    RWCtrl[bank].logLpn = SWCtrl[bank].logLpn;
                    UINT32 tmpPtr = RWCtrl[bank].lpnsListPtr;
                    RWCtrl[bank].lpnsListPtr = SWCtrl[bank].lpnsListPtr;
                    SWCtrl[bank].logLpn = INVALID;
                    SWCtrl[bank].nextDataLpn = INVALID;
                    SWCtrl[bank].lpnsListPtr = tmpPtr;
                }
                else
                {
                    lbn = cleanListPop(&cleanListDataWrite, bank); // Note: Page mapped GC needs a new logBlk to copy valid chunks from victim logBlk
                    RWCtrl[bank].logLpn = lbn * PAGES_PER_BLK;
                    RWCtrl[bank].isOwBlk = FALSE;
                    #if MeasureGc
                    start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_2);
                    int nValidChunks = garbageCollectLog(bank);
                    UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH1);
                    UINT32 nTicks = 0xFFFFFFFF - timerValue;
                    uart_print_level_2("GCW "); uart_print_level_2_int(bank);
                    uart_print_level_2(" "); uart_print_level_2_int(nTicks);
                    uart_print_level_2(" "); uart_print_level_2_int(nValidChunks); uart_print_level_2("\r\n");
                    #else
                    garbageCollectLog(bank);
                    #endif
                    //cleanListDump(&cleanListDataWrite, bank);
                }
#if Overwrite
            }
#endif
        }
        else
        {
            lbn = cleanListPop(&cleanListDataWrite, bank); // Now the hybrid approach can pop from the cleanList
            RWCtrl[bank].logLpn = lbn * PAGES_PER_BLK;
            RWCtrl[bank].isOwBlk = FALSE;
        }
        if (cleanListSize(&cleanListDataWrite, bank) < 1)
        { // In the Page Mapped we GC when there's one log blk left
            uart_print_level_1("\r\n\r\nSorry, run out of space in the log!\r\n\r\n");
            while(1);
        }
    }
    else
    {
        uart_print("\r\n");
        if (RWCtrl[bank].isOwBlk)
        {
            RWCtrl[bank].logLpn = lpn + 2;
        }
        else
        {
            RWCtrl[bank].logLpn = lpn+1;
        }
    }
    uart_print("New log lpn: "); uart_print_int(RWCtrl[bank].logLpn); uart_print("\r\n");
    //uart_print_level_1("31 ");
}

UINT32 getSWLpn(const UINT32 bank)
{
    uart_print("getSWLpn bank="); uart_print_int(bank); uart_print(":");
    UINT32 lpn = SWCtrl[bank].logLpn;
    if (lpn != INVALID) {
        uart_print_int(lpn); uart_print("\r\n");
        return lpn;
    }
    else
    {
        int count=0;
        uart_print(" may need to GC to get a new blk\r\n");
        //uart_print_level_1("Get SW Lpn\r\n");
        //cleanListDump(&cleanListDataWrite, bank);
        while(cleanListSize(&cleanListDataWrite, bank) <= 1)
        {
            count++;
            if(count > 10)
            {
                uart_print_level_1("Warning in getSwLpn: could not free a block after 10 GC\r\n");
                while(1);
            }
            #if MeasureGc
            start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_2);
            int nValidChunks = garbageCollectLog(bank);
            UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH1);
            UINT32 nTicks = 0xFFFFFFFF - timerValue;
            uart_print_level_2("GCW "); uart_print_level_2_int(bank);
            uart_print_level_2(" "); uart_print_level_2_int(nTicks);
            uart_print_level_2(" "); uart_print_level_2_int(nValidChunks);
            uart_print_level_2("\r\n");
            #else
            garbageCollectLog(bank);
            #endif
        }
        UINT32 lbn = cleanListPop(&cleanListDataWrite, bank);
        SWCtrl[bank].logLpn = lbn * PAGES_PER_BLK;
        SWCtrl[bank].nextDataLpn = INVALID;
        uart_print("Finished getSWLpn bank="); uart_print_int(bank); uart_print(":");
        uart_print(" returning "); uart_print_int(SWCtrl[bank].logLpn); uart_print("\r\n");
        //cleanListDump(&cleanListDataWrite, bank);
        return SWCtrl[bank].logLpn;
    }
}

void increaseSWLpn(const UINT32 bank)
{
    uart_print("increaseSWLpn: bank "); uart_print_int(bank); uart_print("\r\n");
    UINT32 lpn = SWCtrl[bank].logLpn;
    if (lpn == INVALID)
    {
        uart_print_level_1("Error in increaseSWLpn: trying to increase SW lpn when there's no SW block allocated!\r\n");
        while(1);
    }
    uart_print("Current log lpn: "); uart_print_int(lpn); uart_print("\r\n");
    if (LogPageToOffset(lpn) == UsedPagesPerLogBlk-1)
    { // current SW log block is full
        UINT32 lbn = get_log_lbn(lpn);
#if NO_FLASH_OPS
#undef nand_page_ptprogram(a, b, c, d, e, f, g)
#endif
        uart_print("Block full, don't allocate a new one. That will be done by following getOWLpn if more data comes\r\n");
        nand_page_ptprogram(bank, get_log_vbn(bank, lbn), PAGES_PER_BLK - 1, 0, (CHUNK_ADDR_BYTES * CHUNKS_PER_LOG_BLK + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR, SWCtrl[bank].lpnsListPtr, RETURN_WHEN_DONE);
#if NO_FLASH_OPS
#define nand_page_ptprogram(a, b, c, d, e, f, g)
#endif
        mem_set_dram(SWCtrl[bank].lpnsListPtr, INVALID, (CHUNKS_PER_BLK * CHUNK_ADDR_BYTES));
        insertBlkInHeap(&heapDataWrite, bank, lbn);
        SWCtrl[bank].logLpn = INVALID;
        SWCtrl[bank].nextDataLpn = INVALID;
        /*TODO: I don't think we should allocate a new block here. If there's more data in the stripe then the following getOWLpn will allocate a new block. However this needs testing.
        int count=0;
        //uart_print_level_1("Increase SW Lpn\r\n");
        //cleanListDump(&cleanListDataWrite, bank);
        while(cleanListSize(&cleanListDataWrite, bank) <= 1)
        {
            count++;
            if(count > 10)
            {
                uart_print_level_1("Warning in increaseSwLpn: could not free a block after 10 GC\r\n");
                while(1);
            }
            #if MeasureGc
            start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_2);
            int nValidChunks = garbageCollectLog(bank);
            UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH1);
            UINT32 nTicks = 0xFFFFFFFF - timerValue;
            uart_print_level_2("GCW "); uart_print_level_2_int(bank);
            uart_print_level_2(" "); uart_print_level_2_int(nTicks);
            uart_print_level_2(" "); uart_print_level_2_int(nValidChunks);
            uart_print_level_2("\r\n");
            #else
            garbageCollectLog(bank);
            #endif
        }
        lbn = cleanListPop(&cleanListDataWrite, bank);
        SWCtrl[bank].logLpn = lbn * PAGES_PER_BLK;
        SWCtrl[bank].nextDataLpn = INVALID;
        //cleanListDump(&cleanListDataWrite, bank);
        */
    }
    else
    {
        SWCtrl[bank].logLpn = lpn+1;
    }
    uart_print("After increaseSWLpn on bank "); uart_print_int(bank); uart_print(" new lpn is "); uart_print_int(SWCtrl[bank].logLpn); uart_print("\r\n");
}

#if Overwrite
UINT32 getOWLpn(const UINT32 bank)
{
    uart_print("getOWLpn bank="); uart_print_int(bank); uart_print("\r\n");
    UINT32 lpn = OWCtrl[bank].logLpn;
    if (lpn != INVALID)
    {
        uart_print_int(lpn); uart_print("\r\n");
        return lpn;
    }
    else
    {
        int count=0;
        uart_print("May need to GC to get a new blk\r\n");
        //uart_print_level_1("Get SW Lpn\r\n");
        //cleanListDump(&cleanListDataWrite, bank);
        while(cleanListSize(&cleanListDataWrite, bank) <= 1)
        {
            count++;
            if(count > 10)
            {
                uart_print_level_1("Warning in getSwLpn: could not free a block after 10 GC\r\n");
                while(1);
            }
            #if MeasureGc
            start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_2);
            int nValidChunks = garbageCollectLog(bank);
            UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH1);
            UINT32 nTicks = 0xFFFFFFFF - timerValue;
            uart_print_level_2("GCW "); uart_print_level_2_int(bank);
            uart_print_level_2(" "); uart_print_level_2_int(nTicks);
            uart_print_level_2(" "); uart_print_level_2_int(nValidChunks);
            uart_print_level_2("\r\n");
            #else
            garbageCollectLog(bank);
            #endif
        }
        UINT32 lbn = cleanListPop(&cleanListDataWrite, bank);
        OWCtrl[bank].logLpn = lbn * PAGES_PER_BLK;
        mem_set_dram(OWCtrl[bank].lpnsListPtr, INVALID, (CHUNKS_PER_BLK * CHUNK_ADDR_BYTES));
        uart_print("Finished getOWLpn bank="); uart_print_int(bank); uart_print(":");
        uart_print(" returning "); uart_print_int(OWCtrl[bank].logLpn); uart_print("\r\n");
        //cleanListDump(&cleanListDataWrite, bank);
        return OWCtrl[bank].logLpn;
    }
}

void increaseOWLpn(const UINT32 bank)
{
    uart_print("increaseOWLpn bank "); uart_print_int(bank); uart_print("\r\n");
    UINT32 lpn = getOWLpn(bank);
    if (lpn == INVALID)
    {
        uart_print_level_1("Error in increaseOWLpn: trying to increase RW lpn when there's no RW block allocated!\r\n");
        while(1);
    }
    uart_print("Current log lpn: "); uart_print_int(lpn); uart_print("\r\n");
    if (LogPageToOffset(lpn) == 123)
    {
        uart_print("OW block full\r\n");
        UINT32 lbn = get_log_lbn(lpn);
        nand_page_ptprogram(bank, get_log_vbn(bank, lbn), 125, 0, (CHUNK_ADDR_BYTES * CHUNKS_PER_LOG_BLK + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR, OWCtrl[bank].lpnsListPtr, RETURN_WHEN_DONE);
        insertBlkInHeap(&heapDataOverwrite, bank, lbn);
        OWCtrl[bank].logLpn = INVALID;
        int count=0;
        while(cleanListSize(&cleanListDataWrite, bank) <= 1)
        {
            count++;
            if(count > 10)
            {
                uart_print_level_1("Warning in increaseOwLpn: could not free a block after 10 GC\r\n");
                while(1);
            }
            #if MeasureGc
            start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_2);
            int nValidChunks = garbageCollectLog(bank);
            UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH1);
            UINT32 nTicks = 0xFFFFFFFF - timerValue;
            uart_print_level_2("GCW "); uart_print_level_2_int(bank);
            uart_print_level_2(" "); uart_print_level_2_int(nTicks);
            uart_print_level_2(" "); uart_print_level_2_int(nValidChunks);
            uart_print_level_2("\r\n");
            #else
            garbageCollectLog(bank);
            #endif
        }
        lbn = cleanListPop(&cleanListDataWrite, bank);
        mem_set_dram(OWCtrl[bank].lpnsListPtr, INVALID, (CHUNKS_PER_BLK * CHUNK_ADDR_BYTES));
        OWCtrl[bank].logLpn = lbn * PAGES_PER_BLK;
    }
    else
    {
        OWCtrl[bank].logLpn = LogPageToOffset(lpn) == 0 ? lpn + 1 : lpn + 2;
    }
    uart_print("After increaseOWLpn on bank "); uart_print_int(bank); uart_print(" new lpn is "); uart_print_int(OWCtrl[bank].logLpn); uart_print("\r\n");
}

void sealOWBlk(const UINT32 bank)
{
#if DetailedOwStats == 1
    uart_print_level_1("SWP\r\n");
#endif
    uart_print("sealOWBlk bank="); uart_print_int(bank); uart_print("\r\n");
    UINT32 nValidChunks = getVictimValidPagesNumber(&heapDataOverwrite, bank);
    UINT32 lbn = getVictim(&heapDataOverwrite, bank);
    if (lbn == INVALID)
    {
        uart_print_level_1("ERROR in sealOWBlk: found INVALID when getting victim from overwrite heap\r\n");
        while(1);
    }
    resetValidChunksAndRemove(&heapDataOverwrite, bank, lbn, CHUNKS_PER_LOG_BLK-8);
    resetOwCounter(bank, lbn);
    setValidChunks(&heapDataWrite, bank, lbn, nValidChunks);
    RWCtrl[bank].logLpn = lbn * PAGES_PER_VBLK + 2;
    RWCtrl[bank].isOwBlk = TRUE;
    mem_set_dram(RWCtrl[bank].lpnsListPtr, INVALID, (CHUNKS_PER_BLK * CHUNK_ADDR_BYTES));
    nand_page_ptread(bank, get_log_vbn(bank, lbn), 125, 0, (CHUNK_ADDR_BYTES * CHUNKS_PER_LOG_BLK + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR, RWCtrl[bank].lpnsListPtr, RETURN_WHEN_DONE);
    UINT32 correspondingChunks=0;
    for (UINT32 pageOffset=0; pageOffset < 124; )
    {
        uart_print("pageOffset="); uart_print_int(pageOffset); uart_print("\r\n");
        for (UINT32 chunkIdx=0; chunkIdx < CHUNKS_PER_PAGE; ++chunkIdx)
        {
            uart_print("chunkIdx="); uart_print_int(chunkIdx); uart_print(" ");
            UINT32 dataLpn = read_dram_32(chunkInLpnsList(RWCtrl[bank].lpnsListPtr, pageOffset, chunkIdx));
            uart_print("dataLpn="); uart_print_int(dataLpn); uart_print(" ");
            if (dataLpn != INVALID)
            {
                for (UINT32 chunkInDataLpn=0; chunkInDataLpn < CHUNKS_PER_PAGE; ++chunkInDataLpn)
                {
                    //UINT32 newChunkAddr = getChunkAddr(node, chunkInDataLpn);
                    if (ChunksMapTable(dataLpn, chunkInDataLpn) > DRAM_BASE + DRAM_SIZE)
                    {
                        uart_print_level_1("ERROR in sealOWBlk 1: reading above DRAM address space\r\n");
                    }
                    UINT32 newChunkAddr = read_dram_32(ChunksMapTable(dataLpn, chunkInDataLpn));
                    uart_print("chunkAddr="); uart_print_int(newChunkAddr);
                    if (findChunkLocation(newChunkAddr) == FlashOwLog)
                    {
                        newChunkAddr = newChunkAddr & 0x7FFFFFFF;
                        uart_print(" masked="); uart_print_int(newChunkAddr); uart_print(" ");
                        if (newChunkAddr == ( (bank * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (lbn * CHUNKS_PER_BLK) + (pageOffset * CHUNKS_PER_PAGE) + chunkIdx ))
                        {
                            uart_print(" corresponding\r\n");
                            correspondingChunks++;
                            write_dram_32(ChunksMapTable(dataLpn, chunkInDataLpn), newChunkAddr);
                            break;
                        }
                        else uart_print(" not corresponding\r\n");
                    }
                    else uart_print(" not in flash OW Log\r\n");
                }
            }
            else
            {
                uart_print("invalid\r\n");
            }
        }
        pageOffset = pageOffset == 0 ? 1 : pageOffset + 2;
    }
    if (correspondingChunks+504 != nValidChunks)
    {
        uart_print_level_1("ERROR: during sealOWBlk found a different number of valid chunks:\r\n");
        uart_print_level_1("correspondingChunks: "); uart_print_level_1_int(correspondingChunks);
        uart_print_level_1(" plus 504 still free chunk: "); uart_print_level_1_int(correspondingChunks+504); uart_print_level_1("\r\n");
        uart_print_level_1("nValidChunks: "); uart_print_level_1_int(nValidChunks); uart_print_level_1("\r\n");
    }
}
#endif

chunkLocation findChunkLocation(const UINT32 chunkAddr)
{
    if (chunkAddr==INVALID)
    {
        return Invalid;
    }
#if Overwrite
    if (chunkAddr & StartOwLogLpn)
    {
        UINT32 chunkOffset = ChunkToChunkOffsetInBank(chunkAddr & 0x7FFFFFFF);
        if (chunkOffset >= LogBufLpn * CHUNKS_PER_PAGE)
        {
            return DRAMOwLog;
        }
        else
        {
            return FlashOwLog;
        }
    }
#endif
    UINT32 chunkOffset = ChunkToChunkOffsetInBank(chunkAddr);
    if (chunkOffset >= LogBufLpn * CHUNKS_PER_PAGE)
    {
        return DRAMWLog;
    }
    return FlashWLog;
}
