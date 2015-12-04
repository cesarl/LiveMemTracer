#pragma once

#ifndef LMT_ALLOC_NUMBER_PER_CHUNK
#define LMT_ALLOC_NUMBER_PER_CHUNK 1024 * 16
#endif

#ifndef LMT_STACK_SIZE_PER_ALLOC
#define LMT_STACK_SIZE_PER_ALLOC 50
#endif

#ifndef LMT_CHUNK_NUMBER_PER_THREAD
#define LMT_CHUNK_NUMBER_PER_THREAD 16
#endif

#ifndef LMT_CACHE_SIZE
#define LMT_CACHE_SIZE 16
#endif

#ifndef LMT_TREAT_CHUNK
static_assert(false, "You have to define LMT_TREAT_CHUNK(chunk)");
#endif

#include <cstdint>    //uint32_t etc...
#include <atomic>     //std::atomic

#ifdef LMT_IMPL
#include <cstdlib>    //malloc etc...
#include <string>     //memset
#include <Windows.h>  //CaptureStackBackTrace

#include <unordered_map>
#include <mutex>
#endif

namespace LiveMemTracer
{
	static const size_t ALLOC_NUMBER_PER_CHUNK = LMT_ALLOC_NUMBER_PER_CHUNK;
	static const size_t STACK_SIZE_PER_ALLOC = LMT_STACK_SIZE_PER_ALLOC;
	static const size_t CHUNK_NUMBER = LMT_CHUNK_NUMBER_PER_THREAD;
	static const size_t CACHE_SIZE = LMT_CACHE_SIZE;

	typedef uint32_t Hash;

	enum ChunkStatus
	{
		TREATED = 0,
		PENDING
	};

	struct Chunk
	{
		int32_t       allocSize[ALLOC_NUMBER_PER_CHUNK];
		Hash          allocHash[ALLOC_NUMBER_PER_CHUNK];
		void         *allocStack[ALLOC_NUMBER_PER_CHUNK];
		uint8_t       allocStackSize[ALLOC_NUMBER_PER_CHUNK];
		void         *stackBuffer[ALLOC_NUMBER_PER_CHUNK * STACK_SIZE_PER_ALLOC];
		uint32_t      allocIndex;
		uint32_t      stackIndex;
		std::atomic<ChunkStatus> treated;
	};

	struct Header
	{
		Hash    hash;
		int32_t size;
	};

	static inline void *alloc(size_t size);
	static inline void *allocAligned(size_t size, size_t alignment);
	static inline void dealloc(void *ptr);
	static inline void deallocAligned(void *ptr);

	static void treatChunk(Chunk *chunk);

	static const size_t HEADER_SIZE = sizeof(Header);

#ifdef LMT_IMPL
	namespace Private
	{
		struct Alloc
		{
			Hash hash;
			int32_t counter;
			const char *stackStr[STACK_SIZE_PER_ALLOC];
			uint32_t stackSize;
		};

		__declspec(thread) static Chunk                     g_th_chunks[CHUNK_NUMBER];
		__declspec(thread) static uint8_t                   g_th_chunkIndex = 0;
		__declspec(thread) static Hash                      g_th_cache[CACHE_SIZE];
		__declspec(thread) static uint8_t                   g_th_cacheIndex = 0;
		__declspec(thread) static bool                      g_th_initialized = false;

		static std::unordered_map<void*, const char *>      g_hashStackRefTable;
		static std::unordered_map<Hash, size_t>             g_allocIndexRefTable;
		static std::vector<Alloc>                           g_allocList;
		static std::mutex                                   g_mutex;

		static inline Chunk *getChunck();
		static inline bool chunkIsFull(const Chunk *chunk);
		static inline Chunk *getNextChunk();
		static inline uint8_t findInCache(Hash hash);
		static inline void logAllocInChunk(Chunk *chunk, Header *header, size_t size);
		static inline void logFreeInChunk(Chunk *chunk, Header *header);
	}
#endif
}

#ifdef LMT_IMPL
void *LiveMemTracer::alloc(size_t size)
{
	Chunk *chunk = Private::getChunck();
	if (Private::chunkIsFull(chunk))
	{
		chunk = Private::getNextChunk();
	}
	void *ptr = malloc(size + HEADER_SIZE);
	Header *header = (Header*)(ptr);
	Private::logAllocInChunk(chunk, header, size);
	return (void*)(size_t(ptr) + HEADER_SIZE);
}

void *LiveMemTracer::allocAligned(size_t size, size_t alignment)
{
	Chunk *chunk = Private::getChunck();
	if (Private::chunkIsFull(chunk))
	{
		chunk = Private::getNextChunk();
	}
	void *ptr = _aligned_offset_malloc(size + HEADER_SIZE, alignment, HEADER_SIZE);
	Header *header = (Header*)(ptr);
	Private::logAllocInChunk(chunk, header, size);
	return (void*)(size_t(ptr) + HEADER_SIZE);
}

