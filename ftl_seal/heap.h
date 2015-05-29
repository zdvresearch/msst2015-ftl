#ifndef HEAP_H
#define HEAP_H

#include "jasmine.h"

void insertBlkInHeap(heapData * data, UINT32 bank, UINT16 realLogLbn);
//void incrementValidChunks(heapData * data, UINT32 bank, UINT16 realLogLbn);
void decrementValidChunks(heapData * data, UINT32 bank, UINT16 realLogLbn);
void decrementValidChunksByN(heapData * data, UINT32 bank, UINT16 realLogLbn, UINT16 n);
void setValidChunks(heapData * data, UINT32 bank, UINT16 realLogLbn, UINT16 initialValue);
void resetValidChunksAndRemove(heapData * data, UINT32 bank, UINT16 realLogLbn, UINT16 initialValue);
UINT32 getVictim(heapData * data, UINT32 bank);
UINT32 getVictimValidPagesNumber(heapData * data, UINT32 bank);
void ValidChunksHeapInit(heapData * data, UINT32 blksPerBank, UINT32 firstLbn, UINT16 initialValue);
//void heapTest(heapData * data);

//void dumpHeap(heapData * data, UINT32 bank);

#endif
