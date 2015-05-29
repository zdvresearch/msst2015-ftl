// 2011 INDILINX Co., Ltd.
//
// This file is part of Jasmine.
//
// Jasmine is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Jasmine is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Jasmine. See the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//
// FASTer FTL source file
//
// Author; Sang-Phil Lim (Sungkyunkwan Univ. VLDB Lab., Korea)
//
// NOTE;
//
//   - This scheme is one of the 'hybrid mapping FTL' which shows relatively good performance on write-skewed workloads than FAST FTL
//     (e.g., 70/30, 80/20, OLTP workload)
//   - In my observation, this scheme shows poor performance than DAC FTL, but the memory requirement is quite low and provide stable performance.
//   - And also, in terms of SPOR, I think it can get more chances than page mapping FTLs to adopting a light-weight recovery algorithm.
//     (such as, Sungup Moon et al, "Crash recovery in FAST FTL", SEUS 2010
//
// Features;
//
//   - normal POR support
//
// Reference;
//
//   - Sang-Phil Lim et al, "FASTer FTL for Enterprise-Class Flash Memory SSDs", IEEE SNAPI 2010.

#include "jasmine.h"
#include "ftl.h"
#include "dram_layout.h"
#include "ftl_parameters.h"
#include "ftl_metadata.h"

#include "log.h"  // TODO: find a better way to share the macros
#include "blk_management.h"  // TODO: find a better way to share the macros
#include "heap.h"
#include "cleanList.h"
#include "read.h"
#include "write.h"
#include "overwrite.h"
#include "flash.h" // RETURN_ON_ISSUE RETURN_WHEN_DONE
#include "garbage_collection.h"

#if NO_FLASH_OPS
#define nand_page_ptread(a, b, c, d, e, f, g)
#define nand_page_read(a, b, c, d)
#define nand_page_program(a, b, c, d)
#define nand_page_ptprogram(a, b, c, d, e, f)
#define nand_page_ptprogram_from_host (a, b, c, d, e)     {g_ftl_write_buf_id = (g_ftl_write_buf_id + 1) % NUM_WR_BUFFERS; \
                            SETREG (BM_STACK_WRSET, g_ftl_write_buf_id); \
                            SETREG (BM_STACK_RESET, 0x01);}
#define nand_page_copyback(a, b, c, d, e)
//#define nand_block_erase(a, b)
//#define nand_block_erase_sync(a, b)
#endif


//----------------------------------
// FTL internal function prototype
//----------------------------------
static void sanity_check (void);
static void build_bad_blk_list (void);
static void format (void);
static void init_metadata_sram (void);
static void set_bad_block (UINT32 const bank, UINT32 const vblk_offset);
//static void test();
//static void uart_print_buf(const UINT32 startAddr, const UINT32 bytes);
static void trimRange(const UINT32 lba, const UINT32 nSectors);

static void backgroundCleaning();

static void sanity_check ()
{
    if (DRAM_BYTES_OTHER > DRAM_SIZE)
    {
        uart_print_level_1("DRAM Metadata and buffers are too big (not counting SATA buffers)\r\n");
        uart_print_level_1 ("\r\nDATA_BLK_PER_BANK "); uart_print_level_1_int(DATA_BLK_PER_BANK);
        uart_print_level_1 ("\r\nLOG_BLK_PER_BANK "); uart_print_level_1_int(LOG_BLK_PER_BANK);
        uart_print_level_1("\r\nDRAM Metadata and buffers (not SATA): "); uart_print_level_1_int(DRAM_BYTES_OTHER/1024); uart_print_level_1("KB\r\n");
        while(1);
    }
    UINT32 dram_requirement = RD_BUF_BYTES + WR_BUF_BYTES + DRAM_BYTES_OTHER;
    uart_print("Read buffers: "); uart_print_int(RD_BUF_BYTES/1024);
    uart_print(" KB\r\nWrite buffers: "); uart_print_int(WR_BUF_BYTES/1024);
    uart_print(" KB\r\nOthers: "); uart_print_int(DRAM_BYTES_OTHER/1024);
    uart_print(" KB\r\nTotal DRAM Requirements: "); uart_print_int(dram_requirement/1024);
    uart_print(" KB\r\nCopy buffers: "); uart_print_int(COPY_BUF_BYTES/1024);
    uart_print(" KB\r\nFtl buffers: "); uart_print_int(FTL_BUF_BYTES/1024 );
    uart_print(" KB\r\nHil buffers: "); uart_print_int(HIL_BUF_BYTES/1024);
    uart_print(" KB\r\nTemp buffers: "); uart_print_int(TEMP_BUF_BYTES/1024);
    uart_print(" KB\r\nLog buffers: "); uart_print_int(LOG_BUF_BYTES/1024);
    uart_print(" KB\r\nLpns in Log buffers: "); uart_print_int(LPNS_IN_LOG_BYTES/1024);
    uart_print(" KB\r\nValid Chunks Heap: "); uart_print_int(HEAP_VALID_CHUNKS_BYTES/1024);
    uart_print(" KB\r\nValidation bitmap: "); uart_print_int(VC_BITMAP_BYTES/1024);
    uart_print(" KB\r\nValid in Data Blk bitmap: "); uart_print_int(C_BITMAP_BYTES/1024);
    uart_print(" KB\r\nValid in Data Blk bitmap: "); uart_print_int(C_BITMAP_BYTES/1024);
    uart_print(" KB\r\nLog BMT: "); uart_print_int(LOG_BMT_BYTES/1024);
    if (dram_requirement > DRAM_SIZE)
    {
        uart_print_level_1("Requires too much DRAM memory\r\n");
        while(1);
    }
}

