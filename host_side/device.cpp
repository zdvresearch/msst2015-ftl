#include <errno.h>
#include <stdio.h>
#include "device.h"
#include "param.h"
#include <iostream>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#define RealLba(lba)    ((lba) & ~((int64_t)(1 << overwriteBitPosition)))

using namespace std;

Device::Device(){}

Device::Device(string deviceName, const char * fileName, const char * mode){
    cout << "Device " << deviceName << endl;
    name_ = deviceName;
    fprintf(stdout, "Creating file %s\n", fileName);
    fflush(stdout);
    if (strcmp(mode, "new") == 0){
        fd = open(fileName, O_CREAT | O_RDWR | O_LARGEFILE );
        if (fd == -1)
            perror("device.cc: error while creating file");
    }
    else{
        fprintf(stdout, "Opening existing file %s\n", fileName);
        fflush(stdout);
        fd = open(fileName, O_RDWR | O_LARGEFILE );
        if (fd == -1)
            perror("device.cc: error while opening file");
    }
}

Device::~Device(){
    if(close(fd) == -1){
        perror("Device destructor: error closing file");
    }
}

int Device::readLba(int64_t lba, unsigned char * buffer, int nSectors){
    off64_t pos = (off64_t)(RealLba(lba) * SECT_SIZE);
    if(lseek64(fd, pos, SEEK_SET) < 0){
        cerr << name_ << ": lseek error" << endl;
        perror("readLba: lseek error");
        exit(-1);
    }
    int byteCnt = read(fd, buffer, nSectors*SECT_SIZE);
    if(byteCnt != nSectors*SECT_SIZE){
        cerr << name_ << ": read error" << endl;
        //perror("readLba: read error");
        fprintf(stderr, "Device::readLba error: should read %dB, but can read only %dB\n", nSectors*SECT_SIZE, byteCnt);
        fprintf(stderr, "lba = %ld, real lba = %ld, nSectors = %d\n", lba, RealLba(lba), nSectors);
        exit(-1);
    }
    return byteCnt;
}

int Device::writeLba(int64_t lba, unsigned char * buffer, int nSectors){
    off64_t pos = (off64_t)(lba * SECT_SIZE);
    //off_t pos = (off_t)(lba * SECT_SIZE);

    // Old version used lseek
    //if(lseek64(fd, pos, SEEK_SET) < 0){
        //perror("writeLba: lseek error");
        //cout << "lseek to pos " << pos << endl;
        //exit(-1);
    //}
    //int byteCnt = write(fd, buffer, nSectors*SECT_SIZE);
    int byteCnt = pwrite(fd, buffer, nSectors*SECT_SIZE, pos);
    if(byteCnt != nSectors*SECT_SIZE){
        cerr << name_ << ": write error" << endl;
        fprintf(stderr, "Device::writeLba error: should write %dB to lba %ld, but can write only %dB\n", nSectors*SECT_SIZE, lba, byteCnt);
        perror("Error: ");
        exit(-1);
    }
    return byteCnt;
}

int Device::readLbaDuringOw(int64_t lba, unsigned char * buffer, int nSectors){
    off64_t pos = (off64_t)(RealLba(lba) * SECT_SIZE);
    if(lseek64(fd, pos, SEEK_SET) < 0){
        cerr << name_ << ": lseek error in readLbaDuringOw" << endl;
        perror("readLbaDuringOw: lseek error");
        exit(-1);
    }
    int byteCnt = read(fd, buffer, nSectors*SECT_SIZE);
    if(byteCnt != nSectors*SECT_SIZE){
        cerr << name_ << ": read error in readLbaDuringOw" << endl;
        fprintf(stderr, "Device::readLbaDuringOw error: should read %dB, but can read only %dB\n", nSectors*SECT_SIZE, byteCnt);
        fprintf(stderr, "lba = %ld, real lba = %ld, nSectors = %d\n", lba, RealLba(lba), nSectors);
        exit(-1);
    }
    return byteCnt;
}


int Device::overwriteLba(int64_t lba, unsigned char * buffer, int nSectors){
    unsigned char* bufRead = (unsigned char*)malloc(nSectors*SECT_SIZE*sizeof(unsigned char));
    readLbaDuringOw(lba, bufRead, nSectors);
    for(int i=0; i<nSectors*SECT_SIZE; i++) // bitwise and to simulate the overwrite operation
        buffer[i]=buffer[i]&bufRead[i];
    int byteCnt = writeLba(lba, buffer, nSectors);
    //readLba(lba, bufRead, nSectors);
    free(bufRead);
    return byteCnt;
}
