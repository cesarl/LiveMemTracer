#pragma once

#ifdef LMT_IMPL
#include <atomic>     //std::atomic
#include <cstdlib>    //malloc etc...
#include <Windows.h>  //CaptureStackBackTrace

#include <vector>
#include <algorithm>
#include <mutex>
#endif

#ifndef LMT_ENABLED
#define LMT_ALLOC(size)malloc(size)
#define LMT_ALLOC_ALIGNED(size, alignment)malloc(size)
#define LMT_DEALLOC(ptr)free(ptr)
#define LMT_DEALLOC_ALIGNED(ptr, alignment)free(ptr)
#define LMT_REALLOC(ptr, size)realloc(ptr, size)
#define LMT_DISPLAY(dt)do{}while(0)
#define LMT_EXIT()do{}while(0)

#else // LMT_ENABLED

#define LMT_ALLOC(size)LiveMemTracer::alloc(size)
#define LMT_ALLOC_ALIGNED(size, alignment)LiveMemTracer::allocAligned(size, alignment)
#define LMT_DEALLOC(ptr)LiveMemTracer::dealloc(ptr)
#define LMT_DEALLOC_ALIGNED(ptr, alignment)LiveMemTracer::deallocAligned(ptr)
#define LMT_REALLOC(ptr, size)LiveMemTracer::realloc(ptr, size)
#define LMT_DISPLAY(dt)LiveMemTracer::display(dt)
#define LMT_EXIT() LiveMemTracer::exit()

namespace LiveMemTracer
{

	inline void *alloc(size_t size);
	inline void *allocAligned(size_t size, size_t alignment);
	inline void dealloc(void *ptr);
	inline void deallocAligned(void *ptr);
	inline void *realloc(void *ptr, size_t size);
	void exit();
	void display(float dt);

#ifdef LMT_IMPL

#ifndef LMT_ALLOC_NUMBER_PER_CHUNK
#define LMT_ALLOC_NUMBER_PER_CHUNK 1024 * 8
#endif

#ifndef LMT_STACK_SIZE_PER_ALLOC
#define LMT_STACK_SIZE_PER_ALLOC 50
#endif

#ifndef LMT_CHUNK_NUMBER_PER_THREAD
#define LMT_CHUNK_NUMBER_PER_THREAD 8
#endif

#ifndef LMT_CACHE_SIZE
#define LMT_CACHE_SIZE 16
#endif

#ifndef LMT_FRAME_TO_SKIP
#define LMT_FRAME_TO_SKIP 3
#endif

#ifndef LMT_ALLOC_DICTIONARY_SIZE
#define LMT_ALLOC_DICTIONARY_SIZE 1024 * 16
#endif

#ifndef LMT_STACK_DICTIONARY_SIZE
#define LMT_STACK_DICTIONARY_SIZE 1024 * 16
#endif

#ifndef LMT_TREE_DICTIONARY_SIZE
#define LMT_TREE_DICTIONARY_SIZE 1024 * 16 * 16
#endif

#ifndef LMT_ASSERT
#define LMT_ASSERT(condition, message) assert(condition)
#endif

#ifndef LMT_TREAT_CHUNK
	static_assert(false, "You have to define LMT_TREAT_CHUNK(chunk)");
#endif

#ifndef LMT_IMPLEMENTED
#define LMT_IMPLEMENTED 1
#else
	static_assert(false, "LMT is already implemented, do not define LMT_IMPLEMENTED more than once.");
#endif

	static const size_t ALLOC_NUMBER_PER_CHUNK = LMT_ALLOC_NUMBER_PER_CHUNK;
	static const size_t STACK_SIZE_PER_ALLOC = LMT_STACK_SIZE_PER_ALLOC;
	static const size_t CHUNK_NUMBER = LMT_CHUNK_NUMBER_PER_THREAD;
	static const size_t CACHE_SIZE = LMT_CACHE_SIZE;
	static const size_t ALLOC_DICTIONARY_SIZE = LMT_ALLOC_DICTIONARY_SIZE;
	static const size_t STACK_DICTIONARY_SIZE = LMT_STACK_DICTIONARY_SIZE;
	static const size_t TREE_DICTIONARY_SIZE = LMT_TREE_DICTIONARY_SIZE;

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

	struct AllocStack;

	static void treatChunk(Chunk *chunk);
	static void updateTree(AllocStack &alloc, int64_t size, bool checkTree);

	static const size_t HEADER_SIZE = sizeof(Header);

	struct Alloc
	{
		int64_t counter;
		const char *str;
		Alloc *next;
		Alloc *shared;
		Alloc() : counter(0), str(nullptr), next(nullptr), shared(nullptr) {}
	};