static void build_bad_blk_list (void) {
    uart_print("build_bad_blk_list\r\n");
    UINT32 bank, num_entries, result, vblk_offset;
    scan_list_t *scan_list = (scan_list_t *) TEMP_BUF_ADDR;
    uart_print("setting bad blk bmp at address ");
    uart_print_int(BAD_BLK_BMP_ADDR);
    uart_print("\r\n");
    mem_set_dram (BAD_BLK_BMP_ADDR, NULL, BAD_BLK_BMP_BYTES);
    disable_irq ();
    flash_clear_irq ();
    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        SETREG (FCP_CMD, FC_COL_ROW_READ_OUT);
        SETREG (FCP_BANK, REAL_BANK (bank));
        SETREG (FCP_OPTION, FO_E);
        SETREG (FCP_DMA_ADDR, (UINT32) scan_list);
        SETREG (FCP_DMA_CNT, SCAN_LIST_SIZE);
        SETREG (FCP_COL, 0);
        SETREG (FCP_ROW_L (bank), SCAN_LIST_PAGE_OFFSET);
        SETREG (FCP_ROW_H (bank), SCAN_LIST_PAGE_OFFSET);
        SETREG (FCP_ISSUE, NULL);
        int count=0;
        while ((GETREG (WR_STAT) & 0x00000001) != 0)
        {
            count++;
            if (count > 100000)
            {
                uart_print_level_1("Warning1 on build_bad_blk_list\r\n");
                count=0;
            }
        }
        count=0;
        while (BSP_FSM (bank) != BANK_IDLE)
        {
            count++;
            if (count > 100000)
            {
                uart_print_level_1("Warning1 on build_bad_blk_list\r\n");
                count=0;
            }
        }
        num_entries = NULL;
        result = OK;
        if (BSP_INTR (bank) & FIRQ_DATA_CORRUPT)
        {
            result = FAIL;
        }
        else
        {
            UINT32 i;
            num_entries = read_dram_16 (&(scan_list->num_entries));
            if (num_entries > SCAN_LIST_ITEMS)
            {
                result = FAIL;
            }
            else
            {
                for (i = 0; i < num_entries; i++) {
                    UINT16 entry = read_dram_16 (scan_list->list + i);
                    UINT16 pblk_offset = entry & 0x7FFF;
                    if (pblk_offset == 0 || pblk_offset >= PBLKS_PER_BANK)
                    {
#if OPTION_REDUCED_CAPACITY == FALSE
                        result = FAIL;
#endif
                    }
                    else
                    {
                        write_dram_16 (scan_list->list + i, pblk_offset);
                    }
                }
            }
        }
        if (result == FAIL)
        {
            num_entries = 0;
        }
        else
        {
            write_dram_16 (&(scan_list->num_entries), 0);
        }
        for (vblk_offset = 1; vblk_offset < VBLKS_PER_BANK; vblk_offset++)
        {
            BOOL32 bad = FALSE;
#if OPTION_2_PLANE
            {
                UINT32 pblk_offset;
                pblk_offset = vblk_offset * NUM_PLANES;
                if (mem_search_equ_dram (scan_list, sizeof (UINT16), num_entries + 1, pblk_offset) < num_entries + 1)
                {
                    bad = TRUE;
                }
                pblk_offset = vblk_offset * NUM_PLANES + 1;
                if (mem_search_equ_dram (scan_list, sizeof (UINT16), num_entries + 1, pblk_offset) < num_entries + 1)
                {
                    bad = TRUE;
                }
            }
#else
            {
                if (mem_search_equ_dram (scan_list, sizeof (UINT16), num_entries + 1, vblk_offset) < num_entries + 1)
                {
                    bad = TRUE;
                }
            }
#endif
            if (bad)
            {
                set_bit_dram (BAD_BLK_BMP_ADDR + bank * (VBLKS_PER_BANK / 8 + 1), vblk_offset);
            }
        }
    }
}

