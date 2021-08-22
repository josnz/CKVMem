
#include "kv_mem.h"
#include "Sddl.h"
#include "dict/dict.h"
#include "dict/sds.h"

#include "dict\Win32_Interop\Win32_PThread.h"
#include <stdio.h>
#include <string.h>

#define KV_NAME_PREFIX "Global\\"

uint64_t hashCallback(const void* key) {
	//printf("hashCallback\n");
	//printf("hash: %s-%n\n", (char*)key, strlen((char*)key));
	return dictGenHashFunction((unsigned char*)key, strlen((char*)key));
}

int compareCallback2(void* privdata, const void* key1, const void* key2) {
	//printf("compareCallback\n");
	//printf("compare: %s - %s\n", key1, key2);
	return strcmp(key1,key2) == 0;
}

void freeCallback(void* privdata, void* val) {
	DICT_NOTUSED(privdata);

	//free(val);
}


/*
typedef struct dictType {
	uint64_t(*hashFunction)(const void* key);
	void* (*keyDup)(void* privdata, const void* key);
	void* (*valDup)(void* privdata, const void* obj);
	int (*keyCompare)(void* privdata, const void* key1, const void* key2);
	void (*keyDestructor)(void* privdata, void* key);
	void (*valDestructor)(void* privdata, void* obj);
} dictType;
*/


dictType BenchmarkDictType = {
	hashCallback,
	NULL,
	NULL,
	compareCallback2,
	freeCallback,
	NULL
};


CRITICAL_SECTION cs;
extern pthread_mutex_t used_memory_mutex;


bool kvUpdataNewDataToDict(kv_mem* mem)
{
	if (mem->varTotalSum < mem->pMemHead->varTotalSum)
	{
		kvDataInfo* pVDataInfo = NULL;
		int pos = sizeof(kvMemHead);

		for (int i = mem->varTotalSum; i < mem->pMemHead->varTotalSum; i++)
		{
			pVDataInfo = (kvDataInfo*)((char*)mem->pSharedMem + pos + i * sizeof(kvDataInfo));
			
			dictAdd((dict*)mem->dataInfoDict, pVDataInfo->KeyName, pVDataInfo);
			dictAdd((dict*)mem->dataDict, pVDataInfo->KeyName, (char*)mem->pSharedMem + pVDataInfo->addrOffset);
		}
		mem->varTotalSum = mem->pMemHead->varTotalSum;
		return true;
	}
	return false;
}


kvDataInfo* kvGetDataInfoPtr(kv_mem* mem, const char* keyName)
{
	dictEntry* de = dictFind((dict*)mem->dataInfoDict, keyName);

	if (de != NULL)
	{
		kvDataInfo* pVData = (kvDataInfo*)dictGetVal(de);

		return pVData;
	}
	else
	{
		if (kvUpdataNewDataToDict(mem))
		{
			return kvGetDataInfoPtr(mem, keyName);
		}
		return NULL;
	}
	return NULL;
}

void kvInitDict(kv_mem* mem)
{
	pthread_mutex_init(&used_memory_mutex, NULL);

	(dict*)mem->dataInfoDict = dictCreate(&BenchmarkDictType, NULL);
	(dict*)mem->dataDict = dictCreate(&BenchmarkDictType, NULL);
	mem->varTotalSum = mem->pMemHead->varTotalSum;

	kvDataInfo* pVDataInfo = NULL;
	int pos = sizeof(kvMemHead);

	for (int i = 0; i < mem->pMemHead->varTotalSum; i++)
	{
		pVDataInfo = (kvDataInfo*)((char*)mem->pSharedMem + pos + i * sizeof(kvDataInfo));
		
		dictAdd((dict*)mem->dataInfoDict, pVDataInfo->KeyName, pVDataInfo);
		dictAdd((dict*)mem->dataDict, pVDataInfo->KeyName, (char*)mem->pSharedMem + pVDataInfo->addrOffset);
	}
}



void kvAddDataInfoToDict(kv_mem* mem, char* keyName)
{

	kvDataInfo* pVData = NULL;
	int pos = sizeof(kvMemHead);

	for (int i = 0; i < mem->pMemHead->varTotalSum; i++)
	{
		pVData = (kvDataInfo*)((char*)mem->pSharedMem + pos + i * sizeof(kvDataInfo));
		if (strcmp(keyName, pVData->KeyName) == 0)
		{
			dictAdd((dict*)mem->dataInfoDict, keyName, pVData);
			return;
		}
	}

	return;
}

void kvAddDataPtrToDict(kv_mem* mem, char* keyName)
{
	dictAdd((dict*)mem->dataInfoDict, keyName, kvGetPtr(mem, keyName));
}

kvDataInfo* kvGetDataInfoPtrByVarIndex(kv_mem* mem, unsigned int nVarIndex)
{
	kvDataInfo* pVData = NULL;
	if (nVarIndex < mem->pMemHead->varTotalSum)
	{
		pVData = (kvDataInfo*)((char*)mem->pSharedMem + sizeof(kvMemHead) + nVarIndex * sizeof(kvDataInfo));
	}

	return pVData;
}


kvDataInfo kvGetDataInfo(kv_mem* mem, const char* keyName)
{
	dictEntry* de = dictFind((dict*)mem->dataInfoDict, keyName);

	if (de != NULL)
	{
		kvDataInfo* pVData = (kvDataInfo*)dictGetVal(de);
		return *pVData;
	}

	kvDataInfo info;
	info.addrOffset = 0;
	info.KeyName[0] = '\0';
	info.size = 0;
	info.type = KV_TYPE_EMPTY;

	return info;
}


bool kvInit(kv_mem* mem, const char* memName, ADDR nDataMemorySize, unsigned long long nVarMaxSize)
{
	bool ret = true;
	char prefix[100] = KV_NAME_PREFIX;// "Global\\";
	const char* csName = strcat(prefix, memName);// memName;
	
	bool m_bAlreadyExist = false;
	mem->isInit = false;
	char csMutexName[50] = "Mutex";
	char csInitMutexName[50] = "Init";
	strcat(csMutexName, csName);
	strcat(csInitMutexName, csName);

	m_bAlreadyExist = false;
	mem->hMutex = CreateMutex(NULL, false, csMutexName);
	HANDLE hMutex = CreateMutex(NULL, false, csInitMutexName);;
	WaitForSingleObject(hMutex, INFINITE);

	mem->memTotalSize = nDataMemorySize + sizeof(kvMemHead) + nVarMaxSize * sizeof(kvDataInfo);

	SECURITY_DESCRIPTOR sd;
	SECURITY_ATTRIBUTES sa;
	SECURITY_ATTRIBUTES* pSecurityAttributes = &sa;
	InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = &sd;

	mem->m_hSharedMemoryFile = CreateFileMapping(INVALID_HANDLE_VALUE,
		pSecurityAttributes,
		PAGE_READWRITE,
		0/*mem->dwMaximumSizeHigh*/,
		mem->memTotalSize/*mem->dwMaximumSizeLow*/,
		csName);


	if (mem->m_hSharedMemoryFile == NULL)
	{
		m_bAlreadyExist = false;
		mem->isInit = false;
		ret = false;
		printf("Last error: [%d]\n", GetLastError());
	}
	else
	{
		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			unsigned long long dataMemorySize = 0;
			unsigned long long varTotalSum = 0;
			bool ret = false;

			ret = kvGetMemSizeByName(memName, &dataMemorySize, &varTotalSum);
			if (dataMemorySize == nDataMemorySize && varTotalSum == nVarMaxSize)
			{
				ret = kvMapMem(mem, memName);
			}
			m_bAlreadyExist = true;
		}
		else
		{

			mem->pSharedMem = MapViewOfFile(mem->m_hSharedMemoryFile,
				FILE_MAP_ALL_ACCESS,
				0/*dwFileOffsetHigh*/,
				0/*dwFileOffsetLow*/,
				mem->memTotalSize);

			if (mem->pSharedMem == NULL)
			{
				mem->isInit = false;
				CloseHandle(mem->m_hSharedMemoryFile);
				return false;
			}
			else
			{
				char strLockName[] = ("Init");

				mem->pMemHead = (kvMemHead*)mem->pSharedMem;
				
				if (!m_bAlreadyExist)
				{
					mem->pMemHead->varTotalMaxSum = nVarMaxSize;
					mem->pMemHead->totalMemorySize = mem->memTotalSize;
					mem->pMemHead->dataMemorySize = nDataMemorySize;
					mem->pMemHead->varTotalSum = 0;
					mem->pMemHead->byteIndexOfLastVDataInfo = sizeof(kvMemHead);
					mem->pMemHead->byteIndexOfLastData = mem->pMemHead->byteIndexOfLastVDataInfo + nVarMaxSize * sizeof(kvDataInfo);
					mem->pMemHead->locked = false;
					mem->pDataAddr = (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData;

					//printf("Init memory [name: %s] [ptr: OX%p]\n", csName, mem->pMemHead);
					printf("Init memory [name: %s] [ptr: OX%p] [size: %d] [TotalVarSize: %d]\n", memName, mem->pMemHead, nDataMemorySize, nVarMaxSize);
				}
				else
				{
					mem->pMemHead->userCounter++;

					printf("Map memory [name: %s] [ptr: OX%p]\n", csName, mem->pMemHead);
				}
				mem->isInit = true;
				mem->ownID = 0;
				kvInitDict(mem);
			}
			
		}
		
	}
	

	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
	return ret;
}

