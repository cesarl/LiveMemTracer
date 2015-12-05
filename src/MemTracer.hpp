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

#include <vector>
#include <algorithm>
#include <mutex>
#endif

namespace LiveMemTracer
{
	static const size_t ALLOC_NUMBER_PER_CHUNK = LMT_ALLOC_NUMBER_PER_CHUNK;
	static const size_t STACK_SIZE_PER_ALLOC = LMT_STACK_SIZE_PER_ALLOC;
	static const size_t CHUNK_NUMBER = LMT_CHUNK_NUMBER_PER_THREAD;
	static const size_t CACHE_SIZE = LMT_CACHE_SIZE;

	typedef size_t Hash;

	enum ChunkStatus
	{
		TREATED = 0,
		PENDING
	};

	struct Chunk
	{
		int64_t       allocSize[ALLOC_NUMBER_PER_CHUNK];
		Hash          allocHash[ALLOC_NUMBER_PER_CHUNK];
		void         *allocStack[ALLOC_NUMBER_PER_CHUNK];
		uint8_t       allocStackSize[ALLOC_NUMBER_PER_CHUNK];
		void         *stackBuffer[ALLOC_NUMBER_PER_CHUNK * STACK_SIZE_PER_ALLOC];
		size_t        allocIndex;
		size_t        stackIndex;
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

	struct Alloc;

	static void treatChunk(Chunk *chunk);
	static void updateTree(Alloc &alloc, int64_t size, bool checkTree);

	static const size_t HEADER_SIZE = sizeof(Header);

#ifdef LMT_IMPL
	struct Alloc
	{
		Hash hash;
		int64_t counter;
		const char *stackStr[STACK_SIZE_PER_ALLOC];
		uint8_t stackSize;
	};

	template <typename Key, typename Value, size_t Capacity>
	class Dictionary
	{
	public:
		static const size_t   HASH_EMPTY = 0;
		static const size_t   HASH_INVALID = Hash(-1);

		Dictionary()
		{
		}

		struct Pair
		{
		private:
			Hash  hash = HASH_EMPTY;
			Key   key;
			Value value;
		public:
			inline Value &getValue() { return value; }
			inline Hash  &getHash() { return hash; }
			friend class Dictionary;
		};

		Pair *update(const Key &name)
		{
			const size_t hash = getHash(name);
			assert(hash != HASH_INVALID);

			Pair *pair = &_buffer[hash];
			if (pair->hash == HASH_EMPTY)
			{
				pair->hash = hash;
				pair->key = name;
			}
			return pair;
		}
	private:
		Pair _buffer[Capacity];
		static const size_t HASH_MODIFIER = 7;

		inline size_t getHash(Key key)
		{
			size_t hash = key;
			for (size_t i = 0; i < Capacity; ++i)
			{
				const size_t realHash = (hash + i * HASH_MODIFIER * HASH_MODIFIER) % Capacity;
				if (_buffer[realHash].hash == HASH_EMPTY || _buffer[realHash].hash == realHash)
					return realHash;
			}
			assert(false);
			return HASH_INVALID;
		}
	};

	struct Edge
	{
		int64_t count = 0;
		const char *name = nullptr;
		std::vector<Hash> to;
	};

	__declspec(thread) static Chunk                     g_th_chunks[CHUNK_NUMBER];
	__declspec(thread) static uint8_t                   g_th_chunkIndex = 0;
	__declspec(thread) static Hash                      g_th_cache[CACHE_SIZE];
	__declspec(thread) static uint8_t                   g_th_cacheIndex = 0;
	__declspec(thread) static bool                      g_th_initialized = false;

	static Dictionary<size_t, const char*, 1024 * 16>   g_hashStackRefTable;
	static Dictionary<Hash, Alloc, 1024 * 16>           g_allocIndexRefTable;

	static Dictionary<size_t, Edge, 1024 * 16>          g_tree;
	static std::mutex                                   g_mutex;

