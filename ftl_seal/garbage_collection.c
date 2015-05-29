#include "garbage_collection.h"
#include "dram_layout.h"
#include "ftl_parameters.h"
#include "ftl_metadata.h"
#include "log.h"
#include "blk_management.h"
#include "heap.h"
#include "cleanList.h"
#include "write.h"

#if NO_FLASH_OPS
#define nand_page_ptread(a, b, c, d, e, f, g)
#define nand_page_read(a, b, c, d)
#define nand_page_program(a, b, c, d)
#define nand_page_ptprogram(a, b, c, d, e, f)
#define nand_page_ptprogram_from_host(a, b, c, d, e)     g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS; \
                            SETREG (BM_STACK_WRSET, g_ftl_write_buf_id); \
                            SETREG (BM_STACK_RESET, 0x01);
#define nand_page_copyback(a, b, c, d, e)
#endif

#if MeasureGc
int garbageCollectLog(const UINT32 bank)
{
#else
void garbageCollectLog(const UINT32 bank)
{
#endif
    int nValidChunksInBlk = 0;
    gcNestLevel[bank]++;
    if (gcNestLevel[bank] == 0 || gcNestLevel[bank] > 2)
    {
        uart_print_level_1("ERROR in GC on bank "); uart_print_level_1_int(bank);
        uart_print_level_1(": unexpected nest level ("); uart_print_level_1_int(gcNestLevel[bank]); uart_print_level_1(")\r\n");
        while(1);
    }
    UINT32 victimLbn;
    UINT32 victimVbn;
    UINT32 previousDstBank=INVALID; // This variable is used to wait for banks in case we are doing copyback to different bank (i.e. if this bank is full)
    UINT32 nValidChunksFromHeap = getVictimValidPagesNumber(&heapDataWrite, bank);
#if Overwrite
    UINT32 nValidChunksFromHeapOW = getVictimValidPagesNumber(&heapDataOverwrite, bank);
    if (nValidChunksFromHeapOW < 504)
    {
        uart_print_level_1("ERROR: in garbageCollectLog on OW blk found less than 504 valid pages, that should not be possible\r\n");
        uart_print_level_1("Found "); uart_print_level_1_int(nValidChunksFromHeap); uart_print_level_1(" valid chunks\r\n");
        while(1);
    }
    nValidChunksFromHeapOW-=504;  // Adjust value considering that all high pages will be found invalid, and they are 63, hence the - 504
    if (nValidChunksFromHeapOW < nValidChunksFromHeap)
    {   // There are no full RW/SW blks, or it is convenient to gc OW blocks
        victimLbn = getVictim(&heapDataOverwrite, bank);
        victimVbn = get_log_vbn(bank, victimLbn);
        nValidChunksFromHeap=nValidChunksFromHeapOW;  // Adjust value considering that all high pages will be found invalid, and they are 63, hence the - 504
        resetValidChunksAndRemove(&heapDataOverwrite, bank, victimLbn, CHUNKS_PER_LOG_BLK-8);
        resetOwCounter(bank, victimLbn);
        if (nValidChunksFromHeap > 0)
        {
            nand_page_ptread(bank, victimVbn, 125, 0, (CHUNK_ADDR_BYTES * CHUNKS_PER_LOG_BLK + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR, VICTIM_LPN_LIST(bank, gcNestLevel[bank]), RETURN_WHEN_DONE);
        }
    }
    else
    {
#endif
        victimLbn = getVictim(&heapDataWrite, bank);
        victimVbn = get_log_vbn(bank, victimLbn);
        resetValidChunksAndRemove(&heapDataWrite, bank, victimLbn, CHUNKS_PER_LOG_BLK);
        if (nValidChunksFromHeap > 0)
        {
            nand_page_ptread(bank, victimVbn, PAGES_PER_BLK - 1, 0, (CHUNK_ADDR_BYTES * CHUNKS_PER_LOG_BLK + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR, VICTIM_LPN_LIST(bank, gcNestLevel[bank]), RETURN_WHEN_DONE);
        }
#if Overwrite
    }
#endif
    uart_print("garbageCollectLog on bank "); uart_print_int(bank);
    uart_print(" victimLbn "); uart_print_int(victimLbn);
    uart_print(" victimVbn "); uart_print_int(victimVbn); uart_print(" ");
    uart_print(" nest level "); uart_print_int(gcNestLevel[bank]); uart_print("\r\n");
    uart_print("Number valid pages: "); uart_print_int(nValidChunksFromHeap); uart_print("\r\n");

    if (nValidChunksFromHeap > 0)
    {
        for(UINT32 pageOffset=0; pageOffset < UsedPagesPerLogBlk; pageOffset++)
        {
            uart_print("\r\npageOffset="); uart_print_int(pageOffset); uart_print("\r\n");
            UINT32 dataChunkOffsets[CHUNKS_PER_PAGE];
            UINT32 dataLpns[CHUNKS_PER_PAGE];
            UINT8 validChunks[CHUNKS_PER_PAGE];
            UINT8 nValidChunksInPage=0;
            for(UINT32 chunkOffset=0; chunkOffset<CHUNKS_PER_PAGE; chunkOffset++) validChunks[chunkOffset]=0;
            for(UINT32 chunkOffset=0; chunkOffset<CHUNKS_PER_PAGE; chunkOffset++)
            {   // This loops finds valid chunks is the page. Note that chunks in GC Buf won't be considered as they temporarily don't occupy space in Log
                UINT32 victimLpn = read_dram_32(VICTIM_LPN_LIST(bank, gcNestLevel[bank])+(pageOffset*CHUNKS_PER_PAGE+chunkOffset)*CHUNK_ADDR_BYTES);
                uart_print("chunkOffset="); uart_print_int(chunkOffset);
                uart_print(", victimLpn="); uart_print_int(victimLpn);
                if (victimLpn != INVALID)
                {
                    //hashnode_ptr nodePtr = shashtblGetNode(victimLpn);
                    //if(nodePtr != NULL)
                    //{
                        for(UINT32 i=0; i<CHUNKS_PER_PAGE; i++)
                        {
                            //UINT32 chunkAddr = getChunkAddr(nodePtr,i);
                            if (ChunksMapTable(victimLpn , i) > DRAM_BASE + DRAM_SIZE)
                            {
                                uart_print_level_1("ERROR in garbageCollectLog 1: reading above DRAM address space\r\n");
                            }
                            UINT32 chunkAddr = read_dram_32(ChunksMapTable(victimLpn, i));
#if Overwrite
                            if (findChunkLocation(chunkAddr) == FlashOwLog || findChunkLocation(chunkAddr) == DRAMOwLog) { chunkAddr = chunkAddr & 0x7FFFFFFF; }
#endif
                            if(chunkAddr == ((bank*LOG_BLK_PER_BANK*CHUNKS_PER_BLK) + (victimLbn*CHUNKS_PER_BLK) + (pageOffset*CHUNKS_PER_PAGE+chunkOffset)))
                            {   // chunk is valid
                                uart_print(" valid!");
                                dataChunkOffsets[chunkOffset]=i;
                                dataLpns[chunkOffset]=victimLpn;
                                validChunks[chunkOffset]=1;
                                nValidChunksInPage++;
                                nValidChunksInBlk++;
                                break;
                            }
                        }
                    //}
                    //else
                    //{
                        //uart_print(" null node");
                    //}
                }
                uart_print("\r\n");
            }
            if(nValidChunksInPage == 8)
            {
                /*
                if(nValidChunksFromHeap < CHUNKS_PER_LOG_BLK)
                {
                    uart_print("Copyback entire page\r\n");
                    // Entire page is valid: use nand_copyback
                    UINT32 newLogLpn = getRWLpn(bank);
                    UINT32 vBlk = get_log_vbn(bank, LogPageToLogBlk(newLogLpn));
                    UINT32 newLogPageOffset = LogPageToOffset(newLogLpn);
                    nand_page_copyback(bank, victimVbn, pageOffset, vBlk, newLogPageOffset);
                    for(UINT32 chunkOffset=0; chunkOffset<CHUNKS_PER_PAGE; chunkOffset++)
                    { // chunks cannot be valid in DRAM, therefore don't bother checking
                        write_dram_32(chunkInLpnsList(RWCtrl[bank].lpnsListPtr, LogPageToOffset(newLogLpn), chunkOffset), dataLpns[chunkOffset]);
                        //write_dram_32(&hashNodeAddressesToUpdate[chunkOffset]->chunks[dataChunkOffsets[chunkOffset]], (bank * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (newLogLpn * CHUNKS_PER_PAGE) + chunkOffset);
                        write_dram_32(ChunksMapTable(dataLpns[chunkOffset], dataChunkOffsets[chunkOffset]), (bank * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (newLogLpn * CHUNKS_PER_PAGE) + chunkOffset);
                    }
                    if (nValidChunksInBlk == nValidChunksFromHeap)
                    {   // Returns block before increasing the RWLpn, because if the increaseLpn triggers a new block allocation, then it can directly reuse the one just freed
                        uart_print("After copyback, return victim blk earlier because it's cleared.\r\n");
                        set_log_vbn (bank, victimLbn, get_free_vbn(bank));
                        nand_block_erase(bank, victimVbn);
                        ret_free_vbn(bank, victimVbn);
                        cleanListPush(&cleanListDataWrite, bank, victimLbn);
                        uart_print("After GC: victim lbn "); uart_print_int(victimLbn);
                        uart_print(" nest level "); uart_print_int(gcNestLevel[bank]); uart_print("\r\n");
                        gcNestLevel[bank]--;
                        increaseRWLpn(bank);
                        #if MeasureGc
                        if (nValidChunksInBlk != nValidChunksFromHeap)
                        {
                            uart_print_level_1("ERROR: found different number of valid chunks than expected. GC on bank "); uart_print_level_1_int(bank);
                            uart_print_level_1(" nest level "); uart_print_level_1_int(gcNestLevel[bank]); uart_print_level_1("\r\n");
                            uart_print_level_1("Found "); uart_print_level_1_int(nValidChunksInBlk);
                            uart_print_level_1(" instead of expected "); uart_print_level_1_int(nValidChunksFromHeap); uart_print_level_1("\r\n");
                            //while(1);
                        }
                        return nValidChunksInBlk;
                        #else
                        return;
                        #endif
                    }
                    else
                    {
                        increaseRWLpn(bank);
                    }
                }
                else
                {
                */
                    uart_print("Current bank is full, copy page to another one\r\n");
                    UINT32 dstBank = chooseNewBank();
                    UINT32 dstLpn = getRWLpn(dstBank);
                    UINT32 dstVbn = get_log_vbn(dstBank, LogPageToLogBlk(dstLpn));
                    UINT32 dstPageOffset = LogPageToOffset(dstLpn);
                    uart_print("dstBank="); uart_print_int(dstBank); uart_print(" dstLpn="); uart_print_int(dstLpn);
                    uart_print(" dstVbn="); uart_print_int(dstVbn); uart_print(" dstPageOffset="); uart_print_int(dstPageOffset); uart_print("\r\n");
                    if (previousDstBank != INVALID) {waitBusyBank(previousDstBank);}
                    previousDstBank = dstBank;
                    nand_page_read(bank, victimVbn, pageOffset, GC_BUF(bank, gcNestLevel[bank]));
                    nand_page_program(dstBank, dstVbn, dstPageOffset, GC_BUF(bank, gcNestLevel[bank]), RETURN_ON_ISSUE);
                    for (UINT32 chunkOffset=0; chunkOffset<CHUNKS_PER_PAGE; ++chunkOffset)
                    {
                        write_dram_32(chunkInLpnsList(RWCtrl[dstBank].lpnsListPtr, dstPageOffset, chunkOffset), dataLpns[chunkOffset]);
                        //write_dram_32(&hashNodeAddressesToUpdate[chunkOffset]->chunks[dataChunkOffsets[chunkOffset]], (dstBank * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (dstLpn * CHUNKS_PER_PAGE) + chunkOffset);
                        write_dram_32(ChunksMapTable(dataLpns[chunkOffset], dataChunkOffsets[chunkOffset]), (dstBank * LOG_BLK_PER_BANK * CHUNKS_PER_BLK) + (dstLpn * CHUNKS_PER_PAGE) + chunkOffset);
                    }
                    if (nValidChunksInBlk == nValidChunksFromHeap)
                    {   // Returns block before increasing the RWLpn, because if the increaseLpn triggers a new block allocation, then it can directly reuse the one just freed
                        uart_print("After copyback, return victim blk earlier because it's cleared.\r\n");
                        set_log_vbn (bank, victimLbn, get_free_vbn(bank));
                        nand_block_erase(bank, victimVbn);
                        ret_free_vbn(bank, victimVbn);
                        cleanListPush(&cleanListDataWrite, bank, victimLbn);
                        uart_print("After GC: victim lbn "); uart_print_int(victimLbn);
                        uart_print(" nest level "); uart_print_int(gcNestLevel[bank]); uart_print("\r\n");
                        gcNestLevel[bank]--;
                        increaseRWLpn(dstBank);
                        #if MeasureGc
                        if (nValidChunksInBlk != nValidChunksFromHeap)
                        {
                            uart_print_level_1("ERROR: found different number of valid chunks than expected. GC on bank "); uart_print_level_1_int(bank);
                            uart_print_level_1(" nest level "); uart_print_level_1_int(gcNestLevel[bank]); uart_print_level_1("\r\n");
                            uart_print_level_1("Found "); uart_print_level_1_int(nValidChunksInBlk);
                            uart_print_level_1(" instead of expected "); uart_print_level_1_int(nValidChunksFromHeap); uart_print_level_1("\r\n");
                            //while(1);
                        }
                        return nValidChunksInBlk;
                        #else
                        return;
                        #endif
                    }
                    else
                    {
                        increaseRWLpn(dstBank);
                    }
                //}
            }
            else
            {
                if(nValidChunksInPage > 0)
                {   // If there's at least one valid chunk in the page
                    nand_page_read(bank, victimVbn, pageOffset, GC_BUF(bank, gcNestLevel[bank]));
                    if (nValidChunksInBlk == nValidChunksFromHeap)
                    {   // Returns block before writing to RW blk, because if there's an increaseLpn that triggers a new block allocation, then it can directly reuse the one just freed
                        uart_print("After reading last page, return victim blk earlier because it's cleared.\r\n");
                        set_log_vbn (bank, victimLbn, get_free_vbn(bank));
                        nand_block_erase(bank, victimVbn);
                        ret_free_vbn(bank, victimVbn);
                        cleanListPush(&cleanListDataWrite, bank, victimLbn);
                    }
                    for(UINT32 chunkOffset=0; chunkOffset<CHUNKS_PER_PAGE; chunkOffset++)
                    {
                        if(validChunks[chunkOffset])
                        {
                            UINT8 ret = writeChunkOnLogBlockDuringGC(dataLpns[chunkOffset], dataChunkOffsets[chunkOffset], chunkOffset, GC_BUF(bank, gcNestLevel[bank]));
                            if (ret != 0)
                            {
                                uart_print_level_1("ERROR during GC at chunkOffset: "); uart_print_level_1_int(chunkOffset); uart_print_level_1("\r\n");
                                while(1);
                            }
                        }
                    }
                    if (nValidChunksInBlk == nValidChunksFromHeap)
                    {   // Returns now because the blk was already erased and returned
                        gcNestLevel[bank]--;
                        uart_print("After GC: victim lbn "); uart_print_int(victimLbn);
                        uart_print(" nest level "); uart_print_int(gcNestLevel[bank]); uart_print("\r\n");
                        #if MeasureGc
                        if (nValidChunksInBlk != nValidChunksFromHeap)
                        {
                            uart_print_level_1("ERROR: found different number of valid chunks than expected. GC on bank "); uart_print_level_1_int(bank);
                            uart_print_level_1(" nest level "); uart_print_level_1_int(gcNestLevel[bank]); uart_print_level_1("\r\n");
                            uart_print_level_1("Found "); uart_print_level_1_int(nValidChunksInBlk);
                            uart_print_level_1(" instead of expected "); uart_print_level_1_int(nValidChunksFromHeap); uart_print_level_1("\r\n");
                            //while(1);
                        }
                        return nValidChunksInBlk;
                        #else
                        return;
                        #endif
                    }
                }
            }
        }
        #if MeasureGc
        if (nValidChunksInBlk != nValidChunksFromHeap)
        {
            uart_print_level_1("ERROR: found different number of valid chunks than expected. GC on bank "); uart_print_level_1_int(bank);
            uart_print_level_1(" nest level "); uart_print_level_1_int(gcNestLevel[bank]); uart_print_level_1("\r\n");
            uart_print_level_1("Found "); uart_print_level_1_int(nValidChunksInBlk);
            uart_print_level_1(" instead of expected "); uart_print_level_1_int(nValidChunksFromHeap); uart_print_level_1("\r\n");
        }
        #endif
    }
    set_log_vbn (bank, victimLbn, get_free_vbn(bank));
    nand_block_erase(bank, victimVbn);
    ret_free_vbn(bank, victimVbn);
    cleanListPush(&cleanListDataWrite, bank, victimLbn);
    uart_print("After GC: victim lbn "); uart_print_int(victimLbn);
    uart_print(" nest level "); uart_print_int(gcNestLevel[bank]); uart_print("\r\n");
    gcNestLevel[bank]--;
    #if MeasureGc
    return nValidChunksInBlk;
    #endif
}
