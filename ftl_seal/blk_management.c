#include "blk_management.h"
#include "ftl_metadata.h"

UINT32 get_free_vbn (UINT32 const bank)
{
    UINT32 free_blk_offset = free_list_tail[bank];
    free_list_tail[bank] = (free_blk_offset + 1) % FREE_BLK_PER_BANK;
    return read_dram_16 (FREE_BMT_ADDR + ((bank * FREE_BLK_PER_BANK) + free_blk_offset) * sizeof (UINT16));
}

void ret_free_vbn (UINT32 const bank, UINT32 const vblock)
{
    UINT32 free_blk_offset = free_list_head[bank];
    write_dram_16 (FREE_BMT_ADDR + ((bank * FREE_BLK_PER_BANK) + free_blk_offset) * sizeof (UINT16), vblock);
    free_list_head[bank] = (free_blk_offset + 1) % FREE_BLK_PER_BANK;
}
