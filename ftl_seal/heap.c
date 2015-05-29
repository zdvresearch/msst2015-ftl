#include "heap.h"
#include "ftl_metadata.h" // SRAM data structure
#include "ftl_parameters.h" // heapEl definition
#include "dram_layout.h" // dram addresses

#if OPTION_UART_DEBUG == 1
    #if OPTION_UART_DEBUG_HEAP == 0
        #define uart_print(X)
        #define uart_print_int(X)
    #endif
#endif

typedef union {
    heapEl val;
    UINT32 stub;
} stubUnion;


// Private functions
static void swap(heapData * data, UINT32 bank, heapEl el1, UINT16 pos1, heapEl el2, UINT16 pos2);
static void setHeapEl(heapData * data, UINT32 bank, UINT16 pos, heapEl el);
static void getHeapEl(heapData * data, UINT32 bank, UINT16 pos, heapEl * res);
static void bubbleDown(heapData * data, UINT32 bank, UINT16 pos);
static void bubbleUp(heapData * data, UINT32 bank, UINT16 pos);

static void swap(heapData * data, UINT32 bank, heapEl el1, UINT16 pos1, heapEl el2, UINT16 pos2) {
    uart_print("swap pos "); uart_print_int(pos1);
    uart_print(" and pos "); uart_print_int(pos2); uart_print("\r\n");
    setHeapEl(data, bank, pos1, el2);
    setHeapEl(data, bank, pos2, el1);
    if((HeapPositions(data->positionsPtr, bank, el1.lbn)) >=(DRAM_BASE + DRAM_SIZE))
    {
        uart_print_level_1("ERROR in heap::swap 1: writing to "); uart_print_level_1_int(HeapPositions(data->positionsPtr, bank, el1.lbn)); uart_print_level_1("\r\n");
    }
    write_dram_32(HeapPositions(data->positionsPtr, bank, el1.lbn), pos2);
    if((HeapPositions(data->positionsPtr, bank, el2.lbn)) >=(DRAM_BASE + DRAM_SIZE))
    {
        uart_print_level_1("ERROR in heap::swap 2: writing to "); uart_print_level_1_int(HeapPositions(data->positionsPtr, bank, el2.lbn)); uart_print_level_1("\r\n");
    }
    write_dram_32(HeapPositions(data->positionsPtr, bank, el2.lbn), pos1);
    //data->validChunksHeapPositions[bank][el1.lbn]=pos2; // swap positions in SRAM data structure
    //data->validChunksHeapPositions[bank][el2.lbn]=pos1;
}

// Note that positions in heap start from 1 to handle finding children at position 2n and 2n+1.
// The setter and getter funcions will translate these positions to positions starting from 0 to retrieve values in DRAM
static void setHeapEl(heapData * data, UINT32 bank, UINT16 pos, heapEl el) {
    if(pos==0){
        uart_print_level_1("\r\n\r\nERROR: in setHeapEl, position is 0\r\n");
        while(1);
    }
    if(pos>data->logBlksPerBank){
        uart_print_level_1("\r\n\r\nERROR: in setHeapEl, position in heap bigger than number of log blocks\r\n");
        while(1);
    }
    uart_print("Setting heap(bank="); uart_print_int(bank);
    uart_print(", pos="); uart_print_int(pos); uart_print(")\r\n");
    stubUnion temp;
    temp.val = el;
    if((ValidChunksAddr(data->dramStartAddr, bank, pos-1)) >=(DRAM_BASE + DRAM_SIZE))
    {
        uart_print_level_1("ERROR in heap::setHeapEl 1: writing to "); uart_print_level_1_int(ValidChunksAddr(data->dramStartAddr, bank, pos-1)); uart_print_level_1("\r\n");
    }
    write_dram_32(ValidChunksAddr(data->dramStartAddr, bank, pos-1), temp.stub);
}

