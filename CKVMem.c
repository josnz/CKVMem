// CKVMem.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

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

