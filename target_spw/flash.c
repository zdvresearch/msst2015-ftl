// Copyright 2011 INDILINX Co., Ltd.
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


#include "jasmine.h"

const UINT8 c_bank_map[NUM_BANKS] = BANK_MAP;
//UINT8 c_bank_rmap[NUM_BANKS_MAX] = BANK_RMAP;

void flash_reset_one_bank(UINT32 bank);

UINT8 isBankBusy(UINT32 const bank){
    UINT32 rbank = REAL_BANK(bank);
    if (_BSP_FSM(rbank) == BANK_IDLE) {
        uart_print("Bank "); uart_print_int(bank); uart_print(" is idle\r\n");
        return FALSE;
    } else {
        uart_print("Bank "); uart_print_int(bank); uart_print(" is busy\r\n");
        return TRUE;
    }
}

void dumpBankStates(UINT32 const bank){
    UINT32 rbank = REAL_BANK(bank);
    while ((UINT8)_BSP_FSM(rbank) != (UINT8)BANK_IDLE) { // wait until the target bank finishes the command
        uart_print_level_1_int((UINT8)_BSP_FSM(rbank)); uart_print_level_1(" ");
    }
    uart_print_level_1("\r\n");
}

void predumpBankStates(UINT32 const bank){
    uart_print_level_1("predumpBankStates bank "); uart_print_level_1_int(bank); uart_print_level_1(": ");
    UINT32 rbank = REAL_BANK(bank);
    UINT32 count = 0;
    while (1) { // wait until the target bank finishes the command
        count++;
        uart_print_level_1_int((UINT8)_BSP_FSM(rbank)); uart_print_level_1(" ");
        if (count == 30) {
            break;
        }
    }
    uart_print_level_1("\r\n");
}

void waitBusyBank(UINT32 const bank)
{
    uart_print("Wait bank "); uart_print_int(bank);
    {
        int count=0;
        while ((GETREG(WR_STAT) & 0x00000001) != 0)
        {
            count++;
            if (count == 1000000)
            {
                count = 0;
                uart_print_level_1("Warning in waitBusyBank on bank="); uart_print_level_1_int(bank); uart_print_level_1(": slow to empty waiting room\r\n");
            }
        }
        count=0;
        UINT32 rbank = REAL_BANK(bank);
        while (_BSP_FSM(rbank) != BANK_IDLE)
        {
            count++;
            if (count == 1000000)
            {
                count = 0;
                uart_print_level_1("Warning in waitBusyBank on bank="); uart_print_level_1_int(bank); uart_print_level_1(": slow to return idle\r\n");
            }
        }
    }
    uart_print(" finished\r\n");
    return;
}