static void getHeapEl(heapData * data, UINT32 bank, UINT16 pos, heapEl * res) {
    if(pos==0){
        uart_print_level_1("\r\n\r\nERROR: in getHeapEl, position is 0\r\n");
        while(1);
    }
    if(pos>data->logBlksPerBank){
        uart_print_level_1("\r\n\r\nERROR: in getHeapEl, position in heap bigger than number of log blocks\r\n");
        while(1);
    }
    uart_print("getHeapEl(bank="); uart_print_int(bank);
    uart_print(", pos="); uart_print_int(pos); uart_print("): ");
    stubUnion temp;
    temp.stub = read_dram_32(ValidChunksAddr(data->dramStartAddr, bank, pos-1));
    if (temp.val.lbn >= LOG_BLK_PER_BANK)
    {
        uart_print_level_1("ERROR in getHeapEl: found element with lbn="); uart_print_level_1_int(temp.val.lbn);
        while(1);
    }
    uart_print("lbn "); uart_print_int(temp.val.lbn);
    uart_print(" value "); uart_print_int(temp.val.value); uart_print("\r\n");
    *res = temp.val;
}

static void bubbleDown(heapData * data, UINT32 bank, UINT16 pos)
{
    uart_print("bank "); uart_print_int(bank);
    uart_print(" bubbleDown pos "); uart_print_int(pos); uart_print("\r\n");
    UINT16 posFather=pos;
    UINT16 posChild1=posFather*2;
    UINT16 posChild2=posFather*2+1;
    heapEl father;
    heapEl child1;
    heapEl child2;
    int count=0;
    while(1)
    {
        count++;
        if (count > 100000)
        {
            uart_print_level_1("Warning in bubbleDown\r\n");
            count=0;
        }
        if(posChild1>data->nElInHeap[bank])
        { // Children are both out of heap
            uart_print("Children are both out of heap\r\n");
            break;
        }
        if(posChild2>data->nElInHeap[bank])
        { // Child 1 is in heap, child 2 is out of heap
            uart_print("Child 1 is in heap, child 2 is out of heap\r\n");
            getHeapEl(data, bank, posFather, &father);
            getHeapEl(data, bank, posChild1, &child1);
            if(father.value <= child1.value)
                break;
            else
            {
                swap(data, bank, father, posFather, child1, posChild1);
                posFather = posChild1;
            }
            posChild1=posFather*2;
            posChild2=posFather*2+1;
        }
        else
        { // Both children are in heap
            uart_print("Both children are in heap\r\n");
            getHeapEl(data, bank, posFather, &father);
            getHeapEl(data, bank, posChild1, &child1);
            getHeapEl(data, bank, posChild2, &child2);
            if(father.value <= child1.value && father.value <= child2.value)
                break;
            if(child1.value < child2.value)
            {
                swap(data, bank, father, posFather, child1, posChild1);
                posFather = posChild1;
            }
            else
            {
                swap(data, bank, father, posFather, child2, posChild2);
                posFather = posChild2;
            }
            posChild1=posFather*2;
            posChild2=posFather*2+1;
        }
    }
    uart_print("BubbleDown finished\r\n");
}

static void bubbleUp(heapData * data, UINT32 bank, UINT16 pos)
{
    uart_print("bank "); uart_print_int(bank);
    uart_print(" bubbleUp pos "); uart_print_int(pos); uart_print("\r\n");
    if(pos == 1)
        return;
    UINT16 posChild=pos;
    UINT16 posFather=posChild/2;
    heapEl child;
    heapEl father;
    getHeapEl(data, bank, posChild, &child);
    getHeapEl(data, bank, posFather, &father);
    int count=0;
    while (1)
    {
        count++;
        if (count > 100000)
        {
            uart_print_level_1("Warning in bubbleUp\r\n");
            count=0;
        }
        uart_print("child="); uart_print_int(child.value);
        uart_print(" father="); uart_print_int(father.value);
        if (child.value < father.value)
        {
            swap(data, bank, child, posChild, father, posFather);
            if (posFather == 1)
                break; // now child is in the first position of the heap
        }
        else
        {
            uart_print("\r\n");
            break;
        }
        posChild=posFather;
        posFather=posChild/2;
        getHeapEl(data, bank, posChild, &child);
        getHeapEl(data, bank, posFather, &father);
    }
}