	struct AllocStack
	{
		Hash hash;
		int64_t counter;
		Alloc *stackAllocs[STACK_SIZE_PER_ALLOC];
		uint8_t stackSize;
		AllocStack() : hash(0), counter(0), stackSize(0) {}
	};

	template <typename Key, typename Value, size_t Capacity>
	class Dictionary
	{
	public:
		static const size_t   HASH_EMPTY = Hash(-1);
		static const size_t   HASH_INVALID = Hash(-2);

		Dictionary()
		{
		}

		struct Pair
		{
		private:
			Hash  hash;
			Key   key;
			Value value;
			Pair() : hash(HASH_EMPTY) {}
		public:
			inline Value &getValue() { return value; }
			friend class Dictionary;
		};

		Pair *update(const Key &key)
		{
			const size_t hash = getHash(key);
			LMT_ASSERT(hash != HASH_INVALID, "This slot is already used.");

			Pair *pair = &_buffer[hash];
			if (pair->hash == HASH_EMPTY)
			{
				pair->hash = hash;
				pair->key = key;
				++_size;
			}
			return pair;
		}
	private:
		Pair _buffer[Capacity];
		size_t _size;
		static const size_t HASH_MODIFIER = 7;

		inline size_t getHash(Key key) const
		{
			for (size_t i = 0; i < Capacity; ++i)
			{
				const size_t realHash = (key * HASH_MODIFIER + i) % Capacity;
				if (_buffer[realHash].hash == HASH_EMPTY || _buffer[realHash].key == key)
					return realHash;
			}
			LMT_ASSERT(false, "Dictionary is full or quadratic probing reach it's limits.");
			return HASH_INVALID;
		}
	};

	struct Edge
	{
		int64_t count;
		const char *name;
		std::vector<Edge*> to;
		Edge() : count(0), name(nullptr) {}
	};

	__declspec(thread) static Chunk                     g_th_chunks[CHUNK_NUMBER];
	__declspec(thread) static uint8_t                   g_th_chunkIndex = 0;
	__declspec(thread) static Hash                      g_th_cache[CACHE_SIZE];
	__declspec(thread) static uint8_t                   g_th_cacheIndex = 0;
	__declspec(thread) static bool                      g_th_initialized = false;

	static Dictionary<Hash, AllocStack, STACK_DICTIONARY_SIZE>  g_allocStackRefTable;
	static Dictionary<Hash, Alloc, ALLOC_DICTIONARY_SIZE>       g_allocRefTable;
	static Alloc                                               *g_allocList = nullptr;
	static bool                                                 g_isRunning = true;

	static Dictionary<size_t, Edge, TREE_DICTIONARY_SIZE>       g_tree;

	static std::vector<Edge*>                                   g_allocStackRoots;
	static std::mutex                                           g_mutex;

	static const size_t                                         g_internalPerThreadMemoryUsed =
		sizeof(g_th_chunks)
		+ sizeof(g_th_chunkIndex)
		+ sizeof(g_th_cache)
		+ sizeof(g_th_cacheIndex)
		+ sizeof(g_th_initialized);

	static const size_t                                         g_internalSharedMemoryUsed =
		sizeof(g_allocStackRefTable)
		+ sizeof(g_allocRefTable)
		+ sizeof(g_allocList)
		+ sizeof(g_tree)
		+ sizeof(g_allocStackRoots)
		+ sizeof(g_mutex)
		+ sizeof(g_internalPerThreadMemoryUsed)
		+ sizeof(size_t) /* itself */;

	static std::atomic_size_t                                   g_internalAllThreadsMemoryUsed = g_internalSharedMemoryUsed;

	static inline Chunk *getChunk();
	static inline bool chunkIsFull(const Chunk *chunk);
	static inline Chunk *getNextChunk();
	static inline uint8_t findInCache(Hash hash);
	static inline void logAllocInChunk(Chunk *chunk, Header *header, size_t size);
	static inline void logFreeInChunk(Chunk *chunk, Header *header);
#endif
}

#ifdef LMT_IMPL
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

void *LiveMemTracer::realloc(void *ptr, size_t size)
{
	if (ptr == nullptr)
	{
		return alloc(size);
	}
	if (size == 0)
	{
		dealloc(ptr);
		return nullptr;
	}

	void* offset = (void*)(size_t(ptr) - HEADER_SIZE);
	Header *header = (Header*)(offset);

	if (size == header->size)
	{
		return ptr;
	}

	Chunk *chunk = getChunk();
	if (chunkIsFull(chunk))
	{
		chunk = getNextChunk();
	}

	logFreeInChunk(chunk, header);

	chunk = getChunk();
	if (chunkIsFull(chunk))
	{
		chunk = getNextChunk();
	}

	void *newPtr = ::realloc((void*)header, size + HEADER_SIZE);
	header = (Header*)(newPtr);
	logAllocInChunk(chunk, header, size);
	return (void*)(size_t(newPtr) + HEADER_SIZE);
}

