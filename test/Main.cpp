#include "../Src/MemTracer.hpp"

int main(int ac, char **av)
{
	void *a = LiveMemTracer::alloc(1);
	void *b = LiveMemTracer::alloc(10);
	void *c = LiveMemTracer::alloc(100);
	void *d = LiveMemTracer::alloc(1000);

	LiveMemTracer::dealloc(a);
	LiveMemTracer::dealloc(b);
	LiveMemTracer::dealloc(c);
	LiveMemTracer::dealloc(d);

	return 0;
}