void flash_issue_cmd(UINT32 const bank, UINT32 const sync) {
    UINT32 rbank = REAL_BANK(bank);

    SETREG(FCP_BANK, rbank);

    // You should not issue a new command when Waiting Room is not empty.
    int count = 0;
    while ((GETREG(WR_STAT) & 0x00000001) != 0) {
        count ++;
        //if (count == 1000000) {
            //count = 0;
            //uart_print_level_1("Warning: waiting too long for WR to be empty before issuing operation on bank "); uart_print_level_1_int(bank);
            //uart_print_level_1(", real bank "); uart_print_level_1_int(rbank); uart_print_level_1("\r\n");
            //uart_print_level_1("Bank states:\r\n");
            //for (int i=0; i<NUM_BANKS; i++) {
                //uart_print_level_1("bank "); uart_print_level_1_int(i);
                //uart_print_level_1(", real bank "); uart_print_level_1_int(REAL_BANK(i));
                //uart_print_level_1(" state "); uart_print_level_1_int(_BSP_FSM(REAL_BANK(i))); uart_print_level_1("\r\n");
            //}
        //}
        if (count == 1000000) {
            count = 0;
            uart_print_level_1("Warning: waiting too long for WR to be empty before issuing operation on bank "); uart_print_level_1_int(bank);
            uart_print_level_1(", real bank "); uart_print_level_1_int(rbank); uart_print_level_1("\r\n");
            for (int i=0; i<NUM_BANKS; i++) {
                if (_BSP_FSM(REAL_BANK(i)) == BANK_TAKE) {
                    uart_print_level_1("Bank "); uart_print_level_1_int(i);
                    uart_print_level_1(" (real bank "); uart_print_level_1_int(REAL_BANK(i));
                    uart_print_level_1(") in state: "); uart_print_level_1_int((UINT8)_BSP_FSM(REAL_BANK(i))); uart_print_level_1("\r\n");
                    uart_print_level_1("Try reset\r\n");
                    flash_reset_one_bank(i);
                    break;
                }
            }
        }
    }

    // If you write any value to FCP_ISSUE, FCP register contents are copied to Waiting Room.
    // The hardware does not read FCP registers unless you write any value to FCP_ISSUE.
    // The hardware never changes the values of FCP registers unless reset.
    // The value written to FCP_ISSUE is not significant.
    SETREG(FCP_ISSUE, NULL);

    if (sync == RETURN_ON_ISSUE)
        return;

    // wait until the new command is accepted by the target bank
    count = 0;
    while ((GETREG(WR_STAT) & 0x00000001) != 0) {
        count ++;
        //if (count == 1000000) {
            //count = 0;
            //uart_print_level_1("Warning: waiting too long for WR to be empty after issuing operation on bank "); uart_print_level_1_int(bank);
            //uart_print_level_1(", real bank "); uart_print_level_1_int(rbank); uart_print_level_1("\r\n");
            //uart_print_level_1("Bank states:\r\n");
            //for (int i=0; i<NUM_BANKS; i++) {
                //uart_print_level_1("bank "); uart_print_level_1_int(i);
                //uart_print_level_1(", real bank "); uart_print_level_1_int(REAL_BANK(i));
                //uart_print_level_1(" state "); uart_print_level_1_int(_BSP_FSM(REAL_BANK(i))); uart_print_level_1("\r\n");
            //}
        //}
        if (count == 1000000) {
            count = 0;
            uart_print_level_1("Warning: waiting too long for WR to be empty after issuing operation on bank "); uart_print_level_1_int(bank);
            uart_print_level_1(", real bank "); uart_print_level_1_int(rbank); uart_print_level_1("\r\n");
            for (int i=0; i<NUM_BANKS; i++) {
                if (_BSP_FSM(REAL_BANK(i)) == BANK_TAKE) {
                    uart_print_level_1("Bank "); uart_print_level_1_int(bank);
                    uart_print_level_1(" (real bank "); uart_print_level_1_int(rbank);
                    uart_print_level_1(") in state: "); uart_print_level_1_int((UINT8)_BSP_FSM(REAL_BANK(i))); uart_print_level_1("\r\n");
                    uart_print_level_1("Try reset\r\n");
                    flash_reset_one_bank(i);
                    break;
                }
            }
        }
    }

    if (sync == RETURN_ON_ACCEPT)
        return;

    // wail until the target bank finishes the command
    //count = 0;
    //while (_BSP_FSM(rbank) != BANK_IDLE) {
        //count ++;
        //if (count == 1000000) {
            //count = 0;
            //uart_print_level_1("Warning: waiting too long bank "); uart_print_level_1_int(bank);
            //uart_print_level_1(", real bank "); uart_print_level_1_int(rbank); uart_print_level_1("to become idle after operation\r\n");
            //uart_print_level_1("Bank state: "); uart_print_level_1_int(_BSP_FSM(rbank)); uart_print_level_1("\r\n");
            //uart_print_level_1("Bank states:\r\n");
            //for (int i=0; i<NUM_BANKS; i++) {
                //uart_print_level_1("bank "); uart_print_level_1_int(i);
                //uart_print_level_1(", real bank "); uart_print_level_1_int(REAL_BANK(i));
                //uart_print_level_1(" state "); uart_print_level_1_int(_BSP_FSM(REAL_BANK(i))); uart_print_level_1("\r\n");
            //}
        //}
    //}
    count = 0;
    while (_BSP_FSM(rbank) != BANK_IDLE) { // wait until the target bank finishes the command
        if (_BSP_FSM(rbank) == BANK_TAKE) {
            count ++;
            if (count == 1000000) {
                count = 0;
                uart_print_level_1("After operation, bank "); uart_print_level_1_int(bank);
                uart_print_level_1(" (real bank "); uart_print_level_1_int(rbank); uart_print_level_1("\r\n");
                uart_print_level_1(") is stuck in state: "); uart_print_level_1_int((UINT8)_BSP_FSM(rbank)); uart_print_level_1("\r\n");
                uart_print_level_1("Try reset\r\n");
                flash_reset_one_bank(bank);
            }
        }
    }
}

