#include "cleanList.h"
#include "ftl_metadata.h"

#if OPTION_UART_DEBUG == 1
    #if OPTION_UART_DEBUG_LIST == 0
        #define uart_print(X)
        #define uart_print_int(X)
    #endif
#endif

void cleanListInit(listData * data, UINT32 startAddr, UINT32 numNodesPerBank)
{
    uart_print("cleanListInit: startAddr = "); uart_print_int(startAddr);
    uart_print(", numNodesPerBank = "); uart_print_int(numNodesPerBank); uart_print("\r\n");
    UINT32 addr=startAddr;
    for(int bank=0; bank<NUM_BANKS; bank++)
    {
        data->cleanListUnusedNodes[bank]=(logListNode *)addr;
        logListNode* currentCleanLogBlkNode=(logListNode *)addr;
        logListNode* nextCleanLogBlkNode=currentCleanLogBlkNode+1;
        uart_print("Initialize clean list for bank "); uart_print_int(bank);
        uart_print("\r\nStarting address is "); uart_print_int(addr); uart_print("\r\n");
        uart_print("Initializing list of unused nodes\r\n");
        int i=0;
        for(i=0; i<numNodesPerBank-1; i++)
        {
            uart_print("Node "); uart_print_int(i); uart_print("\r\n");
            write_dram_32(&currentCleanLogBlkNode->next, nextCleanLogBlkNode);
            currentCleanLogBlkNode=nextCleanLogBlkNode;
            nextCleanLogBlkNode=currentCleanLogBlkNode+1;
        }
        uart_print("Node "); uart_print_int(i); uart_print(" is the last node, write NULL in next field\r\n");
        write_dram_32(&currentCleanLogBlkNode->next, NULL);
        data->cleanListHead[bank]=NULL;
        data->cleanListTail[bank]=NULL;
        data->size[bank]=0;
        addr = (UINT32) nextCleanLogBlkNode; // start address on next bank
    }
}

void cleanListPush(listData * data, UINT32 bank, UINT32 logLbn)
{
    uart_print("cleanListPush bank="); uart_print_int(bank); uart_print("\r\n");
    if(data->cleanListUnusedNodes[bank] == NULL)
    {
        uart_print_level_1("error in cleanListPush on bank ");
        uart_print_level_1_int(bank);
        uart_print_level_1(": list is full\r\n");
        while(1);
    }
    uart_print("cleanListPush bank "); uart_print_int(bank);
    uart_print(". Lbn "); uart_print_int(logLbn); uart_print("\r\n");
    //uart_print(" to address "); uart_print_int(data->cleanListUnusedNodes[bank]); uart_print("\r\n");
    logListNode* newNode = data->cleanListUnusedNodes[bank]; // take node from unused nodes list
    data->cleanListUnusedNodes[bank] = (logListNode *)read_dram_32(&newNode->next); // update unused list head
    if((UINT32)&newNode->lbn >=(DRAM_BASE + DRAM_SIZE))
    {
        uart_print_level_1("ERROR in cleanListPush 1: writing to "); uart_print_level_1_int((UINT32)&newNode->lbn); uart_print_level_1("\r\n");
        while(1);
    }
    write_dram_32(&newNode->lbn, logLbn); // setup new node
    if( ( (UINT32)&newNode->next ) >= (DRAM_BASE + DRAM_SIZE))
    {
        uart_print_level_1("ERROR in cleanListPush 2: writing to "); uart_print_level_1_int((UINT32)&newNode->next); uart_print_level_1("\r\n");
        while(1);
    }
    write_dram_32(&newNode->next, NULL);
    if(data->cleanListTail[bank] != NULL)
    { // if cleanList was empty then tail is NULL and shouldn't be updated!
        if( ( (UINT32) &data->cleanListTail[bank]->next ) >= (DRAM_BASE + DRAM_SIZE))
        {
            uart_print_level_1("ERROR in cleanListPush 3: writing to "); uart_print_level_1_int( (UINT32) &data->cleanListTail[bank]->next); uart_print_level_1("\r\n");
            while(1);
        }
        write_dram_32(&data->cleanListTail[bank]->next, newNode); //insert new node at the end of the list
    }
    data->cleanListTail[bank]=newNode;
    if(data->cleanListHead[bank] == NULL)
    { // just pushed first element at the bottom of an empty list => update head pointer
        uart_print("Pushed first element at the bottom of an empty list, reinitialized head pointer\r\n");
        data->cleanListHead[bank] = data->cleanListTail[bank];
    }
    data->size[bank]++;
}

