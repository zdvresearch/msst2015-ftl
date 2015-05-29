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

UINT8 g_temp_mem[BYTES_PER_SECTOR];    // scratch pad

#if OPTION_UART_DEBUG == 1
    #if OPTION_UART_DEBUG_DRAM == 0
        #define uart_print(X)
        #define uart_print_int(X)
    #endif
#endif

void _mem_copy(void* const dst, const void* const src, UINT32 const num_bytes) {
    int count = 0;
    while (GETREG(MU_RESULT) == 0xFFFFFFFF) {
        count ++;
        if (count == 10000) {
            count = 0;
            uart_print_level_1("Warning: waiting too long before mem_copy");
        }
    }

    uart_print("MC "); uart_print_int(dst);
    uart_print(" "); uart_print_int(src);
    uart_print(" "); uart_print_int(num_bytes); uart_print("\r\n");
    UINT32 d = (UINT32) dst;
    UINT32 s = (UINT32) src;
    //BOOL32 was_disabled;

    ASSERT(d % sizeof(UINT32) == 0);
    ASSERT(s % sizeof(UINT32) == 0);
    ASSERT(num_bytes % sizeof(UINT32) == 0);
    ASSERT(num_bytes <= MU_MAX_BYTES);
    if(d >= (DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in mem_copy dst addr out of DRAM boundaries\r\n");
        while(1);
    }
    if(s >= (DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in mem_copy src addr out of DRAM boundaries\r\n");
        while(1);
    }

    // if the memory data copy DRAM-to-DRAM,
    // then `num_bytes' should be aligned DRAM_ECC_UNIT size
    if (d > DRAM_BASE && s > DRAM_BASE && ((num_bytes % DRAM_ECC_UNIT) != 0)) {
        uart_print_level_1("ERROR during mem_copy: trying to copy from DRAM to DRAM a number of bytes not aligned to DRAM_ECC_UNIT!\r\n");
        while(1);
    }
    //was_disabled = disable_irq();

#if DEBUG_MEM_UTIL
    ASSERT(busy == 0);
    busy = 1;
#endif

    SETREG(MU_SRC_ADDR, s);
    SETREG(MU_DST_ADDR, d);
    SETREG(MU_SIZE, num_bytes);
    SETREG(MU_CMD, MU_CMD_COPY);

    count = 0;
    while (GETREG(MU_RESULT) == 0xFFFFFFFF) {
        count ++;
        if (count == 10000) {
            count = 0;
            uart_print_level_1("Warning: waiting too long after mem_copy");
        }
    }
#if DEBUG_MEM_UTIL
    busy = 0;
#endif
    //if (!was_disabled) {
        //enable_irq();
    //}
}


UINT32 _mem_bmp_find_sram(const void* const bitmap, UINT32 const num_bytes, UINT32 const val)
{
    uart_print_level_1("_mem_bmp_find_sram no safe prints in infinite loops!\r\n");
    UINT32 retval;
    BOOL32 was_disabled;

    ASSERT(num_bytes <= MU_MAX_BYTES && num_bytes != 0);
    ASSERT(num_bytes % sizeof(UINT32) == 0);
    ASSERT(val == 0 || val == 1);
    ASSERT((UINT32) bitmap < DRAM_BASE);

    // can be called in IRQ handling
    was_disabled = disable_irq();

    #if DEBUG_MEM_UTIL
    ASSERT(busy == 0);
    busy = 1;
    #endif

    SETREG(MU_SRC_ADDR, bitmap);
    SETREG(MU_VALUE, val);
    SETREG(MU_SIZE, num_bytes);
    SETREG(MU_CMD, MU_CMD_FIND_SRAM);

    int count=0;
    while (1) {
        count++;
        if (count > 100000) {
            uart_print_level_1("Warning1 in mem_bmp_find_sram\r\n");
            count=0;
        }
        retval = GETREG(MU_RESULT);
        if (retval != 0xFFFFFFFF)
            break;
    }

    #if DEBUG_MEM_UTIL
    busy = 0;
    #endif

    if (!was_disabled)
        enable_irq();

    return retval;
}

UINT32 _mem_bmp_find_dram(const void* const bitmap, UINT32 const num_bytes, UINT32 const val)
{
    uart_print_level_1("_mem_bmp_find_dram no safe prints in infinite loops!\r\n");
    UINT32 retval;
    BOOL32 was_disabled;

    ASSERT(num_bytes <= MU_MAX_BYTES && num_bytes != 0);
    ASSERT(num_bytes % sizeof(UINT32) == 0);
    ASSERT(val == 0 || val == 1);
    ASSERT((UINT32) bitmap >= DRAM_BASE);

    was_disabled = disable_irq();

    #if DEBUG_MEM_UTIL
    ASSERT(busy == 0);
    busy = 1;
    #endif

    SETREG(MU_SRC_ADDR, bitmap);
    SETREG(MU_VALUE, val);
    SETREG(MU_SIZE, num_bytes);
    SETREG(MU_CMD, MU_CMD_FIND_DRAM);

    int count=0;
    while (1) {
        count++;
        if (count > 100000) {
            uart_print_level_1("Warning in mem_bmp_find_dram\r\n");
            count=0;
        }
        retval = GETREG(MU_RESULT);
        if (retval != 0xFFFFFFFF)
            break;
    }

    #if DEBUG_MEM_UTIL
    busy = 0;
    #endif

    if (!was_disabled)
        enable_irq();

    return retval;
}

void _mem_set_sram(UINT32 addr, UINT32 const val, UINT32 num_bytes)
{
    UINT32 size;

    ASSERT((UINT32)addr % sizeof(UINT32) == 0);
    ASSERT(num_bytes % sizeof(UINT32) == 0);
    ASSERT((UINT32) addr + num_bytes <= SRAM_SIZE);

    #if DEBUG_MEM_UTIL
    ASSERT(busy == 0);
    busy = 1;
    #endif

    SETREG(MU_VALUE, val);

    int count1=0;
    do
    {
        count1 ++;
        if (count1 == 100000) {
            count1 = 0;
            uart_print_level_1("Warning in _mem_set_sram: waiting too long on count1\r\n");
        }
        size = MIN(num_bytes, MU_MAX_BYTES);

        SETREG(MU_DST_ADDR, addr);
        SETREG(MU_SIZE, size);
        SETREG(MU_CMD, MU_CMD_SET_REPT_SRAM);

        addr += size;
        num_bytes -= size;

        int count2=0;
        while (GETREG(MU_RESULT) == 0xFFFFFFFF) {
            count2 ++;
            if (count2 == 100000) {
                count2 = 0;
                uart_print_level_1("Warning in _mem_set_sram: waiting too long on count2\r\n");
            }
        }
    }
    while (num_bytes != 0);

    #if DEBUG_MEM_UTIL
    busy = 0;
    #endif
}

void _mem_set_dram(UINT32 addr, UINT32 const val, UINT32 num_bytes)
{
    UINT32 size;

    if((UINT32)addr % SDRAM_ECC_UNIT != 0)
    {
        uart_print_level_1("_mem_set_dram: addr not aligned to SDRAM_ECC_UNIT (128). addr value: "); uart_print_level_1_int(addr); uart_print_level_1("\r\n");
        while(1);
    }
    if(num_bytes % SDRAM_ECC_UNIT != 0)
    {
        uart_print_level_1("_mem_set_dram: num_bytes not multiple of SDRAM_ECC_UNIT (128). num_bytes value: "); uart_print_level_1_int(num_bytes); uart_print_level_1("\r\n");
        while(1);
    }
    if((UINT32) addr < DRAM_BASE)
    {
        uart_print_level_1("_mem_set_dram: addr smaller than DRAM start address. addr value: "); uart_print_level_1_int(addr); uart_print_level_1("\r\n");
        while(1);
    }

    int count0=0;
    while (GETREG(MU_RESULT) == 0xFFFFFFFF) {
        count0++;
        if (count0 == 100000) {
            count0=0;
            uart_print_level_1("Warning in _mem_set_dram: waiting too long on count0\r\n");
        }
    }

    #if DEBUG_MEM_UTIL
    ASSERT(busy == 0);
    busy = 1;
    #endif

    SETREG(MU_VALUE, val);

    int count1=0;
    do {
        count1++;
        if (count1 == 100000) {
            count1=0;
            uart_print_level_1("Warning in _mem_set_dram: waiting too long on count1\r\n");
        }
        size = MIN(num_bytes, MU_MAX_BYTES);

        SETREG(MU_DST_ADDR, addr);
        SETREG(MU_SIZE, size);
        SETREG(MU_CMD, MU_CMD_SET_REPT_DRAM);

        addr += size;
        num_bytes -= size;

        int count2=0;
        while (GETREG(MU_RESULT) == 0xFFFFFFFF) {
            count2++;
            if (count2 == 100000) {
                count2=0;
                uart_print_level_1("Warning in _mem_set_dram: waiting too long on count2\r\n");
            }
        }
    }
    while (num_bytes != 0);

    #if DEBUG_MEM_UTIL
    busy = 0;
    #endif
}

UINT32 _mem_search_min_max(const void* const addr, UINT32 const num_bytes_per_item, UINT32 const num_items, UINT32 const cmd)
{
    uart_print_level_1("_mem_search_min_max no safe prints in infinite loops!\r\n");
    UINT32 retval;

    ASSERT((UINT32)addr % sizeof(UINT32) == 0);
    ASSERT(num_items != 0 && num_bytes_per_item * num_items <= MU_MAX_BYTES);

    #if DEBUG_MEM_UTIL
    ASSERT(busy == 0);
    busy = 1;
    #endif

    if (num_bytes_per_item == sizeof(UINT8))
    {
        SETREG(MU_UNITSTEP, MU_UNIT_8 | 1);
    }
    else if (num_bytes_per_item == sizeof(UINT16))
    {
        SETREG(MU_UNITSTEP, MU_UNIT_16 | 2);
    }
    else
    {
        SETREG(MU_UNITSTEP, MU_UNIT_32 | 4);
    }
    SETREG(MU_SRC_ADDR, addr);
    SETREG(MU_SIZE, num_items);
    SETREG(MU_CMD, cmd);

    int count=0;
    while (1) {
        count ++;
        if (count > 100000) {
            uart_print_level_1("Warning in mem_search_min_max\r\n");
            count=0;
        }
        retval = GETREG(MU_RESULT);

        if (retval != 0xFFFFFFFF)
            break;
    }

    #if DEBUG_MEM_UTIL
    busy = 0;
    #endif

    return retval;
}

UINT32 _mem_search_equ(const void* const addr, UINT32 const num_bytes_per_item, UINT32 const num_items, UINT32 const cmd, UINT32 const val)
{
    //uart_print_level_1("_mem_search_equ no safe prints in infinite loops!\r\n");
    UINT32 retval;

    ASSERT((UINT32)addr % sizeof(UINT32) == 0);
    ASSERT(num_bytes_per_item * num_items <= MU_MAX_BYTES);

    if (num_items == 0)
    {
        return 1;
    }

    #if DEBUG_MEM_UTIL
    ASSERT(busy == 0);
    busy = 1;
    #endif

    if (num_bytes_per_item == sizeof(UINT8))
    {
        SETREG(MU_UNITSTEP, MU_UNIT_8 | 1);
    }
    else if (num_bytes_per_item == sizeof(UINT16))
    {
        SETREG(MU_UNITSTEP, MU_UNIT_16 | 2);
    }
    else
    {
        SETREG(MU_UNITSTEP, MU_UNIT_32 | 4);
    }

    SETREG(MU_SRC_ADDR, addr);
    SETREG(MU_VALUE, val);
    SETREG(MU_SIZE, num_items);
    SETREG(MU_CMD, cmd);

    int count1 = 0;
    while (1)
    {
        count1++;
        if (count1 == 100000) {
            count1=0;
            uart_print_level_1("Warning in _mem_search_equ: waiting too long on count1\r\n");
        }
        retval = GETREG(MU_RESULT);

        if (retval != 0xFFFFFFFF)
            break;
    }

    #if DEBUG_MEM_UTIL
    busy = 0;
    #endif

    return retval;
}

void _write_dram_32(UINT32 const addr, UINT32 const val)
{
    uart_print("WD32 ");
    uart_print_int(addr);
    uart_print(" ");
    uart_print_int(val);
    uart_print("\r\n");
    if(addr < DRAM_BASE){
        uart_print_level_1("ERROR: in write_dram_32 addr smaller than DRAM start addr\r\nAddress is ");
        uart_print_level_1_int(addr);
        uart_print_level_1(" while DRAM_BASE is ");
        uart_print_level_1_int(DRAM_BASE);
        while(1);
    }
    if(addr >=(DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in write_dram_32 addr bigger than DRAM max addr\r\nAddress is ");
        uart_print_level_1_int(addr);
        uart_print_level_1(" while DRAM_BASE + DRAM_SIZE is ");
        uart_print_level_1_int(DRAM_BASE + DRAM_SIZE);
        while(1);
    }
    if(addr % sizeof(UINT32) != 0){
        uart_print_level_1("ERROR: in write_dram_32 addr not aligned on a 4B boundary\r\n");
        while(1);
    }

    mem_copy(addr, &val, sizeof(UINT32));
}

void _write_dram_16(UINT32 const addr, UINT16 const val)
{
    uart_print("WD16 ");
    uart_print_int(addr);
    uart_print(" ");
    uart_print_int(val);
    uart_print("\r\n");
    if(addr < DRAM_BASE){
        uart_print_level_1("ERROR: in write_dram_16 addr smaller than DRAM start addr\r\n");
        while(1);
    }
    if(addr >=(DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in write_dram_16 addr bigger than DRAM max addr\r\n");
        while(1);
    }

    UINT32 offset = addr % 4;
    UINT32 aligned_addr = addr & 0xFFFFFFFC;
    UINT32 real_addr = DRAM_BASE + (aligned_addr - DRAM_BASE)/128*132 + (aligned_addr - DRAM_BASE)%128;
    UINT32 old_val = *(volatile UINT32*)real_addr;
    UINT32 new_val = (old_val & ~(0xFFFF << (offset * 8))) | (val << (offset * 8));
    mem_copy(aligned_addr, &new_val, sizeof(UINT32));
}

void _write_dram_8(UINT32 const addr, UINT8 const val)
{
    uart_print("WD8 ");
    uart_print_int(addr);
    uart_print(" ");
    uart_print_int(val);
    uart_print("\r\n");
    if(addr < DRAM_BASE){
        uart_print_level_1("ERROR: in write_dram_8 addr smaller than DRAM start addr\r\n");
        while(1);
    }
    if(addr >=(DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in write_dram_8 addr bigger than DRAM max addr\r\n");
        while(1);
    }

    UINT32 offset = addr % 4;
    UINT32 aligned_addr = addr & 0xFFFFFFFC;
    UINT32 real_addr = DRAM_BASE + (aligned_addr - DRAM_BASE)/128*132 + (aligned_addr - DRAM_BASE)%128;
    UINT32 old_val = *(volatile UINT32*)real_addr;
    UINT32 new_val = (old_val & ~(0xFF << (offset * 8))) | (val << (offset * 8));
    mem_copy(aligned_addr, &new_val, sizeof(UINT32));
}

void _set_bit_dram(UINT32 const base_addr, UINT32 const bit_offset)
{
    if(base_addr < DRAM_BASE){
        uart_print_level_1("ERROR: in set_bit_dram addr smaller than DRAM start addr\r\n");
        while(1);
    }
    if(base_addr >=(DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in set_bit_dram addr bigger than DRAM max addr\r\n");
        while(1);
    }

    UINT32 val;
    UINT32 addr = base_addr + bit_offset / 8;
    UINT32 aligned_addr = addr & 0xFFFFFFFC;
    UINT32 new_bit_offset = (base_addr*8 + bit_offset) % 32;
    UINT32 real_addr = DRAM_BASE + (aligned_addr - DRAM_BASE)/128*132 + (aligned_addr - DRAM_BASE)%128;

    val = *(volatile UINT32*)real_addr;
    val |= (1 << new_bit_offset);
    mem_copy(aligned_addr, &val, sizeof(UINT32));
}

void _clr_bit_dram(UINT32 const base_addr, UINT32 const bit_offset)
{
    if(base_addr < DRAM_BASE){
        uart_print_level_1("ERROR: in clr_bit_dram addr smaller than DRAM start addr\r\n");
        while(1);
    }
    if(base_addr >=(DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in clr_bit_dram addr bigger than DRAM max addr\r\n");
        while(1);
    }

    UINT32 val;
    UINT32 addr = base_addr + bit_offset / 8;
    UINT32 aligned_addr = addr & 0xFFFFFFFC;
    UINT32 new_bit_offset = (base_addr*8 + bit_offset) % 32;
    UINT32 real_addr = DRAM_BASE + (aligned_addr - DRAM_BASE)/128*132 + (aligned_addr - DRAM_BASE)%128;

    val = *(volatile UINT32*)real_addr;
    val &= ~(1 << new_bit_offset);
    mem_copy(aligned_addr, &val, sizeof(UINT32));
}

BOOL32 _tst_bit_dram(UINT32 const base_addr, UINT32 const bit_offset)
{
    if(base_addr < DRAM_BASE){
        uart_print_level_1("ERROR: in tst_bit_dram addr smaller than DRAM start addr\r\n");
        while(1);
    }
    if(base_addr >=(DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in tst_bit_dram addr bigger than DRAM max addr\r\n");
        while(1);
    }

    UINT32 val;
    UINT32 addr = base_addr + bit_offset / 8;
    UINT32 aligned_addr = addr & 0xFFFFFFFC;
    UINT32 new_bit_offset = (base_addr*8 + bit_offset) % 32;
    UINT32 real_addr = DRAM_BASE + (aligned_addr - DRAM_BASE)/128*132 + (aligned_addr - DRAM_BASE)%128;
    val = *(volatile UINT32*)real_addr;
    //uart_printf("Read from DRAM %d, bit offset is %d, 32bit bit offset is %d, returning %d", val, bit_offset, new_bit_offset, (val & (1 << new_bit_offset)));

    return val & (1 << new_bit_offset);
}

UINT8 _read_dram_8(UINT32 const addr)
{
    uart_print("RD8 ");
    uart_print_int(addr);
    uart_print(" ");
    if(addr < DRAM_BASE){
        uart_print_level_1("ERROR: in read_dram_8 addr smaller than DRAM start addr\r\n");
        while(1);
    }
    if(addr >=(DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in read_dram_8 addr bigger than DRAM max addr\r\n");
        while(1);
    }

    UINT32 aligned_addr = addr & 0xFFFFFFFC;
    UINT32 byte_offset = addr % 4;
    UINT32 val;
    UINT32 real_addr = DRAM_BASE + (aligned_addr - DRAM_BASE)/128*132 + (aligned_addr - DRAM_BASE)%128;
    val = *(volatile UINT32*)real_addr;

    uart_print_int(val >> (byte_offset * 8)); uart_print("\r\n");

    return (UINT8) (val >> (byte_offset * 8));
}

UINT16 _read_dram_16(UINT32 const addr)
{
    uart_print("RD16 ");
    uart_print_int(addr);
    uart_print(" ");
    if(addr < DRAM_BASE){
        uart_print_level_1("ERROR: in read_dram_16 addr smaller than DRAM start addr\r\n");
        while(1);
    }
    if(addr >=(DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in read_dram_16 addr bigger than DRAM max addr\r\n");
        while(1);
    }

    UINT32 aligned_addr = addr & 0xFFFFFFFC;
    UINT32 byte_offset = addr % 4;
    UINT32 val;
    UINT32 real_addr = DRAM_BASE + (aligned_addr - DRAM_BASE)/128*132 + (aligned_addr - DRAM_BASE)%128;
    val = *(volatile UINT32*)real_addr;

    uart_print_int(val >> (byte_offset * 8)); uart_print("\r\n");

    return (UINT16) (val >> (byte_offset * 8));
}

UINT32 _read_dram_32(UINT32 const addr)
{
    uart_print("RD32 ");
    uart_print_int(addr);
    uart_print(" ");
    if(addr < DRAM_BASE){
        uart_print_level_1("ERROR: in read_dram_32 addr smaller than DRAM start addr\r\nAddress is ");
        uart_print_level_1_int(addr);
        uart_print_level_1(" while DRAM_BASE is ");
        uart_print_level_1_int(DRAM_BASE);
        while(1);
    }
    if(addr >=(DRAM_BASE + DRAM_SIZE)){
        uart_print_level_1("ERROR: in read_dram_32 addr bigger than DRAM max addr\r\nAddress is ");
        uart_print_level_1_int(addr);
        uart_print_level_1(" while DRAM_BASE + DRAM_SIZE is ");
        uart_print_level_1_int(DRAM_BASE + DRAM_SIZE);
        while(1);
    }
    if(addr % sizeof(UINT32) != 0){
        uart_print_level_1("ERROR: in read_dram_32 addr not aligned on a 4B boundary\r\n");
        while(1);
    }

    UINT32 val;
    UINT32 real_addr = DRAM_BASE + (addr - DRAM_BASE)/128*132 + (addr - DRAM_BASE)%128;
    val = *(volatile UINT32*)real_addr;

    uart_print_int(val); uart_print("\r\n");

    return val;
}

UINT32 _mem_cmp_sram(const void* const addr1, const void* const addr2, const UINT32 num_bytes)
{
    uart_print_level_1("_mem_cmp_sram no safe prints in infinite loops!\r\n");
    UINT8 *ptr1, *ptr2;
    UINT8 val1, val2;
    UINT32 i;

    ASSERT((UINT32) addr1 < DRAM_BASE);
    ASSERT((UINT32) addr2 < DRAM_BASE);
    ASSERT(num_bytes != 0);

    ptr1 = (UINT8 *)addr1;
    ptr2 = (UINT8 *)addr2;

    for (i = 0; i < num_bytes; i++)
    {
        val1 = *ptr1++;
        val2 = *ptr2++;

        if (val1 != val2)
        {
            return (val1 > val2 ) ? 1 : -1;
        }
    }
    return 0;
}

UINT32 _mem_cmp_dram(const void* const addr1, const void* const addr2, const UINT32 num_bytes)
{
    uart_print_level_1("_mem_cmp_dram no safe prints in infinite loops!\r\n");
    UINT8 *ptr1, *ptr2;
    UINT8 val1, val2;
    UINT32 i;

    ASSERT((UINT32)addr1 >= DRAM_BASE && (UINT32)addr1 < (DRAM_BASE + DRAM_SIZE));
    ASSERT((UINT32)addr2 >= DRAM_BASE && (UINT32)addr2 < (DRAM_BASE + DRAM_SIZE));

    ASSERT(num_bytes != 0);

    ptr1 = (UINT8 *)addr1;
    ptr2 = (UINT8 *)addr2;

    for (i = 0; i < num_bytes; i++)
    {
        val1 = read_dram_8(ptr1);
        val2 = read_dram_8(ptr2);

        if (val1 != val2)
        {
            return (val1 > val2 ) ? 1 : -1;
        }
        ptr1++;
        ptr2++;
    }
    return 0;
}