void ftl_open (void)
{
    uart_print ("WAFTL v1.2\r\n");
    uart_print_level_1("Page Mapped Scheme\r\n");
#if Overwrite
    uart_print_level_1("Overwrites enabled\r\n");
    uart_print_level_1("Overwrite limit: "); uart_print_level_1_int(OwLimit); uart_print_level_1("\r\n");
#else
    uart_print_level_1("Overwrites disabled\r\n");
#endif
    uart_print_level_1("Important parameters:\r\n");
    uart_print_level_1("N_BANKS "); uart_print_level_1_int(NUM_BANKS); uart_print_level_1("\r\n");
    uart_print_level_1("DATA_BLK_PER_BANK "); uart_print_level_1_int(DATA_BLK_PER_BANK); uart_print_level_1("\r\n");
    uart_print_level_1("SPARE_LOG_BLK_PER_BANK "); uart_print_level_1_int(SPARE_LOG_BLK_PER_BANK); uart_print_level_1("\r\n");
    uart_print_level_1("LOG_BLK_PER_BANK "); uart_print_level_1_int(LOG_BLK_PER_BANK); uart_print_level_1("\r\n");
    uart_print_level_1("Bank mapping:\r\n");
    for (int i=0; i<NUM_BANKS; i++)
    {
        uart_print_level_1("Bank "); uart_print_level_1_int(i);
        uart_print_level_1(" mapped to real bank "); uart_print_level_1_int(REAL_BANK(i)); uart_print_level_1("\r\n");
    }
    uart_print("DRAM Address range: "); uart_print_int(DRAM_BASE); uart_print(" - "); uart_print_int(END_ADDR); uart_print("\r\n");
    uart_print("Total bytes: "); uart_print_int(END_ADDR - DRAM_BASE); uart_print("\r\n");
    uart_print_level_1("Total FTL DRAM metadata size: "); uart_print_level_1_int((UINT32)DRAM_BYTES_OTHER/1024); uart_print_level_1(" KB\r\n");
    uart_print_level_1("Total FTL SRAM metadata size: "); uart_print_level_1_int(SizeSRAMMetadata); uart_print_level_1(" B\r\n");
    sanity_check();
    build_bad_blk_list ();
    flash_clear_irq ();
    SETREG (INTR_MASK, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
    SETREG (FCONF_PAUSE, FIRQ_DATA_CORRUPT | FIRQ_BADBLK_L | FIRQ_BADBLK_H);
    enable_irq ();
    format ();
    g_ftl_read_buf_id = 0;
    g_ftl_write_buf_id = 0;
    mem_set_sram (g_mem_to_set, INVALID, PAGES_PER_BLK / 8);
    mem_set_sram (g_mem_to_clr, NULL, PAGES_PER_BLK / 8);
}

static void format (void)
{
    uart_print ("do format\r\n");
    uart_print ("NUM_BANKS ");
    uart_print_int((UINT32)NUM_BANKS);
    uart_print ("\r\nNUM_PSECTORS ");
    uart_print_int((UINT32)NUM_PSECTORS);
    uart_print ("\r\nNUM_LSECTORS ");
    uart_print_int(NUM_LSECTORS);
    uart_print ("\r\nVBLKS_PER_BANK ");
    uart_print_int(VBLKS_PER_BANK);
    uart_print ("\r\nDATA_BLK_PER_BANK ");
    uart_print_int(DATA_BLK_PER_BANK);
    uart_print ("\r\nLOG_BLK_PER_BANK ");
    uart_print_int(LOG_BLK_PER_BANK);
    uart_print ("\r\nFREE_BLK_PER_BANK ");
    uart_print_int(FREE_BLK_PER_BANK);
    uart_print ("\r\n");

    uart_print("Initialize DRAM metadata...\r\n");
    uart_print("Initializing Log BMT...");
    mem_set_dram (LOG_BMT_ADDR, NULL, LOG_BMT_BYTES);
    uart_print("done\r\n");
    uart_print("Initializing Free BMT...");
    mem_set_dram (FREE_BMT_ADDR, NULL, FREE_BMT_BYTES);
    uart_print("done\r\n");
    uart_print("Initializing Chunks Map Table...");
    mem_set_dram (CHUNKS_MAP_TABLE_ADDR, INVALID, CHUNKS_MAP_TABLE_BYTES);
    uart_print("done\r\n");
#if Overwrite
    uart_print("Initializing Overwrite Counters...");
    mem_set_dram (OW_COUNT_ADDR, 0, OW_COUNT_BYTES);
    uart_print("done\r\n");
#endif
    uart_print("Initializing Lpns in Log...");
    mem_set_dram(LPNS_IN_LOG_1_ADDR, INVALID, LPNS_IN_LOG_BYTES);
    mem_set_dram(LPNS_IN_LOG_2_ADDR, INVALID, LPNS_IN_LOG_BYTES);
    mem_set_dram(LPNS_IN_LOG_3_ADDR, INVALID, LPNS_IN_LOG_BYTES);
    uart_print("done\r\n");
    uart_print("DRAM initialization done\r\n");
    UINT32 lbn, vblock;
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
    {
        vblock = 180;
        nand_block_erase_sync (bank, vblock);
        g_bsp_isr_flag[bank] = INVALID;
        uart_print("Setting Free BMT...");
        for (lbn = 0; lbn < FREE_BLK_PER_BANK;)
        {
            vblock++;
            if (is_bad_block (bank, vblock) == TRUE)
            {
                continue;
            }
            nand_block_erase_sync(bank, vblock);
            if (g_bsp_isr_flag[bank] != INVALID)
            {
                set_bad_block (bank, g_bsp_isr_flag[bank]);
                g_bsp_isr_flag[bank] = INVALID;
                continue;
            }
            ret_free_vbn(bank, vblock);
            lbn++;
        }
        uart_print("done\r\n");
        uart_print("Setting Log BMT...");
        uart_print("Initializing bank "); uart_print_int(bank); uart_print("\r\n");
        uart_print("\tReal bank "); uart_print_int(REAL_BANK(bank)); uart_print("\r\n");
        for (lbn = 0; lbn < LOG_BLK_PER_BANK;)
        {
            vblock++;
            if (vblock >= VBLKS_PER_BANK)
            {
                break;
            }
            if (is_bad_block (bank, vblock) == TRUE)
            {
                continue;
            }
            nand_block_erase_sync (bank, vblock);
            if (g_bsp_isr_flag[bank] != INVALID)
            {
                set_bad_block (bank, g_bsp_isr_flag[bank]);
                g_bsp_isr_flag[bank] = INVALID;
                continue;
            }
            uart_print("\tLog block "); uart_print_int(lbn); uart_print(" assigned to vbn "); uart_print_int(vblock); uart_print("\r\n");
            set_log_vbn(bank, lbn, vblock);
            lbn++;
        }
        // set remained log blocks as `invalid'
        UINT32 invalidLogBlks=0;
        while (lbn < LOG_BLK_PER_BANK)
        {
            write_dram_16 (LOG_BMT_ADDR + ((bank * LOG_BLK_PER_BANK + lbn) * sizeof (UINT16)), (UINT16) - 1);
            lbn++;
            invalidLogBlks++;
        }
        uart_print("there are ");
        uart_print_int(invalidLogBlks);
        uart_print("invalid log blocks...done\r\n");
    }
    //----------------------------------------
    // initialize SRAM metadata
    //----------------------------------------
    init_metadata_sram ();
    led (1);
    uart_print_level_1("format complete");
    uart_print_level_1("\r\n");
}

/*
static void test()
{
    uart_print_level_1("ftl::test\r\n");
    UINT32 bank=23;
    UINT32 blk=8;
    //uart_print("Test: Erase, write last page, read last page, erase, write last page, read it.\r\nBank ");
    //uart_print_int(bank);
    //uart_print(" blk ");
    //uart_print_int(blk);
//
    //uart_print("\r\nErase blk...");
    //nand_block_erase_sync(bank, blk);
    //uart_print("done\r\n");
//
//
    //for(int i=0; i<4; i++){
        //uart_print("Prepare buffer...");
        //mem_set_dram (TEMP_BUF_ADDR, i, BYTES_PER_PAGE);
        //uart_print("done\r\n");
//
        //uart_print("Program page...");
        //nand_page_program(bank, blk, page, TEMP_BUF_ADDR);
        //uart_print("done\r\n");
//
        //uart_print("Read page...");
        //nand_page_read(bank, blk, page, FTL_BUF(bank));
        //uart_print("done\r\n");
//
        //uart_print("Page read from flash:\r\n");
        //uart_print_buf(FTL_BUF(bank), BYTES_PER_PAGE);
//
        //uart_print("\r\nErase blk...");
        //nand_block_erase_sync(bank, blk);
        //uart_print("done\r\n");
    //}
    uart_print("Test bank: ");
    uart_print_int(bank);
    uart_print("\r\n");

    for(int pageUnderTest=0; pageUnderTest<PAGES_PER_BLK; pageUnderTest++){
        int lowPage;
        int highPage;
        if(pageUnderTest==0){
            lowPage=0;
            highPage=2;
        }
        else{
            if (pageUnderTest==125){
                lowPage=125;
                highPage=127;
            }
            else{
                if(pageUnderTest%2==1){
                    lowPage=pageUnderTest;
                    highPage=pageUnderTest+3;
                }
                else
                    continue;
            }
        }

        uart_print("\r\n\r\nPages under test: L ");
        uart_print_int(lowPage);
        uart_print(" H ");
        uart_print_int(highPage);
        uart_print("\r\n");

        while(1){
            nand_block_erase_sync(bank, blk);
            if (g_bsp_isr_flag[bank] != INVALID) {
                g_bsp_isr_flag[bank] = INVALID;
                blk++;
            }
            else
                break;
        }

        mem_set_dram (TEMP_BUF_ADDR, 1, BYTES_PER_PAGE);
        nand_page_program(bank, blk, lowPage, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
        mem_set_dram (TEMP_BUF_ADDR, 2, BYTES_PER_PAGE);
        nand_page_program(bank, blk, highPage, TEMP_BUF_ADDR, RETURN_WHEN_DONE);

        mem_set_dram (TEMP_BUF_ADDR, 0xFFFFFFFC, BYTES_PER_PAGE);
        for (int pageToWrite=0; pageToWrite<highPage; pageToWrite++){
            if(pageToWrite != lowPage){
                nand_page_program(bank, blk, pageToWrite, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
                for(int i=0; i<PAGES_PER_BLK; i++){
                    nand_page_read(bank, blk, i, FTL_BUF(bank));
                    if (g_bsp_isr_flag[bank] != INVALID) {
                        uart_print("Error P ");
                        uart_print_int(pageToWrite);
                        uart_print(" when reading P ");
                        uart_print_int(i);
                        uart_print("\r\n");
                        g_bsp_isr_flag[bank] = INVALID;
                    }
                }
            }
        }
        blk++;
    }
    while(1);

    UINT32 highPage = 14;
    UINT32 lowPage = 11;

    uart_print("Write 5 to low page...");
    mem_set_dram (TEMP_BUF_ADDR, 5, BYTES_PER_PAGE);
    nand_page_program(bank, blk, lowPage, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
    uart_print("done\r\n");

    uart_print("Write 7 to high page...");
    mem_set_dram (TEMP_BUF_ADDR, 7, BYTES_PER_PAGE);
    nand_page_program(bank, blk, highPage, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
    uart_print("done\r\n");

    uart_print("Read all pages...\r\n");
    for(int i=0; i<16; i++)
        nand_page_read(bank, blk, i, FTL_BUF(bank));

    for(int run=0; run<4; run++){
        uart_print("Run ");
        uart_print_int(run);
        highPage = 6+2*run;
        lowPage = 3+2*run;

        uart_print("\r\nHigh page: ");
        uart_print_int(highPage);
        uart_print("\r\nLow page: ");
        uart_print_int(lowPage);
        uart_print("\r\n");

        uart_print("Write 5 to low page...");
        mem_set_dram (TEMP_BUF_ADDR, 5, BYTES_PER_PAGE);
        nand_page_program(bank, blk, lowPage, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
        uart_print("done\r\n");

        //uart_print("Read low page...");
        //nand_page_read(bank, blk, lowPage, FTL_BUF(bank));
        //uart_print_buf(FTL_BUF(bank), BYTES_PER_PAGE);
        //uart_print("done\r\n");

        //uart_print("Read high page...");
        //nand_page_read(bank, blk, highPage, FTL_BUF(bank));
        //uart_print_buf(FTL_BUF(bank), BYTES_PER_PAGE);
        //uart_print("done\r\n");

        uart_print("Read all pages...\r\n");
        for(int i=0; i<16; i++)
            nand_page_read(bank, blk, i, FTL_BUF(bank));

        uart_print("Write 7 to high page...");
        mem_set_dram (TEMP_BUF_ADDR, 7, BYTES_PER_PAGE);
        nand_page_program(bank, blk, highPage, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
        uart_print("done\r\n");

        //uart_print("Read low page...");
        //nand_page_read(bank, blk, lowPage, FTL_BUF(bank));
        //uart_print_buf(FTL_BUF(bank), BYTES_PER_PAGE);
        //uart_print("done\r\n");

        //uart_print("Read high page...");
        //nand_page_read(bank, blk, highPage, FTL_BUF(bank));
        //uart_print_buf(FTL_BUF(bank), BYTES_PER_PAGE);
        //uart_print("done\r\n");

        uart_print("Read all pages...\r\n");
        for(int i=0; i<16; i++)
            nand_page_read(bank, blk, i, FTL_BUF(bank));
    }

    highPage = 16;
    lowPage = 13;

    uart_print("Write 5 to low page...");
    mem_set_dram (TEMP_BUF_ADDR, 5, BYTES_PER_PAGE);
    nand_page_program(bank, blk, lowPage, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
    uart_print("done\r\n");

    uart_print("Write 7 to high page...");
    mem_set_dram (TEMP_BUF_ADDR, 7, BYTES_PER_PAGE);
    nand_page_program(bank, blk, highPage, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
    uart_print("done\r\n");

    uart_print("Read all pages...\r\n");
    for(int i=0; i<16; i++)
        nand_page_read(bank, blk, i, FTL_BUF(bank));

    uart_print("\r\nErase blk...");
    nand_block_erase_sync(bank, blk);
    uart_print("done\r\n");

    //uart_print("Reading and writing from DRAM: ");
    //write_dram_32(TEMP_BUF_ADDR, 20);
    //int result = read_dram_32(TEMP_BUF_ADDR);
    //uart_print_int(result);
    //uart_print("\r\n");
//
    //uart_print ("Writing 1-8 to chunks in a page apart from 4...");
    //for(int chunk=0; chunk< CHUNKS_PER_PAGE; chunk++){
        //for(int word=0; word<BYTES_PER_CHUNK/4; word++){
            //int byte=word*4;
            //if (chunk==4)
                //write_dram_32(TEMP_BUF_ADDR+chunk*BYTES_PER_CHUNK+byte, 9);
            //else
                //write_dram_32(TEMP_BUF_ADDR+chunk*BYTES_PER_CHUNK+byte, chunk);
        //}
    //}
    //nand_block_erase_sync(0,12);
    //nand_block_erase_sync(0,13);
    //nand_page_program(0, 12, 0, TEMP_BUF_ADDR);
    //flash_finish();
    //mem_set_dram (TEMP_BUF_ADDR, 0xFFFFFFFF, BYTES_PER_PAGE);
    //nand_page_read(0, 12, 0, TEMP_BUF_ADDR);
    //uart_print("Page read from flash:\r\n");
    //uart_print_buf(TEMP_BUF_ADDR, BYTES_PER_PAGE);
//
    //for(int word=0; word<BYTES_PER_CHUNK/4; word++){
        //int byte=word*4;
        //write_dram_32(TEMP_BUF_ADDR+byte, 4);
    //}
    //start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_0);
    //nand_page_modified_copyback(0, 12, 0, 13, 0, 4*SECTORS_PER_CHUNK, TEMP_BUF_ADDR, BYTES_PER_CHUNK);
    //UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH1);
    //UINT32 nTicks = 0xFFFFFFFF - timerValue;
//
    //nand_page_read(0, 13, 0, TEMP_BUF_ADDR);
    //uart_print("New page read from flash:\r\n");
    //uart_print_buf(TEMP_BUF_ADDR, BYTES_PER_PAGE);
//
    //uart_print("Modified copyback operation completed in: ");
    //uart_print_int(nTicks);
    //uart_print(" ticks (11.43 ns per tick)\r\n");


    //while (1);
    //uart_print("Now TEMP BUF will be written to flash...");
    //uart_print("done\r\n");
    ////for(int chunk=CHUNKS_PER_PAGE-1; chunk>=0; chunk--){
        //int chunk=3;
        //uart_print("\r\nReading chunk number ");
        //uart_print_int(chunk);
        //uart_print(" that should contain ");
        //uart_print_int(chunk);
        //uart_print(" in first position in buffer\r\n");
        //nand_page_ptread(0, 12, 0, chunk*SECTORS_PER_CHUNK, SECTORS_PER_CHUNK, TEMP_BUF_ADDR-chunk*BYTES_PER_CHUNK, RETURN_WHEN_DONE);
        //flash_finish();
        //uart_print("done\r\n");
        //uart_print("Current content of TEMP BUF:");
//
//
        //chunk=5;
        //uart_print("\r\nReading chunk number ");
        //uart_print_int(chunk);
        //uart_print(" that should contain ");
        //uart_print_int(chunk);
        //uart_print(" in first position in buffer\r\n");
        //nand_page_ptread(0, 12, 0, chunk*SECTORS_PER_CHUNK, SECTORS_PER_CHUNK, TEMP_BUF_ADDR, RETURN_WHEN_DONE);
        //flash_finish();
        //uart_print("done\r\n");
        //uart_print("Current content of TEMP BUF:");
//
        //for(int word=0; word<BYTES_PER_PAGE/4; word++){
            //int byte_offset=word*4;
            //if(word%1024==0){
                //uart_print("\r\n\r\n\r\nChunk ");
                //uart_print_int(word/1024);
                //uart_print(":\r\n");
            //}
            //else{
                //if(word%32==0){
                    //uart_print("\r\n");
                //}
            //}
            //uart_print_int(read_dram_32(TEMP_BUF_ADDR+byte_offset));
            //uart_print(" ");
        //}
    //}
}
*/

/*
static void uart_print_buf(const UINT32 startAddr, const UINT32 bytes)
{
    for(int word=0; word<bytes/4; word++)
    {
        int byte_offset=word*4;
        if(word%1024==0)
        {
            uart_print_level_1("\r\n\r\n\r\nChunk ");
            uart_print_level_1_int(word/1024);
            uart_print_level_1(":\r\n");
        }
        else
        {
            if(word%32==0) uart_print_level_1("\r\n");
        }
        uart_print_level_1_int(read_dram_32(startAddr+byte_offset));
        uart_print_level_1(" ");
    }
}
*/

static void init_metadata_sram (void)
{
    uart_print("initialize metadata in SRAM...");
    for (UINT32 bank = 0; bank < NUM_BANKS; bank++)
    {
        free_list_head[bank]=0;
        free_list_tail[bank]=0;
    }
    uart_print(" done\r\n");
    uart_print("Initializing valid chunks heap...");
    heapDataWrite.dramStartAddr = HEAP_VALID_CHUNKS_ADDR_WRITE;
    heapDataWrite.positionsPtr = HEAP_VALID_CHUNKS_POSITIONS_WRITE;
    ValidChunksHeapInit(&heapDataWrite, LOG_BLK_PER_BANK, 0, CHUNKS_PER_LOG_BLK);
#if Overwrite
    heapDataOverwrite.dramStartAddr = HEAP_VALID_CHUNKS_ADDR_OVERWRITE;
    heapDataOverwrite.positionsPtr = HEAP_VALID_CHUNKS_POSITIONS_OVERWRITE;
    ValidChunksHeapInit(&heapDataOverwrite, LOG_BLK_PER_BANK, 0, CHUNKS_PER_LOG_BLK-8);
#endif
    //heapTest(&heapDataWrite);
    //ValidChunksHeapInit(&heapDataWrite, LOG_BLK_PER_BANK, 0, CHUNKS_PER_LOG_BLK);
    uart_print("done\r\n");
    uart_print("Initializing log...");
    initLog();
    uart_print("done\r\n");
}

// flush FTL metadata(SRAM+DRAM) for normal POR
void ftl_flush (void)
{}


void ftl_isr (void)
{
    UINT32 bank;
    UINT32 bsp_intr_flag;
    uart_print("BSP interrupt occured...\r\n");
    SETREG (APB_INT_STS, INTR_FLASH); // interrupt pending clear (ICU)
    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        int count=0;
        while (BSP_FSM (bank) != BANK_IDLE)
        {
            count++;
            if (count > 100000)
            {
                uart_print("Warning on ftl_isr, waiting for bank "); uart_print_int(bank); uart_print("\r\n");
                count=0;
            }
        }
        bsp_intr_flag = BSP_INTR(bank); // get interrupt flag from BSP
        if (bsp_intr_flag == 0)
        {
            continue;
        }
        UINT32 fc = GETREG(BSP_CMD (bank));
        CLR_BSP_INTR(bank, bsp_intr_flag); // BSP clear
        if (bsp_intr_flag & FIRQ_DATA_CORRUPT)
        {
            uart_print("BSP interrupt at bank: ");
            uart_print_int(bank);
            uart_print("\r\nFIRQ_DATA_CORRUPT occured...vblock ");
            uart_print_int(GETREG (BSP_ROW_H (bank)));
            uart_print(", page ");
            uart_print_int(GETREG (BSP_ROW_H (bank)) % PAGES_PER_BLK);
            uart_print("\r\n");
            //g_bsp_isr_flag[bank] = GETREG (BSP_ROW_H (bank)) / PAGES_PER_BLK;
            g_bsp_isr_flag[bank] = INVALID; // Don't want to interfere with erase errors during BER testing
        }
        if (bsp_intr_flag & (FIRQ_BADBLK_H | FIRQ_BADBLK_L))
        {
            if (fc == FC_COL_ROW_IN_PROG || fc == FC_IN_PROG || fc == FC_PROG)
            {
                uart_print("BSP interrupt at bank: ");
                uart_print_int(bank);
                uart_print("\r\n");
                uart_print("find runtime bad block when block program...");
                uart_print("\r\n");
            }
            else
            {
                if(fc == FC_ERASE)
                {
                    uart_print("BSP interrupt at bank: ");
                    uart_print_int(bank);
                    uart_print("\r\n");
                    uart_print("find runtime bad block when block erase...vblock #: ");
                    uart_print_int(GETREG (BSP_ROW_H (bank)) / PAGES_PER_BLK);
                    uart_print("\r\n");
                    g_bsp_isr_flag[bank] = GETREG (BSP_ROW_H (bank)) / PAGES_PER_BLK;
                }
                else
                {
                    uart_print("BSP interrupt at bank: ");
                    uart_print_int(bank);
                    uart_print(" during command: ");
                    uart_print_int(fc);
                    uart_print("\r\n");
                }
            }
        }
        else
        {
            uart_print("BSP interrupt at bank: ");
            uart_print_int(bank);
            uart_print(" with flag: ");
            uart_print_int(bsp_intr_flag);
            uart_print("\r\n");
        }
    }
    uart_print("\r\n");
}

