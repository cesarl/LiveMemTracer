#pragma once

#include <cstdint>    //uint32_t etc...
#include <cstdlib>    //malloc etc...
#include <string>     //memset
#include <Windows.h>  //CaptureStackBackTrace
#include <atomic>     //std::atomic

namespace LiveMemTracer
{
	static const size_t ALLOC_NUMBER_PER_CHUNK = 1024 * 16;
	static const size_t STACK_SIZE_PER_ALLOC = 50;
	static const size_t CHUNK_NUMBER = 16;
	static const size_t CACHE_SIZE = 16;

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

	static const size_t HEADER_SIZE = sizeof(Header);

	__declspec(thread) static Chunk                     g_th_chunks[CHUNK_NUMBER];
	__declspec(thread) static uint8_t                   g_th_chunkIndex = 0;
	__declspec(thread) static Hash                      g_th_cache[CACHE_SIZE];
	__declspec(thread) static uint8_t                   g_th_cacheIndex = 0;
	__declspec(thread) static bool                      g_th_initialized = false;

	static Chunk *getChunck()
	{
		if (!g_th_initialized)
		{
			memset(g_th_chunks, 0, sizeof(g_th_chunks));
			memset(g_th_cache, 0, sizeof(g_th_cache));
			g_th_initialized = true;
		}
		return &g_th_chunks[g_th_chunkIndex];
	}

	static bool chunkIsFull(const Chunk *chunk)
	{
		if (chunk->allocIndex >= ALLOC_NUMBER_PER_CHUNK
			|| chunk->stackIndex >= ALLOC_NUMBER_PER_CHUNK * STACK_SIZE_PER_ALLOC)
			return true;
		return false;
	}

	static Chunk *getNextChunk()
	{
		Chunk *currentChunk = getChunck();
		if (chunkIsFull(currentChunk) == false)
		{
			assert(false && "Why did you called me if current chunk is not full ?");
		}
		assert(currentChunk->treated.load() == ChunkStatus::TREATED && "Why did you called me if current chunk is already pending ?");

		g_th_chunkIndex = (g_th_chunkIndex + 1) % CHUNK_NUMBER;
		Chunk *chunk = getChunck();

		while (chunk->treated.load() == ChunkStatus::PENDING)
		{
			//TODO wait or treat it
		}

		g_th_cacheIndex = 0;
		memset(g_th_cache, 0, sizeof(g_th_cache));
		chunk->allocIndex = 0;
		chunk->stackIndex = 0;
		return chunk;
	}

	static uint8_t findInCache(Hash hash)
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


	void logAllocInChunk(Chunk *chunk, Header *header, size_t size)
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
		chunk->allocStackSize[index] = count;
		g_th_cache[g_th_cacheIndex++] = hash;
		chunk->allocIndex += 1;
		chunk->stackIndex += count;

		header->size = size;
		header->hash = hash;
	}

	void logFreeInChunk(Chunk *chunk, Header *header)
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
		chunk->allocSize[index] = - header->size;
		chunk->allocHash[index] = header->hash;
		chunk->allocStackSize[index] = 0;
		chunk->allocIndex += 1;
	}

	void *alloc(size_t size)
	{
		Chunk *chunk = getChunck();
		if (chunkIsFull(chunk))
		{
			chunk = getNextChunk();
			if (!chunk)
			{
				// TODO WAIT
			}
		}
		void *ptr = malloc(size + HEADER_SIZE);
		Header *header = (Header*)(ptr);
		logAllocInChunk(chunk, header, size);
		return (void*)(size_t(ptr) + HEADER_SIZE);
	}

	void *allocAligned(size_t size, size_t alignment)
	{
		Chunk *chunk = getChunck();
		if (chunkIsFull(chunk))
		{
			chunk = getNextChunk();
			if (!chunk)
			{
				// TODO WAIT
			}
		}
		void *ptr = _aligned_offset_malloc(size + HEADER_SIZE, alignment, HEADER_SIZE);
		Header *header = (Header*)(ptr);
		logAllocInChunk(chunk, header, size);
		return (void*)(size_t(ptr) + HEADER_SIZE);
	}

	void dealloc(void *ptr)
	{
		Chunk *chunk = getChunck();
		if (chunkIsFull(chunk))
		{
			chunk = getNextChunk();
			if (!chunk)
			{
				// TODO WAIT
			}
		}
		void* offset = (void*)(size_t(ptr) - HEADER_SIZE);
		Header *header = (Header*)(offset);
		logFreeInChunk(chunk, header);
		free(offset);
	}

	void deallocAligned(void *ptr)
	{
		Chunk *chunk = getChunck();
		if (chunkIsFull(chunk))
		{
			chunk = getNextChunk();
			if (!chunk)
			{
				// TODO WAIT
			}
		}
		void* offset = (void*)(size_t(ptr) - HEADER_SIZE);
		Header *header = (Header*)(offset);
		logFreeInChunk(chunk, header);
		_aligned_free(offset);
	}
}