void LiveMemTracer::dealloc(void *ptr)
{
	Chunk *chunk = Private::getChunck();
	if (Private::chunkIsFull(chunk))
	{
		chunk = Private::getNextChunk();
	}
	void* offset = (void*)(size_t(ptr) - HEADER_SIZE);
	Header *header = (Header*)(offset);
	Private::logFreeInChunk(chunk, header);
	free(offset);
}

void LiveMemTracer::deallocAligned(void *ptr)
{
	Chunk *chunk = Private::getChunck();
	if (Private::chunkIsFull(chunk))
	{
		chunk = Private::getNextChunk();
	}
	void* offset = (void*)(size_t(ptr) - HEADER_SIZE);
	Header *header = (Header*)(offset);
	Private::logFreeInChunk(chunk, header);
	_aligned_free(offset);
}

//////////////////////////////////////////////////////////////////////////

#include "imagehlp.h"
#pragma comment(lib, "imagehlp.lib")

namespace SymbolGetter
{
	static int _initialized = 0;
	static inline void init()
	{
		if (_initialized == false)
		{
			HANDLE hProcess;

			SymSetOptions(SYMOPT_UNDNAME);

			hProcess = GetCurrentProcess();
			if (!SymInitialize(hProcess, NULL, TRUE))
			{
				assert(false);
			}
			_initialized = true;
		}
	}
	static const char *getSymbol(void *ptr)
	{
		init();
		
		DWORD64 dwDisplacement = 0;
		DWORD64 dwAddress = DWORD64(ptr);
		HANDLE hProcess = GetCurrentProcess();
		
		char pSymbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
		PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)pSymbolBuffer;
		
		pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		pSymbol->MaxNameLen = MAX_SYM_NAME;
		
		if (!SymFromAddr(hProcess, dwAddress, &dwDisplacement, pSymbol))
		{
			return NULL;
		}
		return _strdup(pSymbol->Name);
	}
};

void LiveMemTracer::treatChunk(Chunk *chunk)
{
	for (size_t i = 0, iend = chunk->allocIndex; i < iend; ++i)
	{
		std::lock_guard<std::mutex> lock(Private::g_mutex);
		auto it = Private::g_allocIndexRefTable.find(chunk->allocHash[i]);
		if (it != std::end(Private::g_allocIndexRefTable))
		{
			Private::g_allocList[it->second].counter += chunk->allocSize[i];
			continue;
		}
		Private::Alloc alloc;
		alloc.counter = chunk->allocSize[i];
		alloc.hash = chunk->allocHash[i];

		for (size_t j = 0, jend = chunk->allocStackSize[i]; j < jend; ++j)
		{
			void *addr = (&chunk->allocStack[i])[j];
			auto found = Private::g_hashStackRefTable.find(addr);
			if (found != std::end(Private::g_hashStackRefTable))
			{
				alloc.stackStr[j] = found->second;
				continue;
			}
			alloc.stackStr[j] = SymbolGetter::getSymbol(addr);
			Private::g_hashStackRefTable.insert(std::make_pair(addr, alloc.stackStr[j]));
		}
		alloc.stackSize = chunk->allocStackSize[i];
		Private::g_allocIndexRefTable.insert(std::make_pair(alloc.hash, Private::g_allocList.size()));
		Private::g_allocList.push_back(alloc);
	}
	chunk->treated.store(ChunkStatus::TREATED);
}


//////////////////////////////////////////////////////////////////////////

LiveMemTracer::Chunk *LiveMemTracer::Private::getChunck()
{
	if (!g_th_initialized)
	{
		memset(g_th_chunks, 0, sizeof(g_th_chunks));
		memset(g_th_cache, 0, sizeof(g_th_cache));
		g_th_initialized = true;
	}
	return &g_th_chunks[g_th_chunkIndex];
}

bool LiveMemTracer::Private::chunkIsFull(const LiveMemTracer::Chunk *chunk)
{
	if (chunk->allocIndex >= ALLOC_NUMBER_PER_CHUNK
		|| chunk->stackIndex >= ALLOC_NUMBER_PER_CHUNK * STACK_SIZE_PER_ALLOC)
		return true;
	return false;
}