void flash_copy(UINT32 const bank, UINT32 const dst_row, UINT32 const src_row)
{
    SETREG(FCP_CMD, FC_COPYBACK);
    SETREG(FCP_BANK, REAL_BANK(bank));
    SETREG(FCP_OPTION, FO_P | FO_E | FO_B_W_DRDY);
    SETREG(FCP_COL, 0);
    SETREG(FCP_ROW_L(bank), src_row);
    SETREG(FCP_ROW_H(bank), src_row);
    SETREG(FCP_DST_COL, 0);
    SETREG(FCP_DST_ROW_H, dst_row);
    SETREG(FCP_DST_ROW_L, dst_row);
    int count=0;
    while ((GETREG(WR_STAT) & 0x00000001) != 0) {
        count++;
        if (count>100000) {
            uart_print_level_1("Warning in flash_copy\r\n");
            count=0;
        }
    }
    SETREG(FCP_ISSUE, NULL);
}

void flash_modify_copy(UINT32 const bank, UINT32 const dst_row, UINT32 const src_row,
                UINT32 const sct_offset, UINT32 dma_addr, UINT32 const dma_count)
{
    dma_addr -= sct_offset*BYTES_PER_SECTOR;

    SETREG(FCP_CMD, FC_MODIFY_COPYBACK);
    SETREG(FCP_BANK, REAL_BANK(bank));
    SETREG(FCP_OPTION, FO_P | FO_E | FO_B_W_DRDY);
    SETREG(FCP_DMA_ADDR, dma_addr);
    SETREG(FCP_DMA_CNT, dma_count);
    SETREG(FCP_COL, 0);
    SETREG(FCP_ROW_L(bank), src_row);
    SETREG(FCP_ROW_H(bank), src_row);
    SETREG(FCP_DST_COL, sct_offset);
    SETREG(FCP_DST_ROW_H, dst_row);
    SETREG(FCP_DST_ROW_L, dst_row);
    int count=0;
    while ((GETREG(WR_STAT) & 0x00000001) != 0) {
        count++;
        if (count>100000) {
            uart_print_level_1("Warning in flash_modify_copy\r\n");
            count=0;
        }
    }
    SETREG(FCP_ISSUE, NULL);
}

void flash_erase(UINT32 const bank, UINT16 const vblk_offset)
{
    ASSERT(vblk_offset > 0 && vblk_offset < VBLKS_PER_BANK);

    SETREG(FCP_CMD, FC_ERASE);
    SETREG(FCP_BANK, REAL_BANK(bank));
    SETREG(FCP_OPTION, FO_P);
    SETREG(FCP_ROW_H(bank), vblk_offset * PAGES_PER_VBLK);
    SETREG(FCP_ROW_L(bank), vblk_offset * PAGES_PER_VBLK);
    int count=0;
    while ((GETREG(WR_STAT) & 0x00000001) != 0) {
        count++;
        if (count>100000) {
            uart_print_level_1("Warning in flash_erase\r\n");
            count=0;
        }
    }
    SETREG(FCP_ISSUE, NULL);
}

void flash_finish(void)
{
    // When the value of MON_CHABANKIDLE is zero, Waiting Room is empty and all the banks are idle.
    int count=0;
    while (GETREG(MON_CHABANKIDLE) != 0) {
        count++;
        if (count>100000) {
            uart_print_level_1("Warning in flash_finish\r\n");
            count=0;
        }
    }
}

