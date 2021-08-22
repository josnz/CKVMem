// CTestY.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "dict.h"
#include <stdio.h>
#include <string.h>
#include "Win32_Interop\Win32_PThread.h"

uint64_t hashCallback(const void* key) {
    //printf("hashCallback\n");
    return dictGenHashFunction((unsigned char*)key, strlen((char*)key));
}

int compareCallback(void* privdata, const void* key1, const void* key2) {
    printf("compareCallback\n");
    return strcmp(key1, key2) == 0;
}

void freeCallback(void* privdata, void* val) {
    DICT_NOTUSED(privdata);

    //free(val);
}

dictType BenchmarkDictType = {
    hashCallback,
    NULL,
    NULL,
    compareCallback,
    freeCallback,
    NULL
};
CRITICAL_SECTION cs;
static int var;
extern pthread_mutex_t used_memory_mutex;
int main()
{
    pthread_mutex_init(&used_memory_mutex, NULL);


    void* dict = dictCreate(&BenchmarkDictType, NULL);
    void* dict1 = dictCreate(&BenchmarkDictType, NULL);

    int count = 100;

    char name[]="rrxx";

    dictAdd(dict, name, 1);
    name[3] = 'i';
    //dictAdd(dict, "rr.x2", 2);

    

    dictEntry* de = dictFind(dict, name);

    if (de == NULL)
    {
     //   char* val = dictGetVal(de);
        printf("cannt find x1 is");
    }
    else
    {
        printf("find kv");
    }
    dictDelete(dict, "x");

    return 0;
}