/*
void incrementValidChunks(heapData * data, UINT32 bank, UINT16 realLogLbn)
{
    uart_print("bank "); uart_print_int(bank);
    uart_print(" Incrementing lbn "); uart_print_int(realLogLbn); uart_print("\r\n");
    UINT16 logLbn = realLogLbn - data->firstLbn;
    UINT16 pos = data->validChunksHeapPositions[bank][logLbn];
    if(pos > data->logBlksPerBank)
    {
        uart_print_level_1("\r\n\r\nERROR: in incrementValidChunks, found position in heap bigger than number of log blocks for log blk ");
        uart_print_level_1_int(logLbn);
        uart_print_level_1("\r\n");
        while(1);
    }
    heapEl el;
    getHeapEl(data, bank, pos, &el);
    if(el.lbn != logLbn)
    {
        uart_print_level_1("ERROR in incrementValidChunks: found element which doesn't correpond to lbn\r\n");
        while(1);
    }
    el.value++;
    setHeapEl(data, bank, pos, el);
    uart_print("bank "); uart_print_int(bank);
    uart_print(" n v for lbn "); uart_print_int(logLbn);
    uart_print(" is "); uart_print_int(el.value); uart_print("\r\n");
    if(pos <= data->nElInHeap[bank])
        bubbleDown(data, bank, pos);
}
*/

void decrementValidChunks(heapData * data, UINT32 bank, UINT16 realLogLbn)
{
    uart_print("decrementValidChunks: bank "); uart_print_int(bank);
    uart_print(" lbn "); uart_print_int(realLogLbn); uart_print("\r\n");
    UINT16 logLbn = realLogLbn - data->firstLbn;
    //uart_print_level_1(" to "); uart_print_level_1_int(logLbn); uart_print_level_1("\r\n");
    UINT32 pos = read_dram_32(HeapPositions(data->positionsPtr, bank, logLbn));
    //UINT16 pos = data->validChunksHeapPositions[bank][logLbn];
    if(pos > data->logBlksPerBank)
    {
        uart_print_level_1("\r\n\r\nERROR: in decrementValidChunks, found position in heap bigger than number of log blocks for log blk ");
        uart_print_level_1_int(logLbn);
        uart_print_level_1("\r\n");
        while(1);
    }
    heapEl el;
    getHeapEl(data, bank, pos, &el);
    if(el.lbn != logLbn)
    {
        uart_print_level_1("ERROR in decrementValidChunks: found element which doesn't correpond to lbn\r\n");
        uart_print_level_1("el.lbn = "); uart_print_level_1_int(el.lbn); uart_print_level_1(" lbn = "); uart_print_level_1_int(logLbn); uart_print_level_1("\r\n");
        while(1);
    }
    if (el.value == 0)
    {
        uart_print_level_1("ERROR in decrementValidChunks: decresing valid chunks below 0\r\n");
        while(1);
    }
    uart_print("Decrement lbn "); uart_print_int(realLogLbn);
    uart_print(" from "); uart_print_int(el.value);
    el.value--;
    uart_print(" to "); uart_print_int(el.value); uart_print("\r\n");
    setHeapEl(data, bank, pos, el);
    if(pos <= data->nElInHeap[bank])
    {
        bubbleUp(data, bank, pos);
    }
}

void decrementValidChunksByN(heapData * data, UINT32 bank, UINT16 realLogLbn, UINT16 n)
{
    uart_print("decrementValidChunksByN: bank "); uart_print_int(bank);
    uart_print(" lbn "); uart_print_int(realLogLbn);
    uart_print(" n "); uart_print_int(n); uart_print("\r\n");
    UINT16 logLbn = realLogLbn - data->firstLbn;
    UINT32 pos = read_dram_32(HeapPositions(data->positionsPtr, bank, logLbn));
    if(pos > data->logBlksPerBank)
    {
        uart_print_level_1("\r\n\r\nERROR: in decrementValidChunksByN, found position in heap bigger than number of log blocks for log blk ");
        uart_print_level_1_int(logLbn);
        uart_print_level_1("\r\n");
        while(1);
    }
    heapEl el;
    getHeapEl(data, bank, pos, &el);
    if(el.lbn != logLbn)
    {
        uart_print_level_1("ERROR in decrementValidChunksByN: found element which doesn't correpond to lbn\r\n");
        uart_print_level_1("el.lbn = "); uart_print_level_1_int(el.lbn); uart_print_level_1(" lbn = "); uart_print_level_1_int(logLbn); uart_print_level_1("\r\n");
        while(1);
    }
    if (el.value == 0)
    {
        uart_print_level_1("ERROR in decrementValidChunksByN: decresing valid chunks below 0\r\n");
        while(1);
    }
    uart_print("Decrement lbn "); uart_print_int(realLogLbn);
    uart_print(" from "); uart_print_int(el.value);
    el.value = el.value - n;
    uart_print(" to "); uart_print_int(el.value); uart_print("\r\n");
    setHeapEl(data, bank, pos, el);
    if(pos <= data->nElInHeap[bank])
    {
        bubbleUp(data, bank, pos);
    }
}