void ftl_trim (UINT32 const lba, UINT32 const num_sectors) {
    uart_print("\r\n\r\nftl_trim lba="); uart_print_int(lba);
    uart_print(", num_sectors="); uart_print_int(num_sectors); uart_print("\r\n");
    UINT32 num_sectors_ = num_sectors;
    int count=0;
    while(g_ftl_write_buf_id != GETREG(BM_WRITE_LIMIT))
    {
        count++;
        if (count > 100000)
        {
            uart_print_level_1("Warning in ftl_trim, waiting for buffer pointers alignement.\r\n");
            uart_print_level_1("\tFTL Buf Ptr = "); uart_print_level_1_int(g_ftl_write_buf_id); uart_print_level_1("\r\n");
            uart_print_level_1("\tHW Buf Ptr = "); uart_print_level_1_int(GETREG(BM_WRITE_LIMIT)); uart_print_level_1("\r\n");
            count=0;
        }
    }
    int numSataBuffers = (num_sectors_ + SECTORS_PER_PAGE - 1) / SECTORS_PER_PAGE;
    for (int sataBufN=0; sataBufN<numSataBuffers; ++sataBufN)
    {
        UINT32 sataBufIndex = (g_ftl_write_buf_id + sataBufN) % NUM_WR_BUFFERS;
        UINT32 sectorsInCurrentBuf = num_sectors_ > SECTORS_PER_PAGE ? SECTORS_PER_PAGE : num_sectors_;
        for (int i=0; i<(sectorsInCurrentBuf * BYTES_PER_SECTOR)/4; i=i+2)
        {
            UINT32 rangeToTrim = (read_dram_32(WR_BUF_PTR(sataBufIndex) + (i+1)*4) >> 16) & 0x0000FFFF;
            if (rangeToTrim != 0)
            {
                UINT32 lbaToTrim = read_dram_32(WR_BUF_PTR(sataBufIndex) + i*4); // Should prepend the bits 15:0 of previous word
                if (lbaToTrim <  NUM_BANKS * DATA_BLK_PER_BANK * SECTORS_PER_VBLK)
                {
                    trimRange(lbaToTrim, rangeToTrim);
                }
                else
                {
                    uart_print("terr "); uart_print_int(lbaToTrim); uart_print(" "); uart_print_int(rangeToTrim); uart_print("\r\n");
                }
            }
        }
        num_sectors_ = num_sectors_ - sectorsInCurrentBuf;
    }
    g_ftl_write_buf_id = (g_ftl_write_buf_id + numSataBuffers) % NUM_WR_BUFFERS;
    SETREG(BM_STACK_WRSET, g_ftl_write_buf_id);    // change bm_write_limit
    SETREG(BM_STACK_RESET, 0x01);                // change bm_write_limit
}