bool kvMapMemEx(kv_mem* mem, const char* memName, ADDR nDataMemorySize, unsigned long long nVarCount)
{
	bool ret = false;
	char prefix[100] = KV_NAME_PREFIX;// "Global\\";
	const char* csName = strcat(prefix, memName);// memName;

	bool m_bAlreadyExist = true;
	bool isInit = false;
	char csMutexName[50] = "Mutex";
	char csInitMutexName[50] = "Init";
	strcat(csMutexName, csName);
	strcat(csInitMutexName, csName);

	mem->hMutex = CreateMutex(NULL, false, csMutexName);
	HANDLE hMutex = CreateMutex(NULL, false, csInitMutexName);;
	WaitForSingleObject(hMutex, INFINITE);

	mem->memTotalSize = nDataMemorySize + sizeof(kvMemHead) + nVarCount * sizeof(kvDataInfo);

	SECURITY_DESCRIPTOR sd;
	SECURITY_ATTRIBUTES sa;
	SECURITY_ATTRIBUTES* pSecurityAttributes = &sa;

	InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = &sd;

	mem->m_hSharedMemoryFile = CreateFileMapping(INVALID_HANDLE_VALUE,
		pSecurityAttributes,
		PAGE_READWRITE,
		0/*mem->dwMaximumSizeHigh*/,
		mem->memTotalSize/*mem->dwMaximumSizeLow*/,
		csName);

	if (mem->m_hSharedMemoryFile == NULL)
	{
		m_bAlreadyExist = false;
		isInit = false;
	}
	else
	{
		printf("Last error: [%d]\n", GetLastError());
		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			mem->pSharedMem = MapViewOfFile(mem->m_hSharedMemoryFile,
				FILE_MAP_ALL_ACCESS,
				0/*dwFileOffsetHigh*/,
				0/*dwFileOffsetLow*/,
				mem->memTotalSize);

			if (mem->pSharedMem == NULL)
			{
				isInit = false;
				CloseHandle(mem->m_hSharedMemoryFile);
			}
			else
			{
				char strLockName[] = ("MapMem");

				mem->pMemHead = (kvMemHead*)mem->pSharedMem;
				
				if (m_bAlreadyExist && nVarCount > 0)
				{
					mem->pMemHead->userCounter++;
					kvInitDict(mem);
					printf("Map memory [name: %s] [ptr: OX%p] [size: %d] [TotalVarSize: %d]\n", memName, mem->pMemHead, nDataMemorySize, nVarCount);
				}
				isInit = true;
				mem->ownID = 0;
				ret = true;				
			}
		}
	}
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);

	return ret;
}

bool kvGetMemSizeByName(const char* memName, ADDR* nDataMemorySize, unsigned long long* nVarMaxSize)
{
	kv_mem m;
	m.dataDict = NULL;
	m.dataInfoDict = NULL;
	char prefix[100] = KV_NAME_PREFIX;// "Global\\";
	const char* csName = strcat(prefix, memName);// memName;
	bool ret = kvMapMemEx(&m, memName, sizeof(kvMemHead), 0);

	if (ret)
	{
		*nDataMemorySize = m.pMemHead->dataMemorySize;
		*nVarMaxSize = m.pMemHead->varTotalMaxSum;
	}
	kvUnMapMem(&m);
	return ret;
}

bool kvMapMem(kv_mem* mem, const char* memName)
{
	unsigned long long dataMemorySize = 0;
	unsigned long long varTotalSum = 0;
	bool ret = false;
	char prefix[100] = KV_NAME_PREFIX;// "Global\\";
	const char* csName = strcat(prefix, memName);// memName;

	ret = kvGetMemSizeByName(memName, &dataMemorySize, &varTotalSum);

	printf("Map memory size: %d/%d\n", varTotalSum, dataMemorySize);

	if (ret)
	{
		return kvMapMemEx(mem, memName, dataMemorySize, varTotalSum);
	}
	return ret;
}

void kvSetOwnID(kv_mem* mem,unsigned int id)
{
	mem->ownID = id;
}

void dictEmptyCB(void* p)
{
	//printf("empty dict");
}

bool kvUnMapMem(kv_mem* mem)
{
	if (UnmapViewOfFile(mem->pSharedMem))
	{
		if (mem->dataDict != NULL)
		{
			dictEmpty((dict*)mem->dataDict, dictEmptyCB);
			dictEmpty((dict*)mem->dataInfoDict, dictEmptyCB);
		}

		return CloseHandle(mem->m_hSharedMemoryFile);
	}
	return false;
}