void setValidChunks(heapData * data, UINT32 bank, UINT16 realLogLbn, UINT16 initialValue)
{
    uart_print("setValidChunks bank= "); uart_print_int(bank);
    uart_print(" blk= "); uart_print_int(realLogLbn);
    uart_print(" value= "); uart_print_int(initialValue); uart_print("\r\n");
    UINT16 logLbn = realLogLbn - data->firstLbn;
    UINT32 pos = read_dram_32(HeapPositions(data->positionsPtr, bank, logLbn));
    //UINT16 pos = data->validChunksHeapPositions[bank][logLbn];
    if(pos > data->logBlksPerBank)
    {
        uart_print_level_1("\r\n\r\nERROR: in , setValidChunks found position in heap bigger than number of log blocks for log blk ");
        uart_print_level_1_int(logLbn);
        uart_print_level_1("\r\n");
        while(1);
    }
    if(pos <= data->nElInHeap[bank])
    {
        uart_print_level_1("\r\n\r\nERROR: in , setValidChunks trying to set value for blk that is already inserted in heap ");
        uart_print_level_1_int(logLbn);
        uart_print_level_1("\r\n");
        while(1);
    }
    heapEl el;
    getHeapEl(data, bank, pos, &el);
    if(el.lbn != logLbn)
    {
        uart_print_level_1("ERROR in setValidChunks: found element which doesn't correpond to lbn\r\n");
        while(1);
    }
    el.value=initialValue;
    setHeapEl(data, bank, pos, el);
}

void resetValidChunksAndRemove(heapData * data, UINT32 bank, UINT16 realLogLbn, UINT16 initialValue)
{
    uart_print("bank "); uart_print_int(bank);
    uart_print(" Reset and remove lbn "); uart_print_int(realLogLbn); uart_print("\r\n");
    UINT16 logLbn = realLogLbn - data->firstLbn;
    UINT32 pos = read_dram_32(HeapPositions(data->positionsPtr, bank, logLbn));
    //UINT16 pos = data->validChunksHeapPositions[bank][logLbn];
    if(pos > data->logBlksPerBank)
    {
        uart_print_level_1("\r\n\r\nERROR: in , resetValidChunksAndRemove found position in heap bigger than number of log blocks for log blk ");
        uart_print_level_1_int(logLbn);
        uart_print_level_1("\r\n");
        while(1);
    }
    heapEl el;
    getHeapEl(data, bank, pos, &el);
    if(el.lbn != logLbn)
    {
        uart_print_level_1("ERROR in resetValidChunksAndRemove: found element which doesn't correpond to lbn\r\n");
        while(1);
    }
    el.value=initialValue;
    setHeapEl(data, bank, pos, el);
    UINT16 lastPos = data->nElInHeap[bank];
    heapEl lastEl;
    getHeapEl(data, bank, lastPos, &lastEl);
    swap(data, bank, el, pos, lastEl, lastPos);
    data->nElInHeap[bank]--;
    bubbleDown(data, bank, pos);
}

