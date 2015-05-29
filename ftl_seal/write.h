#ifndef WRITE_H
#define WRITE_H

UINT8 writeChunkOnLogBlockDuringGC(const UINT32 dataLpn, const UINT32 dataChunkOffset, const UINT32 chunkOffsetInBuf, const UINT32 bufAddr);
void writeToLogBlk (const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects);
//void writeToSWBlk(const UINT32 lba, const UINT32 nSects);
UINT32 chooseNewBank();

#endif
