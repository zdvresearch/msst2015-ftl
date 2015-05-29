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
//
// FASTer FTL header file
//
// Author; Sang-Phil Lim (SKKU VLDB Lab.)
//
// Reference;
//   - Sang-Phil Lim, Sang-Won Lee and Bongki Moon, "FASTer FTL for Enterprise-Class Flash Memory SSDs"
//     IEEE SNAPI 2010: 6th IEEE International Workshop on Storage Network Architecture and Parallel I/Os, May 2010
//

#ifndef FTL_H
#define FTL_H

#include "jasmine.h" // UINT32
#include "dram_layout.h" // should expose this header to the rest of the system which includes only ftl.h as comprising all ftl-related stuff
#include "ftl_metadata.h"

/******** FTL metadata ********/

///////////////////////////////
// FTL public functions
///////////////////////////////

void ftl_open (void);
void ftl_read (UINT32 const lba, UINT32 const num_sectors);
void ftl_write (UINT32 const lba, UINT32 const num_sectors);
void ftl_trim (UINT32 const lba, UINT32 const num_sectors);
//void ftl_test_write (UINT32 const lba, UINT32 const num_sectors);
void ftl_flush (void);
void ftl_isr (void);

BOOL32 is_bad_block (UINT32 const bank, UINT32 const vblk_offset); // used in blk_management.c. Probably this should change

#endif //FTL_H