static void trimRange(const UINT32 lba, const UINT32 nSectors)
{
    //uart_print_level_1("t "); uart_print_level_1_int(lba); uart_print_level_1(" "); uart_print_level_1_int(nSectors); uart_print_level_1("\r\n");
    uart_print("trimRange lba="); uart_print_int(lba); uart_print(" num_sectors="); uart_print_int(nSectors); uart_print("\r\n");
    UINT32 lpn = lba / SECTORS_PER_PAGE;
    UINT32 sectOffset = lba % SECTORS_PER_PAGE;
    UINT32 remainingSectors = nSectors;
    int count=0;
    while (remainingSectors != 0)
    {
        count++;
        if (count > 100000)
        {
            uart_print_level_1("Warning0 on trimRange\r\n");
            count=0;
        }
        UINT32 chunkIdx = sectOffset / SECTORS_PER_CHUNK;
        int count1=0;
        while (remainingSectors != 0 && chunkIdx < CHUNKS_PER_PAGE)
        {
            count1++;
            if (count1 > 100000)
            {
                uart_print_level_1("Warning1 on trimRange\r\n");
                count1=0;
            }
            UINT32 sectOffsetInChunk = sectOffset % SECTORS_PER_CHUNK;
            UINT32 nSectsToTrim = ((sectOffsetInChunk + remainingSectors) < SECTORS_PER_CHUNK) ? remainingSectors : (SECTORS_PER_CHUNK - sectOffsetInChunk);
            if (sectOffsetInChunk != 0 || nSectsToTrim < SECTORS_PER_CHUNK)
            {
                uart_print_level_1("Warning! Trim not on a chunk boundary\r\n");
            }
            else
            {
                if (ChunksMapTable(lpn , chunkIdx) > DRAM_BASE + DRAM_SIZE)
                {
                    uart_print_level_1("ERROR in trimRange 1: reading above DRAM address space\r\n");
                }
                UINT32 oldChunkAddr = read_dram_32(ChunksMapTable(lpn , chunkIdx));
                switch (findChunkLocation(oldChunkAddr))
                {
                    case Invalid:
                    {
                        uart_print("Warning: Trim invalid chunk\r\n");
                    } break;
                    case FlashWLog:
                    {
                        UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
                        decrementValidChunks(&heapDataWrite, oldChunkBank, ChunkToLbn(oldChunkAddr));
                    } break;
                    case DRAMWLog:
                    {
                        UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
                        logBufMeta[oldChunkBank].dataLpn[oldChunkAddr % CHUNKS_PER_PAGE] = INVALID;
                    } break;
#if Overwrite
                    case FlashOwLog:
                    {
                        oldChunkAddr = oldChunkAddr & 0x7FFFFFFF;
                        UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
                        decrementValidChunks(&heapDataOverwrite, oldChunkBank, ChunkToLbn(oldChunkAddr));
                    } break;
                    case DRAMOwLog:
                    {
                        oldChunkAddr = oldChunkAddr & 0x7FFFFFFF;
                        UINT32 oldChunkBank = ChunkToBank(oldChunkAddr);
                        owLogBufMeta[oldChunkBank].dataLpn[oldChunkAddr % CHUNKS_PER_PAGE] = INVALID;
                    } break;
#endif
                }
                //write_dram_32(&node->chunks[chunkIdx], INVALID);
                write_dram_32(ChunksMapTable(lpn, chunkIdx), INVALID);
            }
            remainingSectors-=nSectsToTrim;
            chunkIdx++;
            sectOffset+=nSectsToTrim;
        }
        lpn++;
        sectOffset=0;
    }
}

