#include "openssd.h"
#include "device.h"
#include "param.h"
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <iostream>

using namespace std;

OpenSSD::OpenSSD(string name){
    name_ = name;
    //if((fd = open("/dev/sdb", O_RDWR | O_LARGEFILE )) < 0) {
    if((fd = open("/dev/sdb", O_RDWR | O_DIRECT | O_SYNC | O_LARGEFILE )) < 0) {
        perror("open error on file /dev/sdb");
        exit(-1);
    }
    /*
    if ((fd_sg = open("/dev/sg2", O_RDWR)) < 0) { // cannot open with O_DIRECT. Note that most SCSI commands require the O_RDWR flag to be set
        perror("open error on file /dev/sg2");
        exit(-1);
       }
    */
    fprintf(stderr, "OpenSSD opened correctly\n");
    cerr << "Total number of banks: " << NUM_BANKS * DATA_BLK_PER_BANK << endl;
    fprintf(stderr, "Overwrite bit position is %d\n", overwriteBitPosition);
    fflush(stderr);
    return;
}

OpenSSD::~OpenSSD(){
    close(fd);
    close(fd_sg);
}


int64_t OpenSSD::lbaToOwLba(int64_t lba)
{
    return (lba | (int64_t)(1 << overwriteBitPosition));
}

int OpenSSD::overwriteLba(int64_t lba, unsigned char * buffer, int nSectors){
    return(writeLba(lbaToOwLba(lba), buffer, nSectors));
}

void OpenSSD::blkDiscard(uint64_t lba, uint64_t nSectors){
    uint64_t range[2] = { lba*SECT_SIZE, nSectors*SECT_SIZE };
    //cout << "Discarding range " << range[0] << ":" << range[1] << endl;
    ioctl(fd, BLKDISCARD, range);
}