void flash_clear_irq(void)
{
    UINT32 i;

    for (i = 0; i < NUM_BANKS_MAX; i += 4)
    {
        SETREG(BSP_INTR_BASE + i, 0xFFFFFFFF);
    }

    SETREG(APB_INT_STS, INTR_FLASH);
}

void flash_reset(void) {
    uart_print_level_1("flash_reset\r\n");
    UINT32 bank, rbank, ecc_size, nand_cfg_2;

    SETREG(PMU_ResetCon, RESET_FLASH);

    // FCONF_NANDCFG_1

    #if FLASH_TYPE == H27UCG8UDMYR || FLASH_TYPE == H27UBG8T2MYR || FLASH_TYPE == H27UDG8VEMYR ||    \
        FLASH_TYPE == H27UCG8VFATR || FLASH_TYPE == H27UBG8U5ATR || FLASH_TYPE == H27UBG8T2ATR ||    \
        FLASH_TYPE == K9GAG08U0D || FLASH_TYPE == K9LBG08U0D || FLASH_TYPE == K9HCG08U1D ||            \
        FLASH_TYPE == K9GBG08U0M || FLASH_TYPE == K9PFG08U5M
    SETREG(FCONF_NANDCFG_1, 0x18);    // 4Xnm process with LGA TYPE
    #elif NAND_SPEC_MF == NAND_SPEC_SAMSUNG || NAND_SPEC_MF == NAND_SPEC_HYNIX
    {
        #if NAND_SPEC_DIE == NAND_SPEC_DIE_2 && NAND_SPEC_PLANE == NAND_SPEC_PLANE_2 && OPTION_2_PLANE
        {
            #if ROWS_PER_BANK == 262144
            SETREG(FCONF_NANDCFG_1, 0x10 | (1 << 12));        // A30

            #elif ROWS_PER_BANK == 524288
            SETREG(FCONF_NANDCFG_1, 0x10 | (2 << 12));        // A31

            #elif ROWS_PER_BANK == 1048576
            SETREG(FCONF_NANDCFG_1, 0x10 | (3 << 12));        // A32

            #elif ROWS_PER_BANK == 2097152
            SETREG(FCONF_NANDCFG_1, 0x10 | (4 << 12));        // A33

            #elif ROWS_PER_BANK == 4194304
            SETREG(FCONF_NANDCFG_1, 0x10 | (5 << 12));        // A34

            #else
            #error("invalid configuration for 2-plane operation in DDP");
            #endif
        }
        #else
        SETREG(FCONF_NANDCFG_1, 0x10);
        #endif
    }
    #elif NAND_SPEC_MF == NAND_SPEC_MICRON || NAND_SPEC_MF == NAND_SPEC_INTEL
        #if NAND_SPEC_READ == NAND_SPEC_READ_CYCLE_2
        SETREG(FCONF_NANDCFG_1, 0x21);
        #else
        SETREG(FCONF_NANDCFG_1, 0x11);
        #endif
    #elif NAND_SPEC_MF == NAND_SPEC_TOSHIBA
        #if NAND_SPEC_PLANE == NAND_SPEC_PLANE_2
        SETREG(FCONF_NANDCFG_1, 0x12 | 0x08);
        #else
        SETREG(FCONF_NANDCFG_1, 0x12);
        #endif
    #endif

    // FCONF_NANDCFG_2

    #if SPARE_PER_PHYPAGE / SECTORS_PER_PHYPAGE >= 28
    // RS : 12 symbol correction per sector
    // BCH : 16 bit correction per sector
    ecc_size = 2;

    #elif SPARE_PER_PHYPAGE / SECTORS_PER_PHYPAGE >= 22
    // RS : 12 symbol correction per sector
    // BCH : 12 bit correction per sector
    ecc_size = 1;

    #elif SPARE_PER_PHYPAGE / SECTORS_PER_PHYPAGE >= 16
    // RS : 6 symbol correction per sector
    // BCH : 8 bit correction per sector
    ecc_size = 0;

    #else
    #error("ECC configuration error");
    #endif

    nand_cfg_2 = BCH(NAND_SPEC_CELL == NAND_SPEC_CELL_MLC)
                | PG_SIZE(SECTORS_PER_PHYPAGE >> 3)
                | BLK_SIZE(PAGES_PER_VBLK >> 7)
                | ECC_SIZE(ecc_size)
                | CHK_CMD(NAND_SPEC_MF == NAND_SPEC_TOSHIBA && OPTION_2_PLANE)
                | CHK_CODE(0xC0)
                | CHK_MASK(0xC1);

    SETREG(FCONF_NANDCFG_2, nand_cfg_2);
    SETREG(FCONF_TIMECYCLE, FLASH_PARAM_TIMECYCLE_SAFE);
    SETREG(FCONF_BUFSIZE, BYTES_PER_PAGE);
    SETREG(FCONF_PAUSE, 0);
    SETREG(FCONF_CPBDMABASE, COPY_BUF_ADDR);

    SETREG(WR_STAT, 0xFFFFFFFF);

    SETREG(BM_STACK_RDSET, 0);
    SETREG(BM_STACK_WRSET, 0);
    SETREG(BM_STACK_RESET, 0x03);
    SETREG(FTL_READ_PTR, NUM_RD_BUFFERS + 1);

    SETREG(REMAPBANK0, REMAP_BANK_INVALID);
    SETREG(REMAPBANK1, REMAP_BANK_INVALID);
    SETREG(REMAPBANK2, REMAP_BANK_INVALID);
    SETREG(REMAPBANK3, REMAP_BANK_INVALID);

    for (rbank = 0; rbank < NUM_BANKS_MAX; rbank++)
    {
        SETREG(_FCP_ROW_H(rbank), 0xFFFFFFFF);
    }

    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        SETREG(FCP_CMD, FC_GENERIC);
        SETREG(FCP_COL, 0xFF);                // flash reset
        SETREG(FCP_OPTION, 0);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);
    }

    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        SETREG(FCP_CMD, FC_WAIT);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);
    }

    flash_finish();

    flash_clear_irq();

    #if FLASH_TYPE == MT29F64G08CFABA || FLASH_TYPE == MT29F128G08CJABA || FLASH_TYPE == JS29F32G08AAMDB

    #define OVER_DRIVE_2        0
    #define OVER_DRIVE_1        1
    #define DEFAULT_DRIVE        2
    #define UNDER_DRIVE            3

    #define L63B_OVER_DRIVE        OVER_DRIVE_2

    #if L63B_OVER_DRIVE != DEFAULT_DRIVE

    #if L63B_OVER_DRIVE == OVER_DRIVE_2            // Overdrive 2
    mem_set_sram(g_temp_mem, 0x00000000, sizeof(UINT32) * 2);
    #elif L63B_OVER_DRIVE == OVER_DRIVE_1        // Overdrive 1
    mem_set_sram(g_temp_mem, 0x00000101, sizeof(UINT32));
    mem_set_sram(g_temp_mem + sizeof(UINT32), 0x00000000, sizeof(UINT32));
    #elif L63B_OVER_DRIVE == DEFAULT_DRIVE        // Nominal(default)
    mem_set_sram(g_temp_mem, 0x00000202, sizeof(UINT32));
    mem_set_sram(g_temp_mem + sizeof(UINT32), 0x00000000, sizeof(UINT32));
    #else                                        // Underdrvie
    mem_set_sram(g_temp_mem, 0x00000303, sizeof(UINT32));
    mem_set_sram(g_temp_mem + sizeof(UINT32), 0x00000000, sizeof(UINT32));
    #endif

    for (bank = 0; bank < NUM_BANKS; bank++)
    {
        SETREG(FCP_CMD, FC_IN);                        // DIN : P1 : (0x00 : Overdrive 2, 0x01 : Overdrive 1, 0x02 : Nominal(default), 0x03 : Underdrvie)
        SETREG(FCP_DMA_ADDR, g_temp_mem);
        SETREG(FCP_DMA_CNT, 8);
        SETREG(FCP_OPTION, FO_B_W_DRDY);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);

        SETREG(FCP_CMD, FC_GENERIC);                // command : set features
        SETREG(FCP_COL, 0xEF);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);

        SETREG(FCP_CMD, FC_GENERIC_ADDR);            // addr : feature address (Progrmmable Output drive strength)
        SETREG(FCP_DMA_CNT, 2);
        SETREG(FCP_ROW_H(bank),  0x10 << 16);
        SETREG(FCP_ROW_L(bank),  0x10 << 16);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);

        SETREG(FCP_CMD, FC_IN);                        // DIN : P1 : (0x00 : Overdrive 2, 0x01 : Overdrive 1, 0x02 : Nominal(default), 0x03 : Underdrvie)
        SETREG(FCP_DMA_CNT, 8);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);

        SETREG(FCP_CMD, FC_WAIT);
        SETREG(FCP_DMA_CNT, 0);
        SETREG(FCP_COL, 0xFF);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);
    }

    flash_finish();
    #endif
    #endif

    #if CLOCK_SPEED > 165000000
    const UINT32 read_cycle = 2;
    #else
    const UINT32 read_cycle = 1;
    #endif

    const UINT32 time_param = FLASH_SETUP(FLASH_PARAM_SETUP) | FLASH_HOLD(CYCLE_PER_HOLD - 1) | FLASH_CLE_ALE_DLY(1) |
            FLASH_READ_CYCLE_A(read_cycle) | FLASH_READ_CYCLE_B(read_cycle) |
            FLASH_READ_CYCLE_C(read_cycle) | FLASH_READ_CYCLE_D(read_cycle) |
            FLASH_OUT_ENA_DLY(1) | FLASH_WHR(15) | FLASH_ADL(31) | FLASH_RHW(31) | FLASH_ECC_ENABLE;

    SETREG(FCONF_TIMECYCLE, time_param);
    uart_print_level_1("done\r\n");
}

