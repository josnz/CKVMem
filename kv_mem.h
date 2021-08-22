#define _CRT_SECURE_NO_WARNINGS
#pragma once
#include <Windows.h>
#include <stdbool.h>
#include <stdio.h>

#pragma pack(1)

typedef long long INT64;


#ifdef _WIN64
#define ADDR unsigned long long
#define HD_MEM (kv_mem* mem,HANDLE)0xFFFFFFFFFFFFFFFF
#else
#define ADDR unsigned _int64
#define HD_MEM (kv_mem* mem,HANDLE)0xFFFFFFFF
#endif // _WIN32

#define KV_KEYNAME_LEN 50
#define KV_TYPENAME_LEN 20
#define KV_STRUCT_VAR_SUM_LEN 50

#define KV_STATE_OK 0
#define KV_STATE_KEYNAME_LEN_TOO_LONG -1
#define KV_STATE_MEM_OVERFLOW -2
#define KV_STATE_MEM_TYPE_NO_MATCH -3
#define KV_STATE_MEM_NO_WRITE_RIGT -4
#define KV_STATE_KEY_EXIST -5
#define KV_STATE_KEY_NO_EXIST -6

#define KV_OK_CHECK(X) X>-1?true:false

#define KV_TYPE_EMPTY 0
#define KV_TYPE_INT8 1
#define KV_TYPE_INT16 2
#define KV_TYPE_INT32 3
#define KV_TYPE_INT64 4
#define KV_TYPE_FLOAT 5
#define KV_TYPE_DOUBLE 6
#define KV_TYPE_STRING 7
#define KV_TYPE_UNKNOW 8

typedef struct
{
	char KeyName[KV_KEYNAME_LEN];
	char TypeName[KV_TYPENAME_LEN];
	unsigned int type;
	unsigned long size;
	unsigned int ownID;
	ADDR addrOffset;
	int varIndex;

} kvDataInfo;


typedef struct
{
	unsigned long long totalMemorySize; // shared memory total size
	unsigned long long dataMemorySize; //data memory total size
	unsigned long long varTotalMaxSum; // variable max sum
	unsigned long long varTotalSum; // actual varialbe sum
	ADDR byteIndexOfLastData; 
	ADDR byteIndexOfLastVDataInfo;
	unsigned long long userCounter; //
	char lockName[KV_KEYNAME_LEN];
	int locked;
} kvMemHead;

typedef struct
{
	HANDLE hMutex;
	HANDLE m_hSharedMemoryFile;

	void* pSharedMem;
	void* pDataAddr;

	DWORD	dwMaximumSizeHigh;
	DWORD	dwMaximumSizeLow;
	DWORD	memTotalSize;
	kvMemHead* pMemHead;
	bool isInit;
	unsigned int ownID;
	void* dataInfoDict;
	void* dataDict;
	unsigned long long varTotalSum;
} kv_mem;

#ifdef  __cplusplus

