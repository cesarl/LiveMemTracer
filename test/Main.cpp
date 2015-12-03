#include "../Src/MemTracer.hpp"

#include <vector>

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

int main(int ac, char **av)
{
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

	return 0;
}