void insertBlkInHeap(heapData * data, UINT32 bank, UINT16 realLogLbn){
    uart_print("bank "); uart_print_int(bank);
    uart_print(" Insert lbn "); uart_print_int(realLogLbn); uart_print("\r\n");
    UINT16 logLbn = realLogLbn - data->firstLbn;
    data->nElInHeap[bank]++;
    UINT32 pos = read_dram_32(HeapPositions(data->positionsPtr, bank, logLbn));
    //UINT16 pos = data->validChunksHeapPositions[bank][logLbn];
    if(pos > data->logBlksPerBank){
        uart_print_level_1("\r\n\r\nERROR in insertBlkInHeap: found position in heap bigger than number of log blocks for log blk\r\n");
        while(1);
    }
    heapEl el;
    getHeapEl(data, bank, pos, &el);
    if(el.lbn != logLbn){
        uart_print_level_1("ERROR in insertBlkInHeap: found element which doesn't correpond to lbn\r\n");
        while(1);
    }
    UINT16 lastPos = data->nElInHeap[bank];
    if(lastPos > data->logBlksPerBank){
        uart_print_level_1("\r\n\r\nERROR in insertBlkInHeap: last position in heap bigger than number of log blocks for log blk\r\n");
        while(1);
    }
    heapEl lastEl;
    getHeapEl(data, bank, lastPos, &lastEl);
    swap(data, bank, el, pos, lastEl, lastPos);
    if (data->nElInHeap[bank] > 1)
        bubbleUp(data, bank, lastPos);
}

UINT32 getVictim(heapData * data, UINT32 bank){
    heapEl el;
    getHeapEl(data, bank, 1, &el);
    if(data->nElInHeap[bank]>0){
        uart_print("bank "); uart_print_int(bank);
        uart_print(" Get Victim: "); uart_print_int(el.lbn);
        uart_print(" real lbn "); uart_print_int((el.lbn + data->firstLbn)); uart_print("\r\n");
        return (el.lbn + data->firstLbn);
    }
    else{
        uart_print("Get Victim: empty heap!!!\r\n");
        return INVALID;
        //while(1);
    }
}

UINT32 getVictimValidPagesNumber(heapData * data, UINT32 bank)
{
    heapEl el;
    getHeapEl(data, bank, 1, &el);
    if(data->nElInHeap[bank]>0){
        uart_print("getVictimValidPagesNumber bank "); uart_print_int(bank);
        uart_print(" lbn: "); uart_print_int(el.lbn);
        uart_print(" real lbn: "); uart_print_int((el.lbn + data->firstLbn));
        uart_print(" value: "); uart_print_int(el.value); uart_print("\r\n");
        return (el.value);
    }
    else
    {
        uart_print("getVictimValidPagesNumber: empty heap!!!\r\n");
        return INVALID;
    }
}

/*
void dumpHeap(heapData * data, UINT32 bank)
{
    uart_print_level_1("\r\nDump heap for bank "); uart_print_level_1_int(bank); uart_print_level_1("\r\n");
    for (UINT32 i=1; i<=data->logBlksPerBank; ++i)
    {
        heapEl el;
        getHeapEl(data, bank, i, &el);
        uart_print_level_1("Pos "); uart_print_level_1_int(i);
        uart_print_level_1(" lbn="); uart_print_level_1_int(el.lbn);
        uart_print_level_1(" val="); uart_print_level_1_int(el.value); uart_print_level_1("\r\n");
        if (i==data->nElInHeap[bank])
        {
            uart_print_level_1("------------------------------------------------------------\r\n");
        }
    }
}
*/

void ValidChunksHeapInit(heapData * data, UINT32 blksPerBank, UINT32 firstLbn, UINT16 initialValue){
    uart_print("Initializing Heap:\r\n");
    data->logBlksPerBank = blksPerBank;
    data->firstLbn = firstLbn;
    for(UINT32 bank=0; bank<NUM_BANKS; bank++){
        uart_print("bank "); uart_print_int(bank); uart_print("\r\n");
        UINT16 pos=1;
        for(int i=0; i<blksPerBank; i++){
            uart_print("lbn "); uart_print_int(i); uart_print("\r\n");
            if((HeapPositions(data->positionsPtr, bank, i)) >=(DRAM_BASE + DRAM_SIZE))
            {
                uart_print_level_1("ERROR in heap::ValidChunksHeapInit 1: writing to "); uart_print_level_1_int(HeapPositions(data->positionsPtr, bank, i)); uart_print_level_1("\r\n");
            }
            write_dram_32(HeapPositions(data->positionsPtr, bank, i), pos);
            //data->validChunksHeapPositions[bank][i]=pos;
            heapEl temp;
            temp.value=initialValue;
            temp.lbn=i;
            setHeapEl(data, bank, pos, temp);
            pos++;
        }
        data->nElInHeap[bank]=0;
    }
}

