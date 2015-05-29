#ifndef OPENSSD_HH
#define OPENSSD_HH
#include "device.hh"
#include <sys/types.h>
#include <unistd.h>
#include <string>

using namespace std;

class OpenSSD: public Device{
	public:
		OpenSSD(string name);
		virtual ~OpenSSD();
		virtual int overwriteLba(int64_t lba, unsigned char * buffer, int nSectors);
		virtual void trim(int64_t lba, int nSectors);
        void blkDiscard(uint64_t lba, uint64_t nSectors);
	protected:
		int fd_sg;
		int64_t lbaToOwLba(int64_t lba);
};
#endif
