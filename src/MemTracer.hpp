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

#ifndef LMT_FRAME_TO_SKIP
#define LMT_FRAME_TO_SKIP 3
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
		size_t        allocStackIndex[ALLOC_NUMBER_PER_CHUNK];
		uint8_t       allocStackSize[ALLOC_NUMBER_PER_CHUNK];
		void         *stackBuffer[ALLOC_NUMBER_PER_CHUNK * STACK_SIZE_PER_ALLOC];
		size_t        allocIndex;
		size_t        stackIndex;
		std::atomic<ChunkStatus> treated;
	};

	struct Header
	{
		Hash    hash;
		size_t  size;
	};

	static inline void *alloc(size_t size);
	static inline void *allocAligned(size_t size, size_t alignment);
	static inline void dealloc(void *ptr);
	static inline void deallocAligned(void *ptr);

	struct AllocStack;

	static void treatChunk(Chunk *chunk);
	static void updateTree(AllocStack &alloc, int64_t size, bool checkTree);

	static const size_t HEADER_SIZE = sizeof(Header);

#ifdef LMT_IMPL
	struct Alloc
	{
		int64_t counter = 0;
		const char *str = nullptr;
		Alloc *next = nullptr;
		Alloc *shared = nullptr;
	};

	struct AllocStack
	{
		Hash hash = 0;
		int64_t counter = 0;
		Alloc *stackAllocs[STACK_SIZE_PER_ALLOC];
		uint8_t stackSize = 0;
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
			friend class Dictionary;
		};

		Pair *update(const Key &key)
		{
			const size_t hash = getHash(key);
			assert(hash != HASH_INVALID);

			Pair *pair = &_buffer[hash];
			if (pair->hash == HASH_EMPTY)
			{
				pair->hash = hash;
				pair->key = key;
			}
			return pair;
		}
	private:
		Pair _buffer[Capacity];
		static const size_t HASH_MODIFIER = 7;

		inline size_t getHash(Key key) const
		{
			for (size_t i = 0; i < Capacity; ++i)
			{
				const size_t realHash = (key + i * HASH_MODIFIER * HASH_MODIFIER) % Capacity;
				if (_buffer[realHash].hash == HASH_EMPTY || _buffer[realHash].key == key)
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
		std::vector<Edge*> to;
	};

	__declspec(thread) static Chunk                     g_th_chunks[CHUNK_NUMBER];
	__declspec(thread) static uint8_t                   g_th_chunkIndex = 0;
	__declspec(thread) static Hash                      g_th_cache[CACHE_SIZE];
	__declspec(thread) static uint8_t                   g_th_cacheIndex = 0;
	__declspec(thread) static bool                      g_th_initialized = false;

	static Dictionary<Hash, AllocStack, 1024 * 16>      g_allocStackRefTable;
	static Dictionary<Hash, Alloc, 1024 * 8>            g_allocRefTable;
	static Alloc                                       *g_allocList = nullptr;
	static std::vector<Edge*>                           g_allocStackRoots;

	static Dictionary<size_t, Edge, 1024 * 16>          g_tree;
	static std::mutex                                   g_mutex;

	static inline Chunk *getChunk();
	static inline bool chunkIsFull(const Chunk *chunk);
	static inline Chunk *getNextChunk();
	static inline uint8_t findInCache(Hash hash);
	static inline void logAllocInChunk(Chunk *chunk, Header *header, size_t size);
	static inline void logFreeInChunk(Chunk *chunk, Header *header);

#ifndef LMT_IMGUI
	static inline void display(float dt) {}
#else
	static inline void display(float dt);
#endif
}

void *LiveMemTracer::alloc(size_t size)
{
	Chunk *chunk = getChunk();
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
	Chunk *chunk = getChunk();
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
	Chunk *chunk = getChunk();
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
	Chunk *chunk = getChunk();
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

			SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);

			hProcess = GetCurrentProcess();
			if (!SymInitialize(hProcess, NULL, TRUE))
			{
				assert(false);
			}
			_initialized = true;
		}
	}

	static const char *g_truncated = "Truncated\0";

	static const char *getSymbol(void *ptr, void *& absoluteAddress)
	{
		init();

		DWORD64 dwDisplacement = 0;
		DWORD64 dwAddress = DWORD64(ptr);
		HANDLE hProcess = GetCurrentProcess();

		char pSymbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
		PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)pSymbolBuffer;

		pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		pSymbol->MaxNameLen = MAX_SYM_NAME;

		if (size_t(ptr) == size_t(-1) || !SymFromAddr(hProcess, dwAddress, &dwDisplacement, pSymbol))
		{
			return g_truncated;
		}
		absoluteAddress = (void*)(DWORD64(ptr) - dwDisplacement);
		return _strdup(pSymbol->Name);
	}
};

