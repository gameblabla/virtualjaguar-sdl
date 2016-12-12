//
// Memory handler
//
// by David Raingeard (Cal2)
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Cleanups by James L. Hammons
//

#include <malloc.h>
#include <stdlib.h>
#include "log.h"
#include "memory.h"

// Useful structs (for doubly linked list in this case)

typedef struct sMemBlockInfo
{
	void * ptr;
	char * info;
	UINT32 size;
	sMemBlockInfo * next;
	sMemBlockInfo * prev;
} sMemBlockInfo;

// Private global variables

static sMemBlockInfo memoryInfo;
//static UINT32 memoryMaxAllocated;
static UINT32 currentAllocatedMemory;
static UINT32 maximumAllocatedMemory;


void memory_addMemInfo(void * ptr, UINT32 size, char * info)
{
	sMemBlockInfo * alias = &memoryInfo;

	while (alias->next)
		alias = alias->next;

	alias->next = (sMemBlockInfo *)malloc(sizeof(sMemBlockInfo));

	if (alias->next == NULL)
	{
		exit(0);
		return;
	}

	alias->next->prev = alias;
	alias = alias->next;
	alias->next = NULL;
	alias->size = size;
	alias->ptr = ptr;
	alias->info = info;
}

void InitMemory(void)
{
	memoryInfo.next = memoryInfo.prev = NULL;
	currentAllocatedMemory = maximumAllocatedMemory = 0;
}

void MemoryDone(void)
{
}

void * memory_malloc(UINT32 size, char * info)
{
	void * ptr = (void *)malloc(size);

	if (ptr == NULL)
		return NULL;

	memory_addMemInfo(ptr, size, info);
	currentAllocatedMemory += size;

	if (currentAllocatedMemory > maximumAllocatedMemory)
		maximumAllocatedMemory = currentAllocatedMemory;

	return ptr;
}

void memory_malloc_secure(void ** new_ptr, UINT32 size, char * info)
{
	WriteLog("Memory: Allocating %i bytes of memory for <%s>...", size, (info == NULL ? "unknown" : info));

	void * ptr = malloc(size);

	if (ptr == NULL)
	{
		WriteLog("Failed!\n");
		log_done();
		exit(0);
	}

	memory_addMemInfo(ptr, size, info);
	currentAllocatedMemory += size;

	if (currentAllocatedMemory > maximumAllocatedMemory)
		maximumAllocatedMemory = currentAllocatedMemory;

	*new_ptr = ptr;
	WriteLog("OK\n");
}

void memory_free(void * ptr)
{
//	sMemBlockInfo * alias= &memoryInfo;
//	alias = alias->next;
	sMemBlockInfo * alias= memoryInfo.next;

	while (alias->ptr != ptr)
		alias = alias->next;

	WriteLog("Memory: Freeing %i bytes from <%s>...\n", (int)alias->size, alias->info);

	free(ptr);
	currentAllocatedMemory -= alias->size;
	alias->prev->next = alias->next;

	if (alias->next != NULL)
		alias->next->prev = alias->prev;

	free(alias);
}

void memory_memoryUsage(FILE * fp)
{
	/*UINT32 total = 0;

	fprintf(fp, "Memory usage:\n");

//	sMemBlockInfo * alias = &memoryInfo;
//	alias = alias->next;
	sMemBlockInfo * alias= memoryInfo.next;

	while (alias)
	{
		fprintf(fp, "\t%16i bytes: <%s> (@ %08X)\n", (int)alias->size, alias->info, (unsigned int)alias->ptr);
		total += alias->size;
		alias = alias->next;
	}

	fprintf(fp, "\n\t%16i bytes total(%i Mb)\n", (int)total, (int)(total >> 20));
	fprintf(fp, "\n\t%16i bytes memory peak(%i Mb)\n", (int)maximumAllocatedMemory, (int)(maximumAllocatedMemory >> 20));*/
}