void kvResetMem(kv_mem* mem)
{
	kvLock(mem, "reset");

	//mem->pMemHead->varTotalMaxSum = nVarMaxSize;
	mem->pMemHead->totalMemorySize = mem->memTotalSize;
	//mem->pMemHead->dataMemorySize = nDataMemorySize;
	mem->pMemHead->varTotalSum = 0;
	mem->pMemHead->byteIndexOfLastVDataInfo = sizeof(kvMemHead);
	mem->pMemHead->byteIndexOfLastData = mem->pMemHead->byteIndexOfLastVDataInfo + mem->pMemHead->varTotalMaxSum * sizeof(kvDataInfo);
	mem->pMemHead->locked = false;
	mem->pDataAddr = (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData;

	for (int i = 0; i < mem->pMemHead->varTotalSum; i++)
	{
		kvDataInfo* pInfo = (kvDataInfo*)((char*)mem->pSharedMem + sizeof(kvMemHead)) + i;
		pInfo->KeyName[0] = "\0";
		pInfo->ownID = 0;
		pInfo->TypeName[0] = '\0';
	}
	
	mem->pMemHead->varTotalSum = 0;
	mem->varTotalSum = 0;

	if (mem->dataDict != NULL)
	{
		dictEmpty((dict*)mem->dataDict, dictEmptyCB);
		dictEmpty((dict*)mem->dataInfoDict, dictEmptyCB);
	}

	kvUnlock(mem);
}

void kvLock(kv_mem* mem, const char* lockName)
{
	WaitForSingleObject(mem->hMutex, INFINITE);
	//if (mem->isInit)
	{
		int size = min(strlen(lockName), KV_KEYNAME_LEN);
		memcpy(mem->pMemHead->lockName, lockName, size);
		mem->pMemHead->lockName[min(size, KV_KEYNAME_LEN - 1)] = '\0';
		mem->pMemHead->locked = 1;
	}	
}

bool kvUnlock(kv_mem* mem)
{
	mem->pMemHead->locked = 0;
	return ReleaseMutex(mem->hMutex);
}

bool kvIsLock(kv_mem* mem)
{
	bool ret = false;
	DWORD dw = WaitForSingleObject(mem->hMutex, 0); 
	switch (dw)
	{
	case WAIT_OBJECT_0:
		// hProcess所代表的进程在5秒内结束
		ret = false;
		
		break;

	case WAIT_TIMEOUT:
		// 等待时间超过5秒
		ret = true;
		break;

	case WAIT_FAILED:
		// 函数调用失败，比如传递了一个无效的句柄
		break;
	}
	ReleaseMutex(mem->hMutex);
	return ret; mem->pMemHead->locked;
}

char* kvGetLockName(kv_mem* mem)
{
	return mem->pMemHead->lockName;
}

int kvAddInt16(kv_mem* mem, const char* keyName, short val)
{
	int ret = 0;

	if (strlen(keyName) > KV_KEYNAME_LEN - 1)
	{
		printf("name length is too long!");
		ret = KV_STATE_KEYNAME_LEN_TOO_LONG;
	}
	else
	{
		int valSize = sizeof(val);
		kvLock(mem,__func__);
		if (!kvCheckSpace(mem, valSize))
			ret = KV_STATE_MEM_OVERFLOW;
		else
		{
			kvDataInfo data;
			strcpy(data.KeyName, keyName);
			data.KeyName[strlen(keyName)] = '\0';
			data.addrOffset = mem->pMemHead->byteIndexOfLastData;
			data.size = valSize;
			data.type = KV_TYPE_INT16;
			data.varIndex = (mem->pMemHead->varTotalSum)++;
			data.ownID = 0;
		
			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo, (char*)&data, sizeof(kvDataInfo));

			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData, &val, valSize);

			kvDataInfo* pData = (kvDataInfo*)((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataInfoDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData);

			mem->pMemHead->byteIndexOfLastVDataInfo = mem->pMemHead->byteIndexOfLastVDataInfo + sizeof(kvDataInfo);
			mem->pMemHead->byteIndexOfLastData = mem->pMemHead->byteIndexOfLastData + valSize;

			ret = (mem->pMemHead->varTotalSum);
		}
		kvUnlock(mem);
	}
	
	return ret;
}


int kvAddInt32(kv_mem* mem, const char* keyName, int val)
{
	int ret = 0;

	if (strlen(keyName) > KV_KEYNAME_LEN - 1)
	{
		printf("name length is too long!");
		ret = KV_STATE_KEYNAME_LEN_TOO_LONG;
	}
	else
	{
		int valSize = sizeof(val);
		kvLock(mem,__func__);
		if (!kvCheckSpace(mem, valSize))
			ret = KV_STATE_MEM_OVERFLOW;
		else
		{
			kvDataInfo data;
			strcpy(data.KeyName, keyName);
			data.KeyName[strlen(keyName)] = '\0';
			data.addrOffset = mem->pMemHead->byteIndexOfLastData;
			data.size = valSize;
			data.type = KV_TYPE_INT32;
			data.varIndex = (mem->pMemHead->varTotalSum)++;
			data.ownID = 0;
	
			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo, (char*)&data, sizeof(kvDataInfo));

			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData, &val, valSize);

			kvDataInfo* pData = (kvDataInfo*)((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataInfoDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData);

			mem->pMemHead->byteIndexOfLastVDataInfo = mem->pMemHead->byteIndexOfLastVDataInfo + sizeof(kvDataInfo);
			mem->pMemHead->byteIndexOfLastData = mem->pMemHead->byteIndexOfLastData + valSize;

			ret = (mem->pMemHead->varTotalSum);
		}
		kvUnlock(mem);
	}

	return ret;
}

int kvAddInt64(kv_mem* mem, const char* keyName, long long val)
{
	int ret = 0;

	if (strlen(keyName) > KV_KEYNAME_LEN - 1)
	{
		printf("name length is too long!");
		ret = KV_STATE_KEYNAME_LEN_TOO_LONG;
	}
	else
	{
		int valSize = sizeof(val);
		kvLock(mem,__func__);
		if (!kvCheckSpace(mem, valSize))
			ret = KV_STATE_MEM_OVERFLOW;
		else
		{
			kvDataInfo data;
			strcpy(data.KeyName, keyName);
			data.KeyName[strlen(keyName)] = '\0';
			data.addrOffset = mem->pMemHead->byteIndexOfLastData;
			data.size = valSize;
			data.type = KV_TYPE_INT64;
			data.varIndex = (mem->pMemHead->varTotalSum)++;
			data.ownID = 0;

			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo, (char*)&data, sizeof(kvDataInfo));

			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData, &val, valSize);

			kvDataInfo* pData = (kvDataInfo*)((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataInfoDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData);

			mem->pMemHead->byteIndexOfLastVDataInfo = mem->pMemHead->byteIndexOfLastVDataInfo + sizeof(kvDataInfo);
			mem->pMemHead->byteIndexOfLastData = mem->pMemHead->byteIndexOfLastData + valSize;

			ret = (mem->pMemHead->varTotalSum);
		}
		kvUnlock(mem);
	}

	return ret;
}

int kvAddFloat(kv_mem* mem, const char* keyName, float val)
{
	int ret = 0;

	if (strlen(keyName) > KV_KEYNAME_LEN - 1)
	{
		printf("name length is too long!");
		ret = KV_STATE_KEYNAME_LEN_TOO_LONG;
	}
	else
	{
		int valSize = sizeof(val);
		kvLock(mem,__func__);
		if (!kvCheckSpace(mem, valSize))
			ret = KV_STATE_MEM_OVERFLOW;
		else
		{
			kvDataInfo data;
			strcpy(data.KeyName, keyName);
			data.KeyName[strlen(keyName)] = '\0';
			data.addrOffset = mem->pMemHead->byteIndexOfLastData;
			data.size = valSize;
			data.type = KV_TYPE_FLOAT;
			data.varIndex = (mem->pMemHead->varTotalSum)++;
			data.ownID = 0;
			
			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo, (char*)&data, sizeof(kvDataInfo));

			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData, &val, valSize);

			kvDataInfo* pData = (kvDataInfo*)((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataInfoDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData);

			mem->pMemHead->byteIndexOfLastVDataInfo = mem->pMemHead->byteIndexOfLastVDataInfo + sizeof(kvDataInfo);
			mem->pMemHead->byteIndexOfLastData = mem->pMemHead->byteIndexOfLastData + valSize;

			ret = (mem->pMemHead->varTotalSum);
		}
		kvUnlock(mem);
	}

	return ret;
}