void LiveMemTracer::updateTree(AllocStack &allocStack, int64_t size, bool checkTree)
{
	int stackSize = allocStack.stackSize;
	stackSize -= 3;
	uint8_t depth = 0;

	Edge *previousPtr = nullptr;
	while (stackSize >= 0)
	{
		Hash currentHash = size_t(allocStack.stackAllocs[stackSize]) + depth * depth;
		Edge *currentPtr = &g_tree.update(currentHash)->getValue();
		currentPtr->count += size;
		if (checkTree)
		{
			currentPtr->name = allocStack.stackAllocs[stackSize]->str;
			if (previousPtr != nullptr)
			{
				auto it = std::find(std::begin(previousPtr->to), std::end(previousPtr->to), currentPtr);
				if (it == std::end(previousPtr->to))
				{
					previousPtr->to.insert(std::lower_bound(std::begin(previousPtr->to), std::end(previousPtr->to), currentPtr), currentPtr);
				}
			}
			else
			{
				auto it = find(std::begin(g_allocStackRoots), std::end(g_allocStackRoots), currentPtr);
				if (it == std::end(g_allocStackRoots))
					g_allocStackRoots.push_back(currentPtr);
			}
		}
		previousPtr = currentPtr;
		++depth;
		--stackSize;
	}
}

void LiveMemTracer::treatChunk(Chunk *chunk)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	for (size_t i = 0, iend = chunk->allocIndex; i < iend; ++i)
	{
		auto size = chunk->allocSize[i];
		if (size == 0)
			continue;
		auto it = g_allocStackRefTable.update(chunk->allocHash[i]);
		auto &allocStack = it->getValue();
		if (allocStack.stackSize != 0)
		{
			allocStack.counter += size;
			updateTree(allocStack, size, false);
			for (size_t j = 0; j < allocStack.stackSize; ++j)
			{
				allocStack.stackAllocs[j]->counter += size;
			}
			continue;
		}
		allocStack.counter = size;
		allocStack.hash = chunk->allocHash[i];

		for (size_t j = 0, jend = chunk->allocStackSize[i]; j < jend; ++j)
		{
			void *addr = chunk->stackBuffer[chunk->allocStackIndex[i] + j];
			auto found = g_allocRefTable.update(size_t(addr));
			if (found->getValue().shared != nullptr)
			{
				auto shared = found->getValue().shared;
				shared->counter += size;
				allocStack.stackAllocs[j] = shared;
				continue;
			}
			if (found->getValue().str != nullptr)
			{
				allocStack.stackAllocs[j] = &found->getValue();
				found->getValue().counter += size;
				continue;
			}
			void *absoluteAddress = nullptr;
			const char *name = SymbolGetter::getSymbol(addr, absoluteAddress);

			auto shared = g_allocRefTable.update(size_t(absoluteAddress));
			if (shared->getValue().str != nullptr)
			{
				found->getValue().shared = &shared->getValue();
				allocStack.stackAllocs[j] = &shared->getValue();
				shared->getValue().counter += size;
				if (name != SymbolGetter::g_truncated)
					free((void*)name);
				continue;
			}

			found->getValue().shared = &shared->getValue();
			shared->getValue().str = name;
			shared->getValue().counter = size;
			allocStack.stackAllocs[j] = &shared->getValue();
			shared->getValue().next = g_allocList;
			g_allocList = &shared->getValue();
		}
		allocStack.stackSize = chunk->allocStackSize[i];
		updateTree(allocStack, size, true);
	}

	chunk->treated.store(ChunkStatus::TREATED);
}


//////////////////////////////////////////////////////////////////////////

LiveMemTracer::Chunk *LiveMemTracer::getChunk()
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
	Chunk *currentChunk = getChunk();
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
	Chunk *chunk = getChunk();

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
	int i = int(g_th_cacheIndex - 1);
	if (i < 0)
		i = CACHE_SIZE - 1;
	uint8_t res = 1;
	while (true)
	{
		if (g_th_cache[i] == hash)
		{
			return res - 1;
		}
		i -= 1;
		if (i < 0)
			i = CACHE_SIZE - 1;
		if (++res == CACHE_SIZE)
			break;
	}
	return uint8_t(-1);
}


