
#include <stdio.h>
#include "kv_mem.h"


int main()
{
    kv_mem mem;
    kvCreateRedisShm(&mem, "redis");
    int index = kvRedisCommand(&mem, "set x x9");
    char* buf = NULL;
    while (!buf)
    {
        buf = kvRedisGetReply(&mem, index);
    }
    printf("ret: %s", buf);
    Sleep(10000);
    return 1;
}