int kvAddDouble(kv_mem* mem, const char* keyName, double val)
{
	int ret = 0;

	if (strlen(keyName) > KV_KEYNAME_LEN - 1)
	{
		printf("name length is too long!");
		ret = KV_STATE_KEYNAME_LEN_TOO_LONG;
	}
	else
	{
		int valSize = sizeof(val);
		kvLock(mem,__func__);
		if (!kvCheckSpace(mem, valSize))
			ret = KV_STATE_MEM_OVERFLOW;
		else
		{
			kvDataInfo data;
			strcpy(data.KeyName, keyName);
			data.KeyName[strlen(keyName)] = '\0';
			data.addrOffset = mem->pMemHead->byteIndexOfLastData;
			data.size = valSize;
			data.type = KV_TYPE_DOUBLE;
			data.varIndex = (mem->pMemHead->varTotalSum)++;
			data.ownID = 0;

			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo, (char*)&data, sizeof(kvDataInfo));

			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData, &val, valSize);

			kvDataInfo* pData = (kvDataInfo*)((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataInfoDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData);

			mem->pMemHead->byteIndexOfLastVDataInfo = mem->pMemHead->byteIndexOfLastVDataInfo + sizeof(kvDataInfo);
			mem->pMemHead->byteIndexOfLastData = mem->pMemHead->byteIndexOfLastData + valSize;

			ret = (mem->pMemHead->varTotalSum);
		}
		kvUnlock(mem);
	}

	return ret;
}

int kvAddString(kv_mem* mem, const char* keyName, const char* var,int size)
{
	int ret = 0;
	if (strlen(keyName) > KV_KEYNAME_LEN - 1)
	{
		printf("name length is too long!");
		ret = KV_STATE_KEYNAME_LEN_TOO_LONG;
	}
	else
	{
		int valSize = size;// strlen(var);// sizeof(val);
		kvLock(mem,__func__);
		if (!kvCheckSpace(mem, valSize))
			ret = KV_STATE_MEM_OVERFLOW;
		else
		{
			kvDataInfo data;
			strcpy(data.KeyName, keyName);
			data.KeyName[strlen(keyName)] = '\0';
			data.addrOffset = mem->pMemHead->byteIndexOfLastData;
			data.size = valSize;
			data.type = KV_TYPE_STRING;
			data.varIndex = (mem->pMemHead->varTotalSum)++;
			data.ownID = 0;
			
			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo, (char*)&data, sizeof(kvDataInfo));
			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData, var, valSize);

			kvDataInfo* pData = (kvDataInfo*)((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataInfoDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataDict, pData->KeyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData);

			mem->pMemHead->byteIndexOfLastVDataInfo = mem->pMemHead->byteIndexOfLastVDataInfo + sizeof(kvDataInfo);
			mem->pMemHead->byteIndexOfLastData = mem->pMemHead->byteIndexOfLastData + valSize;
			ret = (mem->pMemHead->varTotalSum);
		}
		kvUnlock(mem);
	}
	return ret;
}

int kvAddStruct(kv_mem* mem, const char* keyName, const void* var, unsigned long size)
{
	int ret = 0;
	if (strlen(keyName) > KV_KEYNAME_LEN - 1)
	{
		printf("name length is too long!");
		ret = KV_STATE_KEYNAME_LEN_TOO_LONG;
	}
	else
	{
		int valSize = size;
		kvLock(mem,__func__);
		if (!kvCheckSpace(mem, valSize))
			ret = KV_STATE_MEM_OVERFLOW;
		else
		{
			kvDataInfo data;
			strcpy(data.KeyName, keyName);
			data.KeyName[strlen(keyName)] = '\0';
			data.addrOffset = mem->pMemHead->byteIndexOfLastData;
			data.size = valSize;
			data.type = KV_TYPE_UNKNOW;
			data.varIndex = (mem->pMemHead->varTotalSum)++;
			data.TypeName[0] = 'N';
			data.TypeName[1] = 'O';
			data.TypeName[2] = 'N';
			data.TypeName[3] = '\0';
			data.ownID = 0;
			
			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo, (char*)&data, sizeof(kvDataInfo));
			memcpy((char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData, var, valSize);

			dictAdd((dict*)mem->dataInfoDict, (char*)keyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastVDataInfo);
			dictAdd((dict*)mem->dataDict, (char*)keyName, (char*)mem->pSharedMem + mem->pMemHead->byteIndexOfLastData);

			mem->pMemHead->byteIndexOfLastVDataInfo = mem->pMemHead->byteIndexOfLastVDataInfo + sizeof(kvDataInfo);
			mem->pMemHead->byteIndexOfLastData = mem->pMemHead->byteIndexOfLastData + valSize;
			ret = (mem->pMemHead->varTotalSum);
		}
		kvUnlock(mem);
	}
	return ret;
}

bool kvCheckSpace(kv_mem* mem, int valSize)
{
	if (mem->pMemHead->byteIndexOfLastVDataInfo / sizeof(kvDataInfo) > mem->pMemHead->varTotalMaxSum)
		return false;

	if (mem->memTotalSize < mem->pMemHead->byteIndexOfLastData + valSize)
		return false;

	return true;
}


int kvSetShort(kv_mem* mem, const char* keyName, short val)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		return kvAddInt16(mem, keyName, val);
	}

	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;

	if (pVData->type != KV_TYPE_INT16)
		return KV_STATE_MEM_TYPE_NO_MATCH;

	*((short*)((char*)mem->pSharedMem + pVData->addrOffset)) = val;
	return pVData->varIndex;
}

int kvSetInt(kv_mem* mem, const char* keyName, int val)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		return kvAddInt32(mem, keyName, val);
	}

	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;

	if (pVData->type != KV_TYPE_INT32)
		return KV_STATE_MEM_TYPE_NO_MATCH;

	*((int*)((char*)mem->pSharedMem + pVData->addrOffset)) = val;
	return pVData->varIndex;
}


int kvSetInt64(kv_mem* mem, const char* keyName, long long val)
{
	
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{

		return kvAddInt64(mem, keyName, val);
	}

	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;

	if (pVData->type != KV_TYPE_INT64)
		return KV_STATE_MEM_TYPE_NO_MATCH;

	*((long*)((char*)mem->pSharedMem + pVData->addrOffset)) = val;
	return pVData->varIndex;
}


int kvSetFloat(kv_mem* mem, const char* keyName, float val)
{

	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		return kvAddFloat(mem, keyName, val);
	}

	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;

	if (pVData->type != KV_TYPE_FLOAT)
		return KV_STATE_MEM_TYPE_NO_MATCH;

	*((float*)((char*)mem->pSharedMem + pVData->addrOffset)) = val;
	return pVData->varIndex;
}

int kvSetDouble(kv_mem* mem, const char* keyName, double val)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		return kvAddDouble(mem, keyName, val);
	}

	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;

	if (pVData->type != KV_TYPE_DOUBLE)
		return KV_STATE_MEM_TYPE_NO_MATCH;

	*((double*)((char*)mem->pSharedMem + pVData->addrOffset)) = val;
	return pVData->varIndex;
}

int kvSetRaw(kv_mem* mem, const char* keyName, const void* val, int size)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		return KV_STATE_KEY_NO_EXIST;
	}

	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;



	char* pData = (char*)mem->pSharedMem + pVData->addrOffset;
	int nSize = min(pVData->size, size);
	memcpy(pData, val, nSize);

	if (pVData->type == KV_TYPE_STRING)
	{

		if (nSize < pVData->size)
		{
			pData[nSize] = '\0';
		}
		else
		{
			pData[nSize - 1] = '\0';
		}
	}

	return pVData->varIndex;
}