#if Overwrite
    const UINT32 overwriteBitMask = ((0x00000001) << overwriteBitPosition);
    const UINT32 overwriteLbaMask = ~((0xFFFFFFFF) << overwriteBitPosition);
#endif

void ftl_read (UINT32 const lba, UINT32 const num_sectors)
{
    //uart_print_level_1("r "); uart_print_level_1_int(lba); uart_print_level_1(" "); uart_print_level_1_int(num_sectors); uart_print_level_1("\r\n");
    //uart_print_level_1_int(num_sectors); uart_print_level_1("\r\n");
    //uart_print_level_1_int(num_sectors); uart_print_level_1(" ");
    //uart_print_level_1_int(g_ftl_read_buf_id); uart_print_level_1(" ");
    uart_print("\r\n\r\nftl_read lba="); uart_print_int(lba);
    uart_print(", num_sectors="); uart_print_int(num_sectors); uart_print("\r\n");
#if Overwrite
    UINT32 lpn = (lba & overwriteLbaMask) / SECTORS_PER_PAGE;
    UINT32 sect_offset = (lba & overwriteLbaMask) % SECTORS_PER_PAGE;
#else
    UINT32 lpn = lba / SECTORS_PER_PAGE;
    UINT32 sect_offset = lba % SECTORS_PER_PAGE;
#endif
    UINT32 remain_sects = num_sectors;
    int count=0;
    while (remain_sects != 0)
    {
        count++;
        if (count > 100000)
        {
            uart_print_level_1("Warning in ftl_read\r\n");
            count=0;
        }
        UINT32 num_sectors_to_read = ((sect_offset + remain_sects) < SECTORS_PER_PAGE) ?  remain_sects : (SECTORS_PER_PAGE - sect_offset);

        readFromLogBlk(lpn, sect_offset, num_sectors_to_read);
        sect_offset = 0;
        remain_sects -= num_sectors_to_read;
        lpn++;
    }
}