void LiveMemTracer::dealloc(void *ptr)
{
	if (ptr == nullptr)
		return;
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
	if (ptr == nullptr)
		return;
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

void LiveMemTracer::exit()
{
	g_isRunning = false;
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
				LMT_ASSERT(false, "SymInitialize failed.");
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

template <class T>
inline size_t combineHash(const T& val, size_t baseHash = 14695981039346656037ULL)
{
	const uint8_t* bytes = (uint8_t*)&val;
	const size_t count = sizeof(val);
	const size_t FNV_offset_basis = baseHash;
	const size_t FNV_prime = 1099511628211ULL;

	size_t hash = FNV_offset_basis;
	for (size_t i = 0; i < count; ++i)
	{
		hash ^= (size_t)bytes[i];
		hash *= FNV_prime;
	}

	return hash;
}

void LiveMemTracer::updateTree(AllocStack &allocStack, int64_t size, bool checkTree)
{
	int stackSize = allocStack.stackSize;
	stackSize -= 2;
	uint8_t depth = 0;
	Edge *previousPtr = nullptr;
	while (stackSize >= 0)
	{
		Hash currentHash = size_t(allocStack.stackAllocs[stackSize]);
		currentHash = combineHash((size_t(previousPtr)), currentHash);
		currentHash = combineHash(depth * depth, currentHash);

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
					previousPtr->to.push_back(currentPtr);
				}
			}
			else
			{
				auto it = find(std::begin(g_allocStackRoots), std::end(g_allocStackRoots), currentPtr);
				if (it == std::end(g_allocStackRoots))
					g_allocStackRoots.push_back(currentPtr);
			}
		}
		if (strcmp(currentPtr->name, allocStack.stackAllocs[stackSize]->str) != 0)
		{
			int toto = 12;
		}
		if (previousPtr && previousPtr->count >= 0 && previousPtr->count < currentPtr->count)
		{
			int fuckMe = 123;
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
		g_internalAllThreadsMemoryUsed.fetch_add(g_internalPerThreadMemoryUsed);
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
		LMT_ASSERT(false, "Why did you called me if current chunk is not full ?");
	}
	if (currentChunk->treated.load() == ChunkStatus::PENDING)
	{
		// That's a recursive call, so we go to the next chunk
	}
	else if (g_isRunning)
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

static bool edgeSortFunction(LiveMemTracer::Edge *a, LiveMemTracer::Edge *b) { return (a->count > b->count); }

static void recursiveImguiTreeDisplay(LiveMemTracer::Edge *edge, bool updateData, bool filter)
{
	if (filter == false || edge->count)
	{
		int64_t count = edge->count;
		if (count < 10 * 1024)
			ImGui::Text("%06f b", float(count));
		else if (count <= 10 * 1024 * 1024)
			ImGui::Text("%06f K", float(count) / 1024.f);
		else
		{
			ImGui::Text("%06f Mo", float(count) / 1024.f / 1024.f);
		}
		ImGui::SameLine();
		if (ImGui::TreeNode((void*)edge, edge->name))
		{
			if (updateData)
				std::sort(std::begin(edge->to), std::end(edge->to), &edgeSortFunction);
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
		ImGui::Checkbox("Filter empty allocs", &filterEmptyAlloc); ImGui::SameLine();
		ImGui::Text("Memory used by LMT : %06f Mo", float(g_internalAllThreadsMemoryUsed.load()) / 1024.f / 1024.f);
		ImGui::Separator();

		std::lock_guard<std::mutex> lock(g_mutex);
		if (updateData)
			std::sort(std::begin(g_allocStackRoots), std::end(g_allocStackRoots), edgeSortFunction);
		Alloc *allocList = g_allocList;
		ImGui::Columns(2);
		ImGui::Text("Size");
		ImGui::NextColumn();
		ImGui::Text("Stack");
		ImGui::NextColumn();
		ImGui::Separator();
		ImGui::Columns(1);
		for (auto &e : g_allocStackRoots)
		{
			recursiveImguiTreeDisplay(e, updateData, filterEmptyAlloc);
		}

	}
	ImGui::End();
}
#else
void LiveMemTracer::display(float dt) {}
#endif

#endif // LMT_IMPL
#endif // LMT_ENABLED