int kvCreateString(kv_mem* mem, const char* keyName, const char* val,int size)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		return kvAddString(mem, keyName, val,size);
	}
	else
	{
		return KV_STATE_KEY_EXIST;
	}

	/*
	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;

	if (pVData->type != KV_TYPE_STRING)
		return KV_STATE_MEM_TYPE_NO_MATCH;

	char* pData = (char*)mem->pSharedMem + pVData->addrOffset;
	int nSize = min(pVData->size, strlen(val));
	memcpy(pData, val, nSize);

	if (nSize < pVData->size)
	{
		pData[nSize] = '\0';
	}
	else
	{
		pData[nSize - 1] = '\0';
	}

	return pVData->varIndex;
	*/
}

int kvSetString(kv_mem* mem, const char* keyName, const char* val)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		return KV_STATE_KEY_NO_EXIST;// kvAddString(mem, keyName, val, strlen(val));
	}

	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;

	if (pVData->type != KV_TYPE_STRING)
		return KV_STATE_MEM_TYPE_NO_MATCH;

	char* pData = (char*)mem->pSharedMem + pVData->addrOffset;
	int nSize = min(pVData->size, strlen(val));
	memcpy(pData, val,nSize);

	if (nSize < pVData->size)
	{
		pData[nSize] = '\0';
	}
	else
	{
		pData[nSize - 1] = '\0';
	}

	return pVData->varIndex;
}

int kvCreateStruct(kv_mem* mem, const char* keyName, const void* val, int size)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		return kvAddStruct(mem, keyName, val, size);
	}

	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;

	if (pVData->type != KV_TYPE_UNKNOW)
		return KV_STATE_MEM_TYPE_NO_MATCH;

	char* pData = (char*)mem->pSharedMem + pVData->addrOffset;
	int nSize = min(pVData->size, size);
	memcpy(pData, val, nSize);

	return pVData->varIndex;
}

int kvSetStruct(kv_mem* mem, const char* keyName, const void* val)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData != NULL)
	{
		if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
			return KV_STATE_MEM_NO_WRITE_RIGT;

		if (pVData->type != KV_TYPE_UNKNOW)
			return KV_STATE_MEM_TYPE_NO_MATCH;

		char* pData = (char*)mem->pSharedMem + pVData->addrOffset;
		
		memcpy(pData, val, pVData->size);

		return pVData->varIndex;
	}
	else
	{
		return KV_STATE_KEY_NO_EXIST;
	}
}

bool kvSetStructName(kv_mem* mem, const char* keyName, const char* structName)
{
	bool ret = false;
	if (strlen(structName) < KV_TYPENAME_LEN)
	{
		kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
		if (pVData != NULL)
		{
			if (pVData->type == KV_TYPE_UNKNOW)
			{
				memcpy(pVData->TypeName, structName, strlen(structName));
				//sprintf_s(pVData->TypeName, strlen(structName), "%s", structName);
				pVData->TypeName[strlen(structName)] = '\0';
				ret = true;
			}
		}
	}
	return ret;
}

bool kvCreateStructVarAddressMap(kv_mem* mem, const char* keyName, const char* varName, int type, int size,int offset)
{
	bool ret = false;
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	
	if (pVData != NULL)
	{
		if (pVData->type == KV_TYPE_UNKNOW)
		{
			kvLock(mem,__func__);
			char* pData = kvGetPtr(mem, keyName);
			char* name = malloc(KV_STRUCT_VAR_SUM_LEN);
			
			name[0] = '\0';
			char dot[] = ".";

			strcat(name, keyName);
			strcat(name, dot);
			strcat(name, varName);

			//kvDelectStructVarAddressMap(mem, keyName, varName);
			if (!kvIsExist(mem,name))
			{
				kvDataInfo data;
				strcpy(data.KeyName, name);
				data.KeyName[strlen(keyName)] = '\0';
				data.addrOffset = pVData->addrOffset + offset;
				data.size = size;
				data.type = type;
				data.varIndex = -1;
				data.ownID = pVData->ownID;

				char* buf = malloc(sizeof(kvDataInfo));
				memcpy(buf, &data,sizeof(kvDataInfo));
				dictAdd((dict*)mem->dataInfoDict, name,buf);

				dictAdd((dict*)mem->dataDict, name, pData + offset);
				ret = true;
				
			}
			else
			{
				realloc(name, KV_STRUCT_VAR_SUM_LEN);
			}
			kvUnlock(mem);

		}
	}
	return ret;
}

bool kvDelectStructVarAddressMap(kv_mem* mem, const char* keyName, const char* varName)
{
	bool ret = false;
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);

	if (pVData != NULL)
	{
		if (pVData->type == KV_TYPE_UNKNOW)
		{
			char* pData = kvGetPtr(mem, keyName);
			char* name = malloc(KV_STRUCT_VAR_SUM_LEN);

			name[0] = '\0';
			char dot[] = ".";

			strcat(name, keyName);
			strcat(name, dot);
			strcat(name, varName);

			dictEntry* de = dictFind((dict*)mem->dataDict, name);
			if (de != NULL)
			{
				char* dictKey = dictGetKey(de);
				dictDelete((dict*)mem->dataDict, name);
				realloc(dictKey, KV_STRUCT_VAR_SUM_LEN);
				ret = true;
			}
		}
	}
	return ret;
}

int kvCreateProtectVarShort(kv_mem* mem, const char* keyName, short val)
{
	int ret = KV_STATE_OK;
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		ret = kvAddInt16(mem, keyName, val);
		if (KV_OK_CHECK(ret))
		{
			pVData = kvGetDataInfoPtr(mem, keyName);
			pVData->ownID = mem->ownID;
		}
	}
	else
	{
		ret = KV_STATE_KEY_EXIST;
	}
	return ret;
}
int kvCreateProtectVarInt(kv_mem* mem, const char* keyName, int val)
{
	int ret = KV_STATE_OK;
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		ret = kvAddInt32(mem, keyName, val);
		if (KV_OK_CHECK(ret))
		{
			pVData = kvGetDataInfoPtr(mem, keyName);
			pVData->ownID = mem->ownID;
		}
	}
	else
	{
		ret = KV_STATE_KEY_EXIST;
	}
	return ret;
}
int kvCreateProtectVarInt64(kv_mem* mem, const char* keyName, long long val)
{
	int ret = KV_STATE_OK;
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		ret = kvAddInt64(mem, keyName, val);
		if (KV_OK_CHECK(ret))
		{
			pVData = kvGetDataInfoPtr(mem, keyName);
			pVData->ownID = mem->ownID;
		}
	}
	else
	{
		ret = KV_STATE_KEY_EXIST;
	}
	return ret;
}
int kvCreateProtectVarFloat(kv_mem* mem, const char* keyName, float val)
{
	int ret = KV_STATE_OK;
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		ret = kvAddFloat(mem, keyName, val);
		if (KV_OK_CHECK(ret))
		{
			pVData = kvGetDataInfoPtr(mem, keyName);
			pVData->ownID = mem->ownID;
		}
	}
	else
	{
		ret = KV_STATE_KEY_EXIST;
	}
	return ret;
}
int kvCreateProtectVarDouble(kv_mem* mem, const char* keyName, double val)
{
	int ret = KV_STATE_OK;
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		ret = kvAddDouble(mem, keyName, val);
		if (KV_OK_CHECK(ret))
		{
			pVData = kvGetDataInfoPtr(mem, keyName);
			pVData->ownID = mem->ownID;
		}
	}
	else
	{
		ret = KV_STATE_KEY_EXIST;
	}
	return ret;
}
int kvCreateProtectVarString(kv_mem* mem, const char* keyName, const char* val,int size)
{
	int ret = KV_STATE_OK;
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		ret = kvAddString(mem, keyName, val,size);
		if (KV_OK_CHECK(ret))
		{
			pVData = kvGetDataInfoPtr(mem, keyName);
			pVData->ownID = mem->ownID;
		}
	}
	else
	{
		ret = KV_STATE_KEY_EXIST;
	}
	return ret;
}
int kvCreateProtectVarStruct(kv_mem* mem, const char* keyName, const void* val, int size)
{
	int ret = KV_STATE_OK;
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		ret = kvAddStruct(mem, keyName, val, size);
		if (KV_OK_CHECK(ret))
		{
			pVData = kvGetDataInfoPtr(mem, keyName);
			pVData->ownID = mem->ownID;
		}
	}
	else
	{
		ret = KV_STATE_KEY_EXIST;
	}
	return ret;
}