extern "C"
{

#endif //  __cplusplus

	void kvInitDict(kv_mem* mem);
	void kvAddDataInfoToDict(kv_mem* mem, char* keyName);
	void kvAddDataPtrToDict(kv_mem* mem, char* keyName);

	bool kvInit(kv_mem* mem, const char* memName, ADDR nDataMemorySize, unsigned long long nVarCount);
	bool kvMapMem(kv_mem* mem, const char* memName);
	bool kvMapMemEx(kv_mem* mem, const char* memName, ADDR nDataMemorySize, unsigned long long nVarCount);
	bool kvGetMemSizeByName(const char* memName, ADDR* nDataMemorySize, unsigned long long* nVarMaxSize);
	bool kvUnMapMem(kv_mem* mem);
	void kvSetOwnID(kv_mem* mem,unsigned int id);
	void kvResetMem(kv_mem* mem);

	void kvLock(kv_mem* mem, const char* lockName);
	bool kvUnlock(kv_mem* mem);
	bool kvIsLock(kv_mem* mem);
	char* kvGetLockName(kv_mem* mem);

	// add int16 variable
	// if return value >= 0,it is varaible index; if return value < 0,it is fault number
	int kvAddInt16(kv_mem* mem, const char* keyName, short val);

	// add int32 variable
	// if return value >= 0,it is varaible index; if return value < 0,it is fault number
	int kvAddInt32(kv_mem* mem, const char* keyName, int val);

	// add int64 variable
	// if return value >= 0,it is varaible index; if return value < 0,it is fault number
	int kvAddInt64(kv_mem* mem, const char* strName, long long val);
	int kvAddFloat(kv_mem* mem, const char* strName, float val);
	int kvAddDouble(kv_mem* mem, const char* strName, double val);
	int kvAddString(kv_mem* mem, const char* strName, const char* val,int size);
	int kvAddStruct(kv_mem* mem, const char* strName, const void* val, unsigned long size);

	bool kvCheckSpace(kv_mem* mem, int valSize);

	kvDataInfo kvGetDataInfo(kv_mem* mem, const char* keyName);
	// check ownID
	void* kvGetPtr(kv_mem* mem, const char* keyName);
	// no check ownID
	void* kvForceGetPtr(kv_mem* mem, const char* keyName);
	void* kvGetPtrByVarIndex(kv_mem* mem, unsigned int nVarIndex);

	int kvSetByString(kv_mem* mem, const char* keyName, const char* val);
	int kvSetShort(kv_mem* mem, const char* keyName, short val);
	int kvSetInt(kv_mem* mem, const char* keyName, int val);
	int kvSetInt64(kv_mem* mem, const char* keyName, long long val);
	int kvSetFloat(kv_mem* mem, const char* keyName, float val);
	int kvSetDouble(kv_mem* mem, const char* keyName, double val);
	int kvCreateString(kv_mem* mem, const char* keyName, const char* val,int size);
	int kvSetString(kv_mem* mem, const char* keyName, const char* val);
	int kvCreateStruct(kv_mem* mem, const char* keyName, const void* val, int size);
	int kvSetStruct(kv_mem* mem, const char* keyName, const void* val);
	int kvSetRaw(kv_mem* mem, const char* keyName, const void* val,int size);

	bool kvSetStructName(kv_mem* mem, const char* keyName, const char* structName);
	bool kvCreateStructVarAddressMap(kv_mem* mem, const char* keyName, const char* varName, int type ,int size,int offset);
	bool kvDelectStructVarAddressMap(kv_mem* mem, const char* keyName, const char* varName);

	int kvCreateProtectVarShort(kv_mem* mem, const char* keyName, short val);
	int kvCreateProtectVarInt(kv_mem* mem, const char* keyName, int val);
	int kvCreateProtectVarInt64(kv_mem* mem, const char* keyName, long long val);
	int kvCreateProtectVarFloat(kv_mem* mem, const char* keyName, float val);
	int kvCreateProtectVarDouble(kv_mem* mem, const char* keyName, double val);
	int kvCreateProtectVarString(kv_mem* mem, const char* keyName, const char* val,int size);
	int kvCreateProtectVarStruct(kv_mem* mem, const char* keyName, const void* val, int size);

	bool kvGetShort(kv_mem* mem, const char* keyName, short* val);
	bool kvGetInt(kv_mem* mem, const char* keyName, int* val);
	bool kvGetLong(kv_mem* mem, const char* keyName, long* val);
	bool kvGetInt64(kv_mem* mem, const char* keyName, long long* val);
	bool kvGetFloat(kv_mem* mem, const char* keyName, float* val);
	bool kvGetDouble(kv_mem* mem, const char* keyName, double* val);
	bool kvGetStruct(kv_mem* mem, const char* keyName, void* pVal);
	bool kvGetValue(kv_mem* mem, const char* keyName, void* pVal);



	char* kvGetStringByKeyName(kv_mem* mem, const char* keyName);
	char* kvGetStringByIndex(kv_mem* mem, int index);

	int kvGetVariableTotalSum(kv_mem* mem);
	int kvGetVariableTotalMaxSum(kv_mem* mem);
	unsigned long long  kvGetMemSize(kv_mem* mem);
	unsigned long long  kvGetDataSize(kv_mem* mem);
	unsigned long long kvRemainDataSize(kv_mem* mem);
	unsigned long long kvUsedDataSize(kv_mem* mem);
	char* kvGetKeyName(kv_mem* mem, int pos);
	char* kvGetType(kv_mem* mem, const char* keyName);
	int kvGetTypeNumber(kv_mem* mem, const char* keyName);

	int kvGetVariableSize(kv_mem* mem, const char* keyName);

	bool kvIsExist(kv_mem* mem, const char* keyName);
	bool kvFlush(kv_mem* mem);

	// redis shm
	void kvCreateRedisShm(kv_mem* mem, const char* fileName);
	int kvRedisCommand(kv_mem* mem,const char* format, ...);
	char* kvRedisGetReply(kv_mem* mem, int clientSendIndex);
	void kvRedisSetReply(kv_mem* mem,char* buf,int len);
	char* kvRedisGetCommand(kv_mem* mem);

#ifdef  __cplusplus
}

#endif //  __cplusplus
#pragma pack()