/*
void heapTest(heapData * data)
{
    uart_print("heapTest\r\n");
    UINT32 bank=0;
    UINT32 victimLbn;
    //victimLbn = getVictim(data, bank); // Do not uncomment because now heap blocks if getVictim is called on empty heap

    UINT8 error=0;

    //if (victimLbn != INVALID) {
        //uart_print("heapTest ERROR: empty heap does not return INVALID\r\n");
        //error=1;
    //}

    victimLbn=4;
    decrementValidChunks(data, bank, victimLbn);

    //victimLbn = getVictim(data, bank);

    //if (victimLbn != INVALID) {
        //uart_print("heapTest ERROR: after decrementing one blk, but not inserting it, the victim is not INVALID\r\n");
        //error=1;
    //}

    victimLbn=4;
    insertBlkInHeap(data, bank, victimLbn);

    victimLbn = getVictim(data, bank);

    if (victimLbn != 4) {
        uart_print("heapTest ERROR: getVictim returns "); uart_print_int(victimLbn); uart_print(" instead of 4\r\n");
        error=1;
    }

    victimLbn=7;
    insertBlkInHeap(data, bank, victimLbn);

    victimLbn=8;
    insertBlkInHeap(data, bank, victimLbn);

    victimLbn = getVictim(data, bank);

    if (victimLbn != 4) {
        uart_print("heapTest ERROR: getVictim returns "); uart_print_int(victimLbn); uart_print(" instead of 4 after inserting lbn 7 and 8 but not decreasing them\r\n");
        error=1;
    }

    victimLbn=4;
    decrementValidChunks(data, bank, victimLbn);

    victimLbn=5;
    decrementValidChunks(data, bank, victimLbn);

    victimLbn=6;
    decrementValidChunks(data, bank, victimLbn);

    victimLbn = getVictim(data, bank);

    if (victimLbn != 4) {
        uart_print("heapTest ERROR: getVictim returns "); uart_print_int(victimLbn); uart_print(" instead of 4 after inserting lbn 7 and 8 but not decreasing them and decreasing 5 and 6 but not inserting them\r\n");
        error=1;
    }

    victimLbn=5;
    insertBlkInHeap(data, bank, victimLbn);

    victimLbn = getVictim(data, bank);

    if (victimLbn != 4) {
        uart_print("heapTest ERROR: getVictim returns "); uart_print_int(victimLbn); uart_print(" instead of 4 after inserting 5 decreased once, while 4 was decreased twice\r\n");
        error=1;
    }

    victimLbn=5;
    decrementValidChunks(data, bank, victimLbn);

    victimLbn=5;
    decrementValidChunks(data, bank, victimLbn);

    victimLbn=6;
    decrementValidChunks(data, bank, victimLbn);

    victimLbn=6;
    decrementValidChunks(data, bank, victimLbn);

    victimLbn=6;
    decrementValidChunks(data, bank, victimLbn);

    victimLbn = getVictim(data, bank);

    if (victimLbn != 5) {
        uart_print("heapTest ERROR: getVictim returns "); uart_print_int(victimLbn); uart_print(" instead of 5 after decreasing 5 three times, and 4 was decreased twice\r\n");
        error=1;
    }

    victimLbn=6;
    insertBlkInHeap(data, bank, victimLbn);

    victimLbn = getVictim(data, bank);

    if (victimLbn != 6) {
        uart_print("heapTest ERROR: getVictim returns "); uart_print_int(victimLbn); uart_print(" instead of 6 after decreasing 6 four times, 5 three time and 4 two times\r\n");
        error=1;
    }

    victimLbn=6;
    resetValidChunksAndRemove(data, bank, victimLbn, CHUNKS_PER_LOG_BLK);

    victimLbn = getVictim(data, bank);

    if (victimLbn != 5) {
        uart_print("heapTest ERROR: getVictim returns "); uart_print_int(victimLbn); uart_print(" instead of 5 after 6 has been removed\r\n");
        error=1;
    }

    if (error) {
        uart_print_level_1("heap test failed!\r\n");
        while(1);
    } else {
        uart_print("heap test successful\r\n");
    }
}
*/