void ftl_write (UINT32 const lba, UINT32 const nSects)
{
    //uart_print_level_1("w "); uart_print_level_1_int(lba); uart_print_level_1(" "); uart_print_level_1_int(nSects); uart_print_level_1("\r\n");
    uart_print("\r\n\r\nftl_write lba="); uart_print_int(lba);
    uart_print(", num_sectors="); uart_print_int(nSects); uart_print("\r\n");
    #if MeasureW
    start_interval_measurement(TIMER_CH2, TIMER_PRESCALE_0);
    #endif
    userSecWrites += nSects;
#if Overwrite
    if ((lba & overwriteBitMask) == 0)
    {
#endif
        //uart_print_level_1("w "); uart_print_level_1_int(lba); uart_print_level_1(" "); uart_print_level_1_int(nSects); uart_print_level_1("\r\n");
        uart_print ("normal write\r\n");
        /*if(nSects>500)
        {
            writeToSWBlk(lba, nSects);
            backgroundCleaning();
            #if MeasureW
            UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH2);
            UINT32 nTicks = 0xFFFFFFFF - timerValue;
            uart_print_level_2("WR "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
            #endif
            return;
        }*/

        UINT32 lpn = lba / SECTORS_PER_PAGE;
        UINT32 sectOffset = lba % SECTORS_PER_PAGE;
        UINT32 remainingSects = nSects;
        int count=0;
        while (remainingSects != 0)
        {
            count++;
            if (count > 100000)
            {
                uart_print_level_1("Warning in ftl_write\r\n");
                count=0;
            }
            UINT32 nSectsToWrite = ((sectOffset + remainingSects) < SECTORS_PER_PAGE) ? remainingSects : (SECTORS_PER_PAGE - sectOffset);
            writeToLogBlk(lpn, sectOffset, nSectsToWrite);
            sectOffset = 0;
            remainingSects -= nSectsToWrite;
            lpn++;
        }
        #if MeasureW
        UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH2);
        UINT32 nTicks = 0xFFFFFFFF - timerValue;
        uart_print_level_2("WR "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
        #endif
#if Overwrite
    }
    else
    {
        //uart_print_level_1("o "); uart_print_level_1_int(lba & overwriteLbaMask); uart_print_level_1(" "); uart_print_level_1_int(nSects); uart_print_level_1("\r\n");
        uart_print ("overwrite\r\n");
        UINT32 lpn = (lba & overwriteLbaMask) / SECTORS_PER_PAGE;
        UINT32 sectOffset = (lba & overwriteLbaMask) % SECTORS_PER_PAGE;
        UINT32 remainingSects = nSects;
        int count=0;
        while (remainingSects != 0)
        {
            count++;
            if (count > 100000)
            {
                uart_print_level_1("Warning in ftl_write\r\n");
                count=0;
            }
            UINT32 nSectsToWrite = ((sectOffset + remainingSects) < SECTORS_PER_PAGE) ? remainingSects : (SECTORS_PER_PAGE - sectOffset);
            overwriteToLogBlk(lpn, sectOffset, nSectsToWrite);
            sectOffset = 0;
            remainingSects -= nSectsToWrite;
            lpn++;
        }
        #if MeasureW
        UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH2);
        UINT32 nTicks = 0xFFFFFFFF - timerValue;
        uart_print_level_2("OW "); uart_print_level_2_int(nTicks); uart_print_level_2("\r\n");
        #endif
    }