	static inline Chunk *getChunck();
	static inline bool chunkIsFull(const Chunk *chunk);
	static inline Chunk *getNextChunk();
	static inline uint8_t findInCache(Hash hash);
	static inline void logAllocInChunk(Chunk *chunk, Header *header, size_t size);
	static inline void logFreeInChunk(Chunk *chunk, Header *header);
}
#endif

#ifdef LMT_IMPL
void *LiveMemTracer::alloc(size_t size)
{
	Chunk *chunk = getChunck();
	if (chunkIsFull(chunk))
	{
		chunk = getNextChunk();
	}
	void *ptr = malloc(size + HEADER_SIZE);
	Header *header = (Header*)(ptr);
	logAllocInChunk(chunk, header, size);
	return (void*)(size_t(ptr) + HEADER_SIZE);
}

void *LiveMemTracer::allocAligned(size_t size, size_t alignment)
{
	Chunk *chunk = getChunck();
	if (chunkIsFull(chunk))
	{
		chunk = getNextChunk();
	}
	void *ptr = _aligned_offset_malloc(size + HEADER_SIZE, alignment, HEADER_SIZE);
	Header *header = (Header*)(ptr);
	logAllocInChunk(chunk, header, size);
	return (void*)(size_t(ptr) + HEADER_SIZE);
}

void LiveMemTracer::dealloc(void *ptr)
{
	Chunk *chunk = getChunck();
	if (chunkIsFull(chunk))
	{
		chunk = getNextChunk();
	}
	void* offset = (void*)(size_t(ptr) - HEADER_SIZE);
	Header *header = (Header*)(offset);
	logFreeInChunk(chunk, header);
	free(offset);
}

void LiveMemTracer::deallocAligned(void *ptr)
{
	Chunk *chunk = getChunck();
	if (chunkIsFull(chunk))
	{
		chunk = getNextChunk();
	}
	void* offset = (void*)(size_t(ptr) - HEADER_SIZE);
	Header *header = (Header*)(offset);
	logFreeInChunk(chunk, header);
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

void LiveMemTracer::updateTree(Alloc &alloc, int64_t size, bool checkTree)
{
	int stackSize = alloc.stackSize;
	stackSize -= 2;
	uint8_t depth = 0;

	Dictionary<size_t, Edge, 1024 * 16>::Pair *previousEdge = nullptr;
	while (stackSize >= 0)
	{
		auto *current = g_tree.update(size_t(alloc.stackStr[stackSize]) + depth * depth);
		current->getValue().count += size;
		if (checkTree && previousEdge != nullptr)
		{
			current->getValue().name = alloc.stackStr[stackSize];
			auto &prev = previousEdge->getValue();
			if (prev.to.empty()
				|| std::lower_bound(std::begin(prev.to), std::end(prev.to), current->getHash()) == std::end(prev.to))
			{
				prev.to.insert(std::upper_bound(std::begin(prev.to), std::end(prev.to), current->getHash()), current->getHash());
			}
		}

		previousEdge = current;
		++depth;
		--stackSize;
	}
}

void LiveMemTracer::treatChunk(Chunk *chunk)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	for (size_t i = 0, iend = chunk->allocIndex; i < iend; ++i)
	{
		auto it = g_allocIndexRefTable.update(chunk->allocHash[i]);
		auto &alloc = it->getValue();
		if (alloc.stackSize != 0)
		{
			alloc.counter += chunk->allocSize[i];
			updateTree(alloc, chunk->allocSize[i], false);
			continue;
		}
		alloc.counter = chunk->allocSize[i];
		alloc.hash = chunk->allocHash[i];

		for (size_t j = 0, jend = chunk->allocStackSize[i]; j < jend; ++j)
		{
			void *addr = (&chunk->allocStack[i])[j];
			auto found = g_hashStackRefTable.update(size_t(addr));
			if (found->getValue() != nullptr)
			{
				alloc.stackStr[j] = found->getValue();
				continue;
			}
			alloc.stackStr[j] = SymbolGetter::getSymbol(addr);
			found->getValue() = alloc.stackStr[j];
		}
		alloc.stackSize = chunk->allocStackSize[i];
		updateTree(alloc, chunk->allocSize[i], true);
	}

	chunk->treated.store(ChunkStatus::TREATED);
}


//////////////////////////////////////////////////////////////////////////

LiveMemTracer::Chunk *LiveMemTracer::getChunck()
{
	if (!g_th_initialized)
	{
		memset(g_th_chunks, 0, sizeof(g_th_chunks));
		memset(g_th_cache, 0, sizeof(g_th_cache));
		g_th_initialized = true;
	}
	return &g_th_chunks[g_th_chunkIndex];
}

bool LiveMemTracer::chunkIsFull(const LiveMemTracer::Chunk *chunk)
{
	if (chunk->allocIndex >= ALLOC_NUMBER_PER_CHUNK
		|| chunk->stackIndex >= ALLOC_NUMBER_PER_CHUNK * STACK_SIZE_PER_ALLOC)
		return true;
	return false;
}

LiveMemTracer::Chunk *LiveMemTracer::getNextChunk()
{
	Chunk *currentChunk = getChunck();
	if (chunkIsFull(currentChunk) == false)
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
	Chunk *chunk = getChunck();

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

uint8_t LiveMemTracer::findInCache(LiveMemTracer::Hash hash)
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


void LiveMemTracer::logAllocInChunk(LiveMemTracer::Chunk *chunk, LiveMemTracer::Header *header, size_t size)
{
	Hash hash;
	void **stack = &chunk->stackBuffer[chunk->stackIndex];
	uint32_t count = CaptureStackBackTrace(3, STACK_SIZE_PER_ALLOC, stack, (PDWORD)&hash);

	if (count == STACK_SIZE_PER_ALLOC)
	{
		void* tmpStack[500];
		uint32_t tmpSize = CaptureStackBackTrace(3, 500, tmpStack, (PDWORD)&hash);

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
	chunk->allocStackSize[index] = count - 3;
	g_th_cache[g_th_cacheIndex] = hash;
	g_th_cacheIndex = (g_th_cacheIndex + 1) % CACHE_SIZE;
	chunk->allocIndex += 1;
	chunk->stackIndex += count - 3;

	header->size = size;
	header->hash = hash;
}

void LiveMemTracer::logFreeInChunk(LiveMemTracer::Chunk *chunk, LiveMemTracer::Header *header)
{
	size_t index = chunk->allocIndex;
	uint8_t found = findInCache(header->hash);
	if (found != uint8_t(-1))
	{
		index = chunk->allocIndex - 1 - found;
		chunk->allocSize[index] -= header->size;
		return;
	}

	g_th_cache[g_th_cacheIndex] = header->hash;
	g_th_cacheIndex = (g_th_cacheIndex + 1) % CACHE_SIZE;
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