void flash_reset_one_bank(UINT32 bank) {
    uart_print_level_1("flash_reset_one_bank(bank ="); uart_print_level_1_int(bank); uart_print_level_1(")\r\n");

    //UINT32 bank, rbank, ecc_size, nand_cfg_2;

    SETREG(PMU_ResetCon, RESET_FLASH);

    // FCONF_NANDCFG_1

    //#if FLASH_TYPE == H27UCG8UDMYR || FLASH_TYPE == H27UBG8T2MYR || FLASH_TYPE == H27UDG8VEMYR || FLASH_TYPE == H27UCG8VFATR || FLASH_TYPE == H27UBG8U5ATR || FLASH_TYPE == H27UBG8T2ATR || FLASH_TYPE == K9GAG08U0D || FLASH_TYPE == K9LBG08U0D || FLASH_TYPE == K9HCG08U1D FLASH_TYPE == K9GBG08U0M || FLASH_TYPE == K9PFG08U5M
    //SETREG(FCONF_NANDCFG_1, 0x18);    // 4Xnm process with LGA TYPE
    //#elif NAND_SPEC_MF == NAND_SPEC_SAMSUNG || NAND_SPEC_MF == NAND_SPEC_HYNIX
    //{
        //#if NAND_SPEC_DIE == NAND_SPEC_DIE_2 && NAND_SPEC_PLANE == NAND_SPEC_PLANE_2 && OPTION_2_PLANE
        //{
            //#if ROWS_PER_BANK == 262144
            //SETREG(FCONF_NANDCFG_1, 0x10 | (1 << 12));        // A30
//
            //#elif ROWS_PER_BANK == 524288
            //SETREG(FCONF_NANDCFG_1, 0x10 | (2 << 12));        // A31
//
            //#elif ROWS_PER_BANK == 1048576
            //SETREG(FCONF_NANDCFG_1, 0x10 | (3 << 12));        // A32
//
            //#elif ROWS_PER_BANK == 2097152
            //SETREG(FCONF_NANDCFG_1, 0x10 | (4 << 12));        // A33
//
            //#elif ROWS_PER_BANK == 4194304
            //SETREG(FCONF_NANDCFG_1, 0x10 | (5 << 12));        // A34
//
            //#else
            //#error("invalid configuration for 2-plane operation in DDP");
            //#endif
        //}
        //#else
        //SETREG(FCONF_NANDCFG_1, 0x10);
        //#endif
    //}
    //#elif NAND_SPEC_MF == NAND_SPEC_MICRON || NAND_SPEC_MF == NAND_SPEC_INTEL
        //#if NAND_SPEC_READ == NAND_SPEC_READ_CYCLE_2
        //SETREG(FCONF_NANDCFG_1, 0x21);
        //#else
        //SETREG(FCONF_NANDCFG_1, 0x11);
        //#endif
    //#elif NAND_SPEC_MF == NAND_SPEC_TOSHIBA
        //#if NAND_SPEC_PLANE == NAND_SPEC_PLANE_2
        //SETREG(FCONF_NANDCFG_1, 0x12 | 0x08);
        //#else
        //SETREG(FCONF_NANDCFG_1, 0x12);
        //#endif
    //#endif
//
    //// FCONF_NANDCFG_2
//
    //#if SPARE_PER_PHYPAGE / SECTORS_PER_PHYPAGE >= 28
    //// RS : 12 symbol correction per sector
    //// BCH : 16 bit correction per sector
    //ecc_size = 2;
//
    //#elif SPARE_PER_PHYPAGE / SECTORS_PER_PHYPAGE >= 22
    //// RS : 12 symbol correction per sector
    //// BCH : 12 bit correction per sector
    //ecc_size = 1;
//
    //#elif SPARE_PER_PHYPAGE / SECTORS_PER_PHYPAGE >= 16
    //// RS : 6 symbol correction per sector
    //// BCH : 8 bit correction per sector
    //ecc_size = 0;
//
    //#else
    //#error("ECC configuration error");
    //#endif
//
    //nand_cfg_2 = BCH(NAND_SPEC_CELL == NAND_SPEC_CELL_MLC)
                //| PG_SIZE(SECTORS_PER_PHYPAGE >> 3)
                //| BLK_SIZE(PAGES_PER_VBLK >> 7)
                //| ECC_SIZE(ecc_size)
                //| CHK_CMD(NAND_SPEC_MF == NAND_SPEC_TOSHIBA && OPTION_2_PLANE)
                //| CHK_CODE(0xC0)
                //| CHK_MASK(0xC1);
//
    //SETREG(FCONF_NANDCFG_2, nand_cfg_2);
    //SETREG(FCONF_TIMECYCLE, FLASH_PARAM_TIMECYCLE_SAFE);
    //SETREG(FCONF_BUFSIZE, BYTES_PER_PAGE);
    //SETREG(FCONF_PAUSE, 0);
    //SETREG(FCONF_CPBDMABASE, COPY_BUF_ADDR);
//
    SETREG(WR_STAT, 0xFFFFFFFF);

    //SETREG(BM_STACK_RDSET, 0);
    //SETREG(BM_STACK_WRSET, 0);
    //SETREG(BM_STACK_RESET, 0x03);
    //SETREG(FTL_READ_PTR, NUM_RD_BUFFERS + 1);

    //SETREG(REMAPBANK0, REMAP_BANK_INVALID);
    //SETREG(REMAPBANK1, REMAP_BANK_INVALID);
    //SETREG(REMAPBANK2, REMAP_BANK_INVALID);
    //SETREG(REMAPBANK3, REMAP_BANK_INVALID);

    //for (rbank = 0; rbank < NUM_BANKS_MAX; rbank++)
    //{
        //SETREG(_FCP_ROW_H(rbank), 0xFFFFFFFF);
        SETREG(_FCP_ROW_H(REAL_BANK(bank)), 0xFFFFFFFF);
    //}

    //for (bank = 0; bank < NUM_BANKS; bank++)
    //{
        SETREG(FCP_CMD, FC_GENERIC);
        SETREG(FCP_COL, 0xFF);                // flash reset
        SETREG(FCP_OPTION, 0);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);
    //}