#endif
    backgroundCleaning();
}

static void backgroundCleaning() {
    for(UINT32 bank=0; bank<NUM_BANKS; ++bank) {
        if (isBankBusy(bank) == FALSE) {
            if (getVictimValidPagesNumber(&heapDataWrite, bank) == 0) {
                #if MeasureGc
                start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_2);
                int nValidChunks = garbageCollectLog(bank);
                UINT32 timerValue=GET_TIMER_VALUE(TIMER_CH1);
                UINT32 nTicks = 0xFFFFFFFF - timerValue;
                uart_print_level_2("BC "); uart_print_level_2_int(bank);
                uart_print_level_2(" "); uart_print_level_2_int(nTicks);
                uart_print_level_2(" "); uart_print_level_2_int(nValidChunks); uart_print_level_2("\r\n");
                #else
                garbageCollectLog(bank);
                #endif
            }
        }
    }
}


BOOL32 is_bad_block (UINT32 const bank, UINT32 const vblk_offset)
{
    if (tst_bit_dram (BAD_BLK_BMP_ADDR + bank * (VBLKS_PER_BANK / 8 + 1), vblk_offset) == FALSE)
    {
        return FALSE;
    }
    return TRUE;
}

static void set_bad_block (UINT32 const bank, UINT32 const vblk_offset)
{
    set_bit_dram(BAD_BLK_BMP_ADDR + bank * (VBLKS_PER_BANK / 8 + 1), vblk_offset);
}