bool kvGetShort(kv_mem* mem, const char* keyName, short* val)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return false;
	*val = *(short*)((char*)mem->pSharedMem + pVDataInfo->addrOffset);
	return true;
}


bool kvGetInt(kv_mem* mem, const char* keyName, int* val)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return false;
	*val = *(int*)((char*)mem->pSharedMem + pVDataInfo->addrOffset);
	return true;
}

bool kvGetLong(kv_mem* mem, const char* keyName, long* val)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return false;
	*val = *(long*)((char*)mem->pSharedMem + pVDataInfo->addrOffset);
	return true;
}

bool kvGetInt64(kv_mem* mem, const char* keyName, long long* val)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return false;
	*val = *(long long*)((char*)mem->pSharedMem + pVDataInfo->addrOffset);
	return true;
}

bool kvGetFloat(kv_mem* mem, const char* keyName, float* val)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return false;
	*val = *(float*)((char*)mem->pSharedMem + pVDataInfo->addrOffset);
	return true;
}

bool kvGetDouble(kv_mem* mem, const char* keyName, double* val)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return false;
	*val = *(double*)((char*)mem->pSharedMem + pVDataInfo->addrOffset);
	return true;
}

bool kvGetStruct(kv_mem* mem, const char* keyName, void* pVal)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return false;
	
	memcpy(pVal, (char*)(char*)mem->pSharedMem + pVDataInfo->addrOffset, pVDataInfo->size);

	return true;
}

bool kvGetValue(kv_mem* mem, const char* keyName, void* pVal)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return false;

	memcpy(pVal, (char*)(char*)mem->pSharedMem + pVDataInfo->addrOffset, pVDataInfo->size);

	return true;
}

int kvSetByString(kv_mem* mem, const char* keyName, const char* val)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
		return 0;

	if (pVData->ownID > 0 && pVData->ownID != mem->ownID)
		return KV_STATE_MEM_NO_WRITE_RIGT;

	char ckvGetRet[1024];
	char* pData = kvGetPtr(mem, keyName);// ((char*)mem->pSharedMem + pVData->addrOffset);// , pVData->size);
	char* strBuf;
	ckvGetRet[0] = '\0';
	switch (pVData->type)
	{
	case KV_TYPE_INT32:
	{

		//sprintf(ckvGetRet, "%d", *(int*)pData);
		*((int*)((char*)mem->pSharedMem + pVData->addrOffset)) = atoi(val);
		break;
	}
	case KV_TYPE_INT16:
	{
		*((short*)((char*)mem->pSharedMem + pVData->addrOffset)) = atoi(val);
		break;
	}
	case KV_TYPE_INT64:
	{
		*((long*)((char*)mem->pSharedMem + pVData->addrOffset)) = atoi(val);
		break;
	}
	case KV_TYPE_FLOAT:
	{
		*((float*)((char*)mem->pSharedMem + pVData->addrOffset)) = atof(val);
		break;
	}
	case KV_TYPE_DOUBLE:
	{
		*((double*)((char*)mem->pSharedMem + pVData->addrOffset)) = atof(val);
		break;
	}
	case KV_TYPE_STRING:
	{
		{
			char* pData = (char*)mem->pSharedMem + pVData->addrOffset;
			int nSize = min(pVData->size, strlen(val));
			memcpy(pData, val, nSize);
		}
		break;
	}
	case KV_TYPE_UNKNOW:
	{
		char* pData = (char*)mem->pSharedMem + pVData->addrOffset;
		int nSize = min(pVData->size, strlen(val));
		memcpy(pData, val, nSize);

		break;
	}

	default:
		break;
	}
	return 0;
}

char* kvGetStringByKeyName(kv_mem* mem, const char* keyName)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return "NULL";
	char ckvGetRet[1024*16];
	char* pData = (char*)(char*)mem->pSharedMem + pVDataInfo->addrOffset;// ((char*)mem->pSharedMem + pVData->addrOffset);// , pVData->size);
	char* strBuf;
	ckvGetRet[0] = '\0';
	switch (pVDataInfo->type)
	{
	case KV_TYPE_INT32:
	{

		sprintf(ckvGetRet, "%d", *(int*)pData);
		break;
	}
	case KV_TYPE_INT16:
	{
		sprintf(ckvGetRet, "%d", *(short*)pData);
		break;
	}
	case KV_TYPE_INT64:
	{
		sprintf(ckvGetRet, "%lld", *(INT64*)pData);
		break;
	}
	case KV_TYPE_FLOAT:
	{
		sprintf(ckvGetRet, "%e", *(float*)pData);
		break;
	}
	case KV_TYPE_DOUBLE:
	{
		sprintf(ckvGetRet, "%e", *(double*)pData);
		break;
	}
	case KV_TYPE_STRING:
	{
		memcpy(ckvGetRet, pData, min(pVDataInfo->size, sizeof(ckvGetRet)));
		
		if (pVDataInfo->size < sizeof(ckvGetRet))
		{
			ckvGetRet[pVDataInfo->size] = '\0';
		}
		else
		{
			ckvGetRet[sizeof(ckvGetRet) - 1] = '\0';
		}
		break;
	}
	case KV_TYPE_UNKNOW:
		//memcpy(ckvGetRet, pData, min(pVDataInfo->size, sizeof(ckvGetRet) - 1));
		sprintf_s(ckvGetRet, min(pVDataInfo->size, strlen(ckvGetRet) - 1), "[%s]", pData);
		//ckvGetRet[min(pVDataInfo->size, strlen(ckvGetRet) - 1)] = '\0';
		break;
	default:
		break;
	}
	return ckvGetRet;
}

char* kvGetStringByIndex(kv_mem* mem, int index)
{
	kvMemHead* pHead = (kvMemHead*)mem->pSharedMem;
	if (pHead->varTotalSum < index)
	{
		printf("index overflow\n");
		return "";
	}
	kvDataInfo* pVDataInfo = (kvDataInfo*)((char*)mem->pSharedMem + sizeof(kvMemHead) + index * sizeof(kvDataInfo));
	char ckvGetRet[1024];
	char* pData = (char*)mem->pSharedMem + pVDataInfo->addrOffset;// ((char*)mem->pSharedMem + pVDataInfo->addrOffset);
	char* strBuf;
	ckvGetRet[0] = '\0';
	switch (pVDataInfo->type)
	{
	case KV_TYPE_INT32:
	{

		sprintf(ckvGetRet, "%d", *(int*)pData);
		break;
	}
	case KV_TYPE_INT16:
	{
		sprintf(ckvGetRet, "%d", *(short*)pData);
		break;
	}
	case KV_TYPE_INT64:
	{
		sprintf(ckvGetRet, "%d", *(INT64*)pData);
		break;
	}
	case KV_TYPE_FLOAT:
	{
		sprintf(ckvGetRet, "%e", *(float*)pData);
		break;
	}
	case KV_TYPE_DOUBLE:
	{
		sprintf(ckvGetRet, "%e", *(double*)pData);
		break;
	}
	case KV_TYPE_STRING:
	{
		memcpy(ckvGetRet, pData, min(pVDataInfo->size, sizeof(ckvGetRet)));

		if (pVDataInfo->size < sizeof(ckvGetRet))
		{
			ckvGetRet[pVDataInfo->size] = '\0';
		}
		else
		{
			ckvGetRet[sizeof(ckvGetRet) - 1] = '\0';
		}
		break;
	}
	case KV_TYPE_UNKNOW:
		sprintf_s(ckvGetRet, min(pVDataInfo->size, strlen(ckvGetRet) - 1), "[%s]", pData);
		//ckvGetRet[min(pVDataInfo->size, sizeof(ckvGetRet) - 1)] = '\0';
		break;
	default:
		break;
	}
	return ckvGetRet;
}


int kvGetVariableTotalSum(kv_mem* mem)
{
	return mem->pMemHead->varTotalSum;
}

int kvGetVariableTotalMaxSum(kv_mem* mem)
{
	return mem->pMemHead->varTotalMaxSum;
}

unsigned long long  kvGetMemSize(kv_mem* mem)
{
	return mem->pMemHead->totalMemorySize;
}

unsigned long long  kvGetDataSize(kv_mem* mem)
{
	return mem->pMemHead->dataMemorySize;
}

unsigned long long kvRemainDataSize(kv_mem* mem)
{
	return mem->pMemHead->totalMemorySize - mem->pMemHead->byteIndexOfLastData;
}

unsigned long long kvUsedDataSize(kv_mem* mem)
{
	return mem->pMemHead->byteIndexOfLastData - mem->pMemHead->varTotalMaxSum * sizeof(kvDataInfo) - sizeof(kvMemHead);
}

char* kvGetKeyName(kv_mem* mem, int pos)
{
	return ((kvDataInfo*)((char*)mem->pSharedMem + sizeof(kvMemHead) + pos * sizeof(kvDataInfo)))->KeyName;
}

bool kvIsExist(kv_mem* mem, const char* keyName)
{
	kvDataInfo* pVData = kvGetDataInfoPtr(mem, keyName);
	if (pVData == NULL)
	{
		if (kvUpdataNewDataToDict(mem))
		{
			return kvIsExist(mem, keyName);
		}
		return false;
	}
	return true;
}

void* kvGetPtr(kv_mem* mem, const char* keyName)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
	{
		if (kvUpdataNewDataToDict(mem))
		{
			return kvGetPtr(mem, keyName);
		}
		return NULL;
	}

	if (pVDataInfo->ownID > 0 && pVDataInfo->ownID != mem->ownID)
		return NULL;

	return (char*)mem->pSharedMem + pVDataInfo->addrOffset;
}

void* kvForceGetPtr(kv_mem* mem, const char* keyName)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
	{
		if (kvUpdataNewDataToDict(mem))
		{
			return kvGetPtr(mem, keyName);
		}
		return NULL;
	}

	return (char*)mem->pSharedMem + pVDataInfo->addrOffset;
}

void* kvGetPtrByVarIndex(kv_mem* mem, unsigned int nVarIndex)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtrByVarIndex(mem, nVarIndex);
	if (pVDataInfo == NULL)
		return NULL;

	if (pVDataInfo->ownID > 0 && pVDataInfo->ownID != mem->ownID)
		return NULL;

	return (char*)mem->pSharedMem + pVDataInfo->addrOffset;
}

char* kvGetType(kv_mem* mem, const char* keyName)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return "NULL";

	switch (pVDataInfo->type)
	{
	case KV_TYPE_INT32:
		return "int32";
		break;
	case KV_TYPE_INT16:
		return "int16";
		break;
	case KV_TYPE_INT64:
		return "int64";
		break;
	case KV_TYPE_FLOAT:
		return "float";
		break;
	case KV_TYPE_DOUBLE:
		return "double";
		break;
	case KV_TYPE_STRING:
		return "string";
		break;
	case KV_TYPE_UNKNOW:
		return pVDataInfo->TypeName;
		break;
	default:
		break;
	}
	return "vUnknow";
}

int kvGetTypeNumber(kv_mem* mem, const char* keyName)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo == NULL)
		return KV_TYPE_EMPTY;

	return (pVDataInfo->type);
}


bool kvFlush(kv_mem* mem)
{
	return FlushViewOfFile((LPCVOID)mem->pSharedMem, mem->memTotalSize);
}

// redis shm


#define SHM_CMD_TOTAL 10
#define SHM_INFOS_NAME "shm"
typedef struct
{
	char cmd[100];
	char reply[1024 * 16];
	int replyOk;
} redisShmInfo;

typedef struct
{
	int svrReadIndex;
	int svrReplyIndex;
	int clientSendIndex;
	char noreply[3];
	redisShmInfo info[SHM_CMD_TOTAL];
} redisShms;


/* Return the number of digits of 'v' when converted to string in radix 10.
 * Implementation borrowed from link in redis/src/util.c:string2ll(). */
static uint32_t countDigits2(uint64_t v) {
	uint32_t result = 1;
	for (;;) {
		if (v < 10) return result;
		if (v < 100) return result + 1;
		if (v < 1000) return result + 2;
		if (v < 10000) return result + 3;
		v /= 10000U;
		result += 4;
	}
}

/* Helper that calculates the bulk length given a certain string length. */
static size_t bulklen2(size_t len) {
	return (size_t)(1 + countDigits2(len) + 2 + len + 2);
}

void kvCreateRedisShm(kv_mem* mem, const char* fileName)
{
	kvInit(mem, fileName, sizeof(redisShms), 1);
	redisShms* shms = malloc(sizeof(redisShms));
	shms->svrReadIndex = 0;
	shms->clientSendIndex = 0;
	shms->svrReplyIndex = 0;
	shms->noreply[0] = 'n';
	shms->noreply[1] = 'o';
	shms->noreply[2] = '\0';
	kvSetStruct(mem, SHM_INFOS_NAME, shms, sizeof(redisShms));
	free(shms);
}

int kvRedisCommandToShm(kv_mem* mem, char* cmd,int totlen)
{
	kvLock(mem, __func__);
	
	redisShms* shms = (redisShms*)kvGetPtr(mem, SHM_INFOS_NAME);
	memcpy(shms->info[shms->clientSendIndex].cmd, cmd, totlen);
	shms->info[shms->clientSendIndex].replyOk = 0;
	int index =  shms->clientSendIndex++;
	if (shms->clientSendIndex >= SHM_CMD_TOTAL)
		shms->clientSendIndex = 0;
	kvUnlock(mem);
	return index;
}

void kvRedisSetReply(kv_mem* mem, char* buf, int len)
{
	redisShms* shms = (redisShms*)kvGetPtr(mem, SHM_INFOS_NAME);
	shms->info[shms->svrReplyIndex].replyOk = 1;
	memcpy(shms->info[shms->svrReplyIndex].reply,buf,len);
	shms->svrReplyIndex++;
	if (shms->svrReplyIndex >= SHM_CMD_TOTAL)
		shms->svrReplyIndex = 0;
}

char* kvRedisGetReply(kv_mem* mem, int clientSendIndex)
{
	redisShms* shms = (redisShms*)kvGetPtr(mem, SHM_INFOS_NAME);
	int counter = 1;
	while (!shms->info[clientSendIndex].replyOk)
	{
		shms->info[clientSendIndex].replyOk = 0;
		printf(shms->info[clientSendIndex].reply);
		return shms->info[clientSendIndex].reply;
	}
	return NULL;
}