//
    //for (bank = 0; bank < NUM_BANKS; bank++)
    //{
        SETREG(FCP_CMD, FC_WAIT);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);
    //}
//
    flash_finish();

    flash_clear_irq();

    #if FLASH_TYPE == MT29F64G08CFABA || FLASH_TYPE == MT29F128G08CJABA || FLASH_TYPE == JS29F32G08AAMDB

    #define OVER_DRIVE_2        0
    #define OVER_DRIVE_1        1
    #define DEFAULT_DRIVE        2
    #define UNDER_DRIVE            3

    #define L63B_OVER_DRIVE        OVER_DRIVE_2

    #if L63B_OVER_DRIVE != DEFAULT_DRIVE

    #if L63B_OVER_DRIVE == OVER_DRIVE_2            // Overdrive 2
    mem_set_sram(g_temp_mem, 0x00000000, sizeof(UINT32) * 2);
    #elif L63B_OVER_DRIVE == OVER_DRIVE_1        // Overdrive 1
    mem_set_sram(g_temp_mem, 0x00000101, sizeof(UINT32));
    mem_set_sram(g_temp_mem + sizeof(UINT32), 0x00000000, sizeof(UINT32));
    #elif L63B_OVER_DRIVE == DEFAULT_DRIVE        // Nominal(default)
    mem_set_sram(g_temp_mem, 0x00000202, sizeof(UINT32));
    mem_set_sram(g_temp_mem + sizeof(UINT32), 0x00000000, sizeof(UINT32));
    #else                                        // Underdrvie
    mem_set_sram(g_temp_mem, 0x00000303, sizeof(UINT32));
    mem_set_sram(g_temp_mem + sizeof(UINT32), 0x00000000, sizeof(UINT32));
    #endif

    //for (bank = 0; bank < NUM_BANKS; bank++)
    //{
        SETREG(FCP_CMD, FC_IN);                        // DIN : P1 : (0x00 : Overdrive 2, 0x01 : Overdrive 1, 0x02 : Nominal(default), 0x03 : Underdrvie)
        SETREG(FCP_DMA_ADDR, g_temp_mem);
        SETREG(FCP_DMA_CNT, 8);
        SETREG(FCP_OPTION, FO_B_W_DRDY);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);

        SETREG(FCP_CMD, FC_GENERIC);                // command : set features
        SETREG(FCP_COL, 0xEF);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);

        SETREG(FCP_CMD, FC_GENERIC_ADDR);            // addr : feature address (Progrmmable Output drive strength)
        SETREG(FCP_DMA_CNT, 2);
        SETREG(FCP_ROW_H(bank),  0x10 << 16);
        SETREG(FCP_ROW_L(bank),  0x10 << 16);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);

        SETREG(FCP_CMD, FC_IN);                        // DIN : P1 : (0x00 : Overdrive 2, 0x01 : Overdrive 1, 0x02 : Nominal(default), 0x03 : Underdrvie)
        SETREG(FCP_DMA_CNT, 8);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);

        SETREG(FCP_CMD, FC_WAIT);
        SETREG(FCP_DMA_CNT, 0);
        SETREG(FCP_COL, 0xFF);
        flash_issue_cmd(bank, RETURN_ON_ISSUE);
    //}
//
    flash_finish();
    #endif
    #endif

    //#if CLOCK_SPEED > 165000000
    //const UINT32 read_cycle = 2;
    //#else
    //const UINT32 read_cycle = 1;
    //#endif

    //const UINT32 time_param = FLASH_SETUP(FLASH_PARAM_SETUP) | FLASH_HOLD(CYCLE_PER_HOLD - 1) | FLASH_CLE_ALE_DLY(1) |
            //FLASH_READ_CYCLE_A(read_cycle) | FLASH_READ_CYCLE_B(read_cycle) |
            //FLASH_READ_CYCLE_C(read_cycle) | FLASH_READ_CYCLE_D(read_cycle) |
            //FLASH_OUT_ENA_DLY(1) | FLASH_WHR(15) | FLASH_ADL(31) | FLASH_RHW(31) | FLASH_ECC_ENABLE;

    //SETREG(FCONF_TIMECYCLE, time_param);
    uart_print_level_1("done\r\n");
}
