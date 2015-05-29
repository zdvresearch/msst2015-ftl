#ifndef CLEAN_LIST_H
#define CLEAN_LIST_H
#include "jasmine.h"
#include "ftl_metadata.h"

void cleanListPush(listData * data, UINT32 bank, UINT32 logLbn);
UINT32 cleanListPop(listData * data, UINT32 bank);
UINT32 cleanListSize(listData * data, UINT32 bank);
//void testCleanList();
void cleanListInit(listData * data, UINT32 startAddr, UINT32 numNodesPerBank);

//void cleanListDump(listData * data, UINT32 bank);

#endif