UINT32 cleanListPop(listData * data, UINT32 bank)
{
    uart_print("cleanListPop bank="); uart_print_int(bank); uart_print("\r\n");
    if(data->cleanListHead[bank] == NULL)
    {
        uart_print_level_1("error in cleanListPop on bank ");
        uart_print_level_1_int(bank);
        uart_print_level_1(": list is empty\r\n");
        while(1);
    }
    uart_print("cleanListPop bank "); uart_print_int(bank);
    //uart_print(" from address "); uart_print_int(&data->cleanListHead[bank]->lbn);
    //uart_print(" head node is at address "); uart_print_int(data->cleanListHead[bank]); uart_print("\r\n");
    if(data->cleanListHead[bank] == data->cleanListTail[bank])
    { // about to pop last element from the list => clean tail pointer
        uart_print("About to pop the last element, clear the tail pointer");
        data->cleanListTail[bank]=NULL;
    }
    UINT32 logLbn = read_dram_32(&data->cleanListHead[bank]->lbn); // get lpn
    logListNode* newHead = (logListNode *) read_dram_32(&data->cleanListHead[bank]->next); // save next clean node
    if( ( (UINT32) &data->cleanListHead[bank]->next ) >= (DRAM_BASE + DRAM_SIZE) )
    {
        uart_print_level_1("ERROR in cleanListPop 1: writing to "); uart_print_level_1_int( (UINT32) &data->cleanListHead[bank]->next); uart_print_level_1("\r\n");
        while(1);
    }
    write_dram_32(&data->cleanListHead[bank]->next, data->cleanListUnusedNodes[bank]); // insert current node on top of unused list
    data->cleanListUnusedNodes[bank]=data->cleanListHead[bank];
    data->cleanListHead[bank]=newHead; // update clean list head
    if (data->size[bank] == 0)
    {
        uart_print_level_1("error in cleanListPop on bank ");
        uart_print_level_1_int(bank);
        uart_print_level_1(": decreasing size below zero\r\n");
        while(1);
    }
    data->size[bank]--;
    uart_print(". Lbn "); uart_print_int(logLbn); uart_print("\r\n");
    return logLbn;
}

UINT32 cleanListSize(listData * data, UINT32 bank)
{
    uart_print("cleanListSize bank="); uart_print_int(bank); uart_print(" size="); uart_print_int(data->size[bank]); uart_print("\r\n");
    return data->size[bank];
}

/*
void cleanListDump(listData * data, UINT32 bank)
{
    uart_print_level_1("CleanListDump bank "); uart_print_level_1_int(bank); uart_print_level_1("\r\n");
    if(data->cleanListHead[bank] == NULL)
    {
        uart_print_level_1("List empty\r\n");
        return;
    }
    logListNode* node = data->cleanListHead[bank];
    uart_print_level_1("Size="); uart_print_level_1_int(data->size[bank]); uart_print_level_1("\r\n");
    while(node != NULL)
    {
        uart_print_level_1_int(read_dram_32(&node->lbn)); uart_print_level_1(" ");
        node = read_dram_32(&node->next);
    }
    uart_print_level_1("\r\n");
}
*/

/*
void testCleanList()
{
    uart_print("testCleanList\r\n");
    uart_print("\r\nNode size: "); uart_print_int(sizeof(logListNode));

    // Init w list
    cleanListInit(&cleanListDataWrite, CleanList(0), LOG_BLK_PER_BANK);
    for(int bank=0; bank<NUM_BANKS; bank++)
    {
        for(int lbn=0; lbn<LOG_BLK_PER_BANK; lbn++)
        {
            cleanListPush(&cleanListDataWrite, bank, lbn);
        }
    }
    // Pop nodes from w list
    for(int lbn=0; lbn<LOG_BLK_PER_BANK; lbn++)
    {
        for(int bank=0; bank<NUM_BANKS; bank++)
        {
            UINT32 newLogLbn = cleanListPop(&cleanListDataWrite, bank);
        }
    }

    // Pop nodes from w list
    for(int lbn=0; lbn<LOG_BLK_PER_BANK; lbn++)
    {
        for(int bank=0; bank<NUM_BANKS; bank++)
        {
            cleanListPush(&cleanListDataWrite, bank, lbn);
            UINT32 newLogLbn = cleanListPop(&cleanListDataWrite, bank);
            if (newLogLbn != lbn)
            {
                uart_print_level_1("testCleanList failed at first checkpoint!");
                while(1);
            }
        }
    }
    for(int lbn=LOG_BLK_PER_BANK-1; lbn>=0; lbn--)
    {
        for(int bank=0; bank<NUM_BANKS; bank++)
        {
            cleanListPush(&cleanListDataWrite, bank, lbn);
        }
    }
    for(int lbn=LOG_BLK_PER_BANK-1; lbn>=0; lbn--)
    {
        for(int bank=0; bank<NUM_BANKS; bank++)
        {
            UINT32 newLogLbn = cleanListPop(&cleanListDataWrite, bank);
            if (newLogLbn != lbn)
            {
                uart_print_level_1("testCleanList failed at third checkpoint!");
                while(1);
            }
        }
    }
    uart_print("testCleanList completed succesfully!\r\n");
}
*/
