#define LMT_ALLOC_NUMBER_PER_CHUNK 1024 * 16
#define LMT_STACK_SIZE_PER_ALLOC 50
#define LMT_CHUNK_NUMBER_PER_THREAD 16
#define LMT_CACHE_SIZE 16
//#define LMT_WAIT_IF_FULL 1

#define LMT_TREAT_CHUNK(chunk) LiveMemTracer::treatChunk(chunk);

#define LMT_IMPL 1

#include "../Src/MemTracer.hpp"

#include <vector>
#define OVERRIDE_NEW 1

#ifdef OVERRIDE_NEW
//////////////////////////////////////////////////////////////////////////
void* operator new(size_t count) throw(std::bad_alloc)
{
	return LiveMemTracer::alloc(count);
}

void* operator new(size_t count, const std::nothrow_t&) throw()
{
	return LiveMemTracer::alloc(count);
}

void* operator new(size_t count, size_t alignment) throw(std::bad_alloc)
{
	return LiveMemTracer::allocAligned(count, alignment);
}

void* operator new(size_t count, size_t alignment, const std::nothrow_t&) throw()
{
	return LiveMemTracer::allocAligned(count, alignment);
}

void* operator new[](size_t count) throw(std::bad_alloc)
{
	return LiveMemTracer::alloc(count);
}

void* operator new[](size_t count, const std::nothrow_t&) throw()
{
	return LiveMemTracer::alloc(count);
}

void* operator new[](size_t count, size_t alignment) throw(std::bad_alloc)
{
	return LiveMemTracer::allocAligned(count, alignment);
}

void* operator new[](size_t count, size_t alignment, const std::nothrow_t&) throw()
{
	return LiveMemTracer::allocAligned(count, alignment);
}

void operator delete(void* ptr) throw()
{
	return LiveMemTracer::dealloc(ptr);
}

void operator delete(void *ptr, const std::nothrow_t&) throw()
{
	return LiveMemTracer::dealloc(ptr);
}

void operator delete(void *ptr, size_t alignment) throw()
{
	return LiveMemTracer::deallocAligned(ptr);
}

void operator delete(void *ptr, size_t alignment, const std::nothrow_t&) throw()
{
	return LiveMemTracer::deallocAligned(ptr);
}

void operator delete[](void* ptr) throw()
{
	return LiveMemTracer::dealloc(ptr);
}

void operator delete[](void *ptr, const std::nothrow_t&) throw()
{
	return LiveMemTracer::dealloc(ptr);
}

void operator delete[](void *ptr, size_t alignment) throw()
{
	return LiveMemTracer::deallocAligned(ptr);
}

void operator delete[](void *ptr, size_t alignment, const std::nothrow_t&) throw()
{
	return LiveMemTracer::deallocAligned(ptr);
}
//////////////////////////////////////////////////////////////////////////
#endif

struct Bar
{
	Bar(){}
	char empty[8];
};

struct Foo
{
	size_t       index;
	std::string  indexStr;
	Bar*         ptr;
};

void fooBar(std::vector<Foo*> &vec, size_t size)
{
	if (size == 0)
		return;
	Foo *tmpFoo = new Foo;
	tmpFoo->index = size;
	tmpFoo->indexStr = std::to_string(size);
	tmpFoo->ptr = new Bar[size]();
	vec.push_back(tmpFoo);
	fooBar(vec, --size);
}

#include <chrono>
#include <iostream>

int main(int ac, char **av)
{
	for (int test = 0; test < 10; ++test)
	{

		auto start = std::chrono::high_resolution_clock::now();

		std::vector<Foo*> testVector;

		fooBar(testVector, 100);
		for (size_t i = 0; i < 100; ++i)
			fooBar(testVector, 1000);

		for (size_t i = 0; i < testVector.size(); ++i)
		{
			delete[]testVector[i]->ptr;
			delete  testVector[i];
		}

		testVector.clear();

		auto end = std::chrono::high_resolution_clock::now();
		int64_t elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		std::cout << elapsedTime << " millis" << std::endl;
	}
	return 0;
}