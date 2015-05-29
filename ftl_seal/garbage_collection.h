#ifndef GARBAGE_COLLECTOR_H
#define GARBAGE_COLLECTOR_H
#include "jasmine.h"

void progressiveMerge(const UINT32 bank, const UINT32 maxFlashOps);
#if MeasureGc
int garbageCollectLog(const UINT32 bank);
#else
void garbageCollectLog(const UINT32 bank);
#endif
#endif
