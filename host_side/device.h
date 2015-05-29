#ifndef DEVICE_HH
#define DEVICE_HH

#define _GNU_SOURCE
//#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <time.h>
#include <string>

using namespace std;

class Device{
    public:
        Device();
        Device(string deviceName, const char * fileName, const char * mode);
        virtual ~Device();
        virtual int writeLba(int64_t lba, unsigned char * buffer, int nSectors);
        virtual int overwriteLba(int64_t lba, unsigned char * buffer, int nSectors);
        virtual int readLba(int64_t lba, unsigned char * buffer, int nSectors);
        int readLbaDuringOw(int64_t lba, unsigned char * buffer, int nSectors);

    protected:
        int fd;
        string name_;
};
#endif
