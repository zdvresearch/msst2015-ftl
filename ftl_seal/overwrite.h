#ifndef OVERWRITE_H
#define OVERWRITE_H

#include "jasmine.h"

#if Overwrite

void overwriteToLogBlk (const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects);
void flushOverwriteLog();

#endif

#endif