void LiveMemTracer::logAllocInChunk(LiveMemTracer::Chunk *chunk, LiveMemTracer::Header *header, size_t size)
{
	Hash hash;
	void **stack = &chunk->stackBuffer[chunk->stackIndex];
	uint32_t count = CaptureStackBackTrace(LMT_FRAME_TO_SKIP, STACK_SIZE_PER_ALLOC, stack, (PDWORD)&hash);

	if (count == STACK_SIZE_PER_ALLOC)
	{
		void* tmpStack[500];
		uint32_t tmpSize = CaptureStackBackTrace(LMT_FRAME_TO_SKIP, 500, tmpStack, (PDWORD)&hash);

		for (uint32_t i = 0; i < STACK_SIZE_PER_ALLOC - 1; i++)
			stack[STACK_SIZE_PER_ALLOC - 1 - i] = tmpStack[tmpSize - 1 - i];
		stack[0] = (void*)~0;
	}

	header->size = size;
	header->hash = hash;

	size_t index = chunk->allocIndex;
	uint8_t found = findInCache(hash);
	if (found != uint8_t(-1))
	{
		index = chunk->allocIndex - found - 1;
		assert(chunk->allocHash[index] == hash);
		chunk->allocSize[index] += size;
		return;
	}

	chunk->allocStackIndex[index] = chunk->stackIndex;
	chunk->allocSize[index] = size;
	chunk->allocHash[index] = hash;
	chunk->allocStackSize[index] = count;
	g_th_cache[g_th_cacheIndex] = hash;
	g_th_cacheIndex = (g_th_cacheIndex + 1) % CACHE_SIZE;
	chunk->allocIndex += 1;
	chunk->stackIndex += count;
}

void LiveMemTracer::logFreeInChunk(LiveMemTracer::Chunk *chunk, LiveMemTracer::Header *header)
{
	size_t index = chunk->allocIndex;
	uint8_t found = findInCache(header->hash);
	if (found != uint8_t(-1))
	{
		index = chunk->allocIndex - found - 1;
		assert(chunk->allocHash[index] == header->hash);
		chunk->allocSize[index] -= header->size;
		return;
	}

	g_th_cache[g_th_cacheIndex] = header->hash;
	g_th_cacheIndex = (g_th_cacheIndex + 1) % CACHE_SIZE;
	chunk->allocStackIndex[index] = -1;
	chunk->allocSize[index] = -int64_t(header->size);
	chunk->allocHash[index] = header->hash;
	chunk->allocStackSize[index] = 0;
	chunk->allocIndex += 1;
}

#ifdef LMT_IMGUI
#include LMT_IMGUI_INCLUDE_PATH

struct EdgeSortFunction {
	bool operator() (LiveMemTracer::Edge *a, LiveMemTracer::Edge *b) { return (a->count > b->count); }
} edgeSortFunction;

void recursiveImguiTreeDisplay(LiveMemTracer::Edge *edge, bool updateData, bool filter)
{
	if (filter == false || edge->count)
	{
		if (edge->count > 1024)
			ImGui::Text("%06i K", edge->count / 1024);
		else
			ImGui::Text("%06i b", edge->count);
		ImGui::SameLine();
		if (ImGui::TreeNode(edge->name))
		{
			if (updateData)
				std::sort(std::begin(edge->to), std::end(edge->to), edgeSortFunction);
			for (size_t i = 0; i < edge->to.size(); ++i)
			{
				recursiveImguiTreeDisplay(edge->to[i], updateData, filter);
			}
			ImGui::TreePop();
		}
	}
}

void LiveMemTracer::display(float dt)
{
	static bool displayTree = false;
	static bool filterEmptyAlloc = true;
	static float updateRatio = 0.f;

	bool updateData = false;
	updateRatio += dt;
	if (updateRatio >= 1.f)
	{
		updateData = true;
		updateRatio = 0.f;
	}

	if (ImGui::Begin("LiveMemoryProfiler"))
	{
		ImGui::Checkbox("DisplayTree", &displayTree); ImGui::SameLine();
		ImGui::Checkbox("Filter empty allocs", &filterEmptyAlloc);
		ImGui::Separator();
		if (displayTree == false)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			Alloc *allocList = g_allocList;
			while (allocList != nullptr)
			{
				ImGui::Columns(2);
				ImGui::Text("%i Ko", allocList->counter / 1024);
				ImGui::NextColumn();
				ImGui::Text("%s", allocList->str);
				allocList = allocList->next;
			}
		}
		else
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			if (updateData)
				std::sort(std::begin(g_allocStackRoots), std::end(g_allocStackRoots), edgeSortFunction);
			for (auto &e : g_allocStackRoots)
			{
				recursiveImguiTreeDisplay(e, updateData, filterEmptyAlloc);
			}
		}
	}
	ImGui::End();
}
#endif

#endif