LiveMemTracer::Chunk *LiveMemTracer::Private::getNextChunk()
{
	Chunk *currentChunk = Private::getChunck();
	if (Private::chunkIsFull(currentChunk) == false)
	{
		assert(false && "Why did you called me if current chunk is not full ?");
	}
	if (currentChunk->treated.load() == ChunkStatus::PENDING)
	{
		// That's a recursive call, so we go to the next chunk
	}
	else
	{
		currentChunk->treated.store(ChunkStatus::PENDING);
		LMT_TREAT_CHUNK(currentChunk);
	}

	g_th_chunkIndex = (g_th_chunkIndex + 1) % CHUNK_NUMBER;
	Chunk *chunk = Private::getChunck();

	while (chunk->treated.load() == ChunkStatus::PENDING)
	{
#ifdef LMT_WAIT_IF_FULL
		LMT_WAIT_IF_FULL();
#else
		LiveMemTracer::treatChunk(currentChunk);
#endif
		//wait or treat it in current thread
	}

	g_th_cacheIndex = 0;
	memset(g_th_cache, 0, sizeof(g_th_cache));
	chunk->allocIndex = 0;
	chunk->stackIndex = 0;
	return chunk;
}

uint8_t LiveMemTracer::Private::findInCache(LiveMemTracer::Hash hash)
{
	for (uint8_t i = 0; i < CACHE_SIZE; ++i)
	{
		int index = int(g_th_chunkIndex) - i;
		if (index < 0)
		{
			index += CACHE_SIZE;
		}
		if (g_th_cache[index] == hash)
		{
			return uint8_t(index);
		}
	}
	return uint8_t(-1);
}


void LiveMemTracer::Private::logAllocInChunk(LiveMemTracer::Chunk *chunk, LiveMemTracer::Header *header, size_t size)
{
	Hash hash;
	void **stack = &chunk->stackBuffer[chunk->stackIndex];
	uint32_t count = CaptureStackBackTrace(4, STACK_SIZE_PER_ALLOC, stack, (PDWORD)&hash);

	if (count == STACK_SIZE_PER_ALLOC)
	{
		void* tmpStack[500];
		uint32_t tmpSize = CaptureStackBackTrace(4, 500, tmpStack, (PDWORD)&hash);

		for (uint32_t i = 0; i < STACK_SIZE_PER_ALLOC - 1; i++)
			stack[STACK_SIZE_PER_ALLOC - 1 - i] = tmpStack[tmpSize - 1 - i];
		stack[0] = (void*)~0;
	}

	size_t index = chunk->allocIndex;
	uint8_t found = findInCache(hash);
	if (found != uint8_t(-1))
	{
		index = chunk->allocIndex - 1 - found;
		chunk->allocSize[index] += size;
		return;
	}

	chunk->allocStack[index] = chunk->stackBuffer[chunk->stackIndex];
	chunk->allocSize[index] = size;
	chunk->allocHash[index] = hash;
	chunk->allocStackSize[index] = count;
	g_th_cache[g_th_cacheIndex++] = hash;
	chunk->allocIndex += 1;
	chunk->stackIndex += count;

	header->size = size;
	header->hash = hash;
}

void LiveMemTracer::Private::logFreeInChunk(LiveMemTracer::Chunk *chunk, LiveMemTracer::Header *header)
{
	size_t index = chunk->allocIndex;
	uint8_t found = findInCache(header->hash);
	if (found != uint8_t(-1))
	{
		index = chunk->allocIndex - 1 - found;
		chunk->allocSize[index] -= header->size;
		return;
	}

	g_th_cache[g_th_cacheIndex++] = header->hash;
	chunk->allocStack[index] = nullptr;
	chunk->allocSize[index] = -header->size;
	chunk->allocHash[index] = header->hash;
	chunk->allocStackSize[index] = 0;
	chunk->allocIndex += 1;
}

//#include <iostream>
//#include <fstream>
//
//void printStack()
//{
//	// Quote from Microsoft Documentation:
//	// ## Windows Server 2003 and Windows XP:  
//	// ## The sum of the FramesToSkip and FramesToCapture parameters must be less than 63.
//	const int kMaxCallers = 62;
//
//	void         * callers_stack[kMaxCallers];
//	unsigned short frames;
//	SYMBOL_INFO  * symbol;
//	HANDLE         process;
//	process = GetCurrentProcess();
//	SymInitialize(process, NULL, TRUE);
//	frames = CaptureStackBackTrace(0, kMaxCallers, callers_stack, NULL);
//	symbol = (SYMBOL_INFO *)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
//	symbol->MaxNameLen = 255;
//	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
//
//	//out << "(" << sample_address << "): " << std::endl;
//	const unsigned short  MAX_CALLERS_SHOWN = 6;
//	frames = frames < MAX_CALLERS_SHOWN ? frames : MAX_CALLERS_SHOWN;
//	for (unsigned int i = 0; i < frames; i++)
//	{
//		SymFromAddr(process, (DWORD64)(callers_stack[i]), 0, symbol);
//		printf("*** %d: %s 0x%0X\n", i, symbol->Name, symbol->Address);
//		//out << "*** " << i << ": " << callers_stack[i] << " " << symbol->Name << " - 0x" << symbol->Address << std::endl;
//	}
//
//	free(symbol);
//}

#endif