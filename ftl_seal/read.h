#ifndef READ_H
#define READ_H

void readFromLogBlk (UINT32 const dataLpn, UINT32 const sectOffset, UINT32 const nSects);
void rebuildPageToFtlBuf(const UINT32 dataLpn, const UINT32 sectOffset, const UINT32 nSects, const UINT8 mode);

#endif