int kvRedisCommand(kv_mem* mem, const char* format, ...) {

	va_list ap;
	int ret;

	va_start(ap, format);

	const char* c = format;
	char* cmd = NULL; /* final command */
	int pos; /* position in final command */
	sds curarg, newarg; /* current argument */
	int touched = 0; /* was the current argument touched? */
	char** curargv = NULL, ** newargv = NULL;
	int argc = 0;
	int totlen = 0;
	int error_type = 0; /* 0 = no error; -1 = memory error; -2 = format error */
	int j;


	/* Build the command string accordingly to protocol */
	curarg = sdsempty();
	if (curarg == NULL)
		return -1;

	while (*c != '\0') {
		if (*c != '%' || c[1] == '\0') {
			if (*c == ' ') {
				if (touched) {
					newargv = realloc(curargv, sizeof(char*) * (argc + 1));
					if (newargv == NULL) goto memory_err;
					curargv = newargv;
					curargv[argc++] = curarg;
					totlen += (int)bulklen2(sdslen(curarg));

					/* curarg is put in argv so it can be overwritten. */
					curarg = sdsempty();
					if (curarg == NULL) goto memory_err;
					touched = 0;
				}
			}
			else {
				newarg = sdscatlen(curarg, c, 1);
				if (newarg == NULL) goto memory_err;
				curarg = newarg;
				touched = 1;
			}
		}
		else {
			char* arg;
			size_t size;

			/* Set newarg so it can be checked even if it is not touched. */
			newarg = curarg;

			switch (c[1]) {
			case 's':
				arg = va_arg(ap, char*);
				size = strlen(arg);
				if (size > 0)
					newarg = sdscatlen(curarg, arg, size);
				break;
			case 'b':
				arg = va_arg(ap, char*);
				size = va_arg(ap, size_t);
				if (size > 0)
					newarg = sdscatlen(curarg, arg, size);
				break;
			case '%':
				newarg = sdscat(curarg, "%");
				break;
			default:
				/* Try to detect printf format */
			{
				static const char intfmts[] = "diouxX";
				static const char flags[] = "#0-+ ";
				char _format[16];
				const char* _p = c + 1;
				size_t _l = 0;
				va_list _cpy;

				/* Flags */
				while (*_p != '\0' && strchr(flags, *_p) != NULL) _p++;

				/* Field width */
				while (*_p != '\0' && isdigit(*_p)) _p++;

				/* Precision */
				if (*_p == '.') {
					_p++;
					while (*_p != '\0' && isdigit(*_p)) _p++;
				}

				/* Copy va_list before consuming with va_arg */
				va_copy(_cpy, ap);

				/* Integer conversion (without modifiers) */
				if (strchr(intfmts, *_p) != NULL) {
					va_arg(ap, int);
					goto fmt_valid;
				}

				/* Double conversion (without modifiers) */
				if (strchr("eEfFgGaA", *_p) != NULL) {
					va_arg(ap, double);
					goto fmt_valid;
				}

				/* Size: char */
				if (_p[0] == 'h' && _p[1] == 'h') {
					_p += 2;
					if (*_p != '\0' && strchr(intfmts, *_p) != NULL) {
						va_arg(ap, int); /* char gets promoted to int */
						goto fmt_valid;
					}
					goto fmt_invalid;
				}

				/* Size: short */
				if (_p[0] == 'h') {
					_p += 1;
					if (*_p != '\0' && strchr(intfmts, *_p) != NULL) {
						va_arg(ap, int); /* short gets promoted to int */
						goto fmt_valid;
					}
					goto fmt_invalid;
				}

				/* Size: PORT_LONGLONG */
				if (_p[0] == 'l' && _p[1] == 'l') {
					_p += 2;
					if (*_p != '\0' && strchr(intfmts, *_p) != NULL) {
						va_arg(ap, PORT_LONGLONG);
						goto fmt_valid;
					}
					goto fmt_invalid;
				}

				/* Size: PORT_LONG */
				if (_p[0] == 'l') {
					_p += 1;
					if (*_p != '\0' && strchr(intfmts, *_p) != NULL) {
						va_arg(ap, PORT_LONG);
						goto fmt_valid;
					}
					goto fmt_invalid;
				}

			fmt_invalid:
				va_end(_cpy);
				goto format_err;

			fmt_valid:
				_l = (_p + 1) - c;
				if (_l < sizeof(_format) - 2) {
					memcpy(_format, c, _l);
					_format[_l] = '\0';
					newarg = sdscatvprintf(curarg, _format, _cpy);

					/* Update current position (note: outer blocks
					 * increment c twice so compensate here) */
					c = _p - 1;
				}

				va_end(_cpy);
				break;
			}
			}

			if (newarg == NULL) goto memory_err;
			curarg = newarg;

			touched = 1;
			c++;
		}
		c++;
	}

	/* Add the last argument if needed */
	if (touched) {
		newargv = realloc(curargv, sizeof(char*) * (argc + 1));
		if (newargv == NULL) goto memory_err;
		curargv = newargv;
		curargv[argc++] = curarg;
		totlen += (int)bulklen2(sdslen(curarg));
	}
	else {
		sdsfree(curarg);
	}

	/* Clear curarg because it was put in curargv or was free'd. */
	curarg = NULL;

	/* Add bytes needed to hold multi bulk count */
	totlen += 1 + countDigits2(argc) + 2;

	/* Build the command at protocol level */
	cmd = malloc(totlen + 1);
	if (cmd == NULL) goto memory_err;

	pos = sprintf(cmd, "*%d\r\n", argc);
	for (j = 0; j < argc; j++) {
		pos += sprintf(cmd + pos, "$%Iu\r\n", sdslen(curargv[j]));                  WIN_PORT_FIX /* %zu -> %Iu */
			memcpy(cmd + pos, curargv[j], sdslen(curargv[j]));
		pos += (int)sdslen(curargv[j]);
		sdsfree(curargv[j]);
		cmd[pos++] = '\r';
		cmd[pos++] = '\n';
	}
	//assert(pos == totlen);
	cmd[pos] = '\0';

	free(curargv);

	kvRedisCommandToShm(mem, cmd,totlen);

	return kvRedisCommandToShm(mem, cmd, totlen);;

format_err:
	error_type = -2;
	goto cleanup;

memory_err:
	error_type = -1;
	goto cleanup;

cleanup:
	if (curargv) {
		while (argc--)
			sdsfree(curargv[argc]);
		free(curargv);
	}

	sdsfree(curarg);

	/* No need to check cmd since it is the last statement that can fail,
	 * but do it anyway to be as defensive as possible. */
	if (cmd != NULL)
		free(cmd);

	va_end(ap);
	return error_type;
}

char* kvRedisGetCommand(kv_mem* mem)
{
	kvLock(mem, __func__);
	redisShms* shms = (redisShms*)kvGetPtr(mem, SHM_INFOS_NAME);
	char* cmd = NULL;
	if (shms->svrReadIndex != shms->clientSendIndex)
	{
		cmd = shms->info[shms->svrReadIndex].cmd;

		shms->svrReadIndex++;
		if (shms->svrReadIndex >= SHM_CMD_TOTAL)
			shms->svrReadIndex = 0;
		
	}

	kvUnlock(mem);
	return cmd;
}

int kvGetVariableSize(kv_mem* mem, const char* keyName)
{
	kvDataInfo* pVDataInfo = kvGetDataInfoPtr(mem, keyName);
	if (pVDataInfo)
	{
		return pVDataInfo->size;
	}
	return -1;
}