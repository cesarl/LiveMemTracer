//#define LMT_ENABLED 1
#define LMT_ALLOC_NUMBER_PER_CHUNK 1024
#define LMT_STACK_SIZE_PER_ALLOC 50
#define LMT_CHUNK_NUMBER_PER_THREAD 16
#define LMT_CACHE_SIZE 16

#define LMT_TREAT_CHUNK(chunk) LiveMemTracer::treatChunk(chunk);

#define LMT_IMPL 1
#define LMT_IMGUI 1
#define LMT_IMGUI_INCLUDE_PATH "External/imgui/imgui.h"

#include "../Src/MemTracer.hpp"

#include <vector>
#define OVERRIDE_NEW 1

#include LMT_IMGUI_INCLUDE_PATH
#include "External/GL/gl3w.h"
#include "External/GLFW/glfw3.h"
#include "External/imgui/imgui_impl_glfw_gl3.h"

#include <string>

static void error_callback(int error, const char* description)
{
	fprintf(stderr, "Error %d: %s\n", error, description);
}

#ifdef OVERRIDE_NEW
//////////////////////////////////////////////////////////////////////////
void* operator new(size_t count) throw(std::bad_alloc)
{
	return LMT_ALLOC(count);
}

void* operator new(size_t count, const std::nothrow_t&) throw()
{
	return LMT_ALLOC(count);
}

void* operator new(size_t count, size_t alignment) throw(std::bad_alloc)
{
	return LMT_ALLOC_ALIGNED(count, alignment);
}

void* operator new(size_t count, size_t alignment, const std::nothrow_t&) throw()
{
	return LMT_ALLOC_ALIGNED(count, alignment);
}

void* operator new[](size_t count) throw(std::bad_alloc)
{
	return LMT_ALLOC(count);
}

void* operator new[](size_t count, const std::nothrow_t&) throw()
{
	return LMT_ALLOC(count);
}

void* operator new[](size_t count, size_t alignment) throw(std::bad_alloc)
{
	return LMT_ALLOC_ALIGNED(count, alignment);
}

void* operator new[](size_t count, size_t alignment, const std::nothrow_t&) throw()
{
	return LMT_ALLOC_ALIGNED(count, alignment);
}

void operator delete(void* ptr) throw()
{
	return LMT_DEALLOC(ptr);
}

void operator delete(void *ptr, const std::nothrow_t&) throw()
{
	return LMT_DEALLOC(ptr);
}

void operator delete(void *ptr, size_t alignment) throw()
{
	return LMT_DEALLOC_ALIGNED(ptr);
}

void operator delete(void *ptr, size_t alignment, const std::nothrow_t&) throw()
{
	return LMT_DEALLOC_ALIGNED(ptr);
}

void operator delete[](void* ptr) throw()
{
	return LMT_DEALLOC(ptr);
}

void operator delete[](void *ptr, const std::nothrow_t&) throw()
{
	return LMT_DEALLOC(ptr);
}

void operator delete[](void *ptr, size_t alignment) throw()
{
	return LMT_DEALLOC_ALIGNED(ptr);
}

void operator delete[](void *ptr, size_t alignment, const std::nothrow_t&) throw()
{
	return LMT_DEALLOC_ALIGNED(ptr);
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
	Foo*         next;
	Foo() :
		next(nullptr)
	{}
};

template <size_t size>
struct FooBarFunctor
{
	static void call(Foo *prev)
	{
		if (size == 0)
			return;
		Foo *tmpFoo = new Foo;
		tmpFoo->index = size;
		tmpFoo->indexStr = std::to_string(size);
		tmpFoo->ptr = new Bar[size]();
		for (int i = 0; i < size; ++i)
			tmpFoo->indexStr += "|coucou|";
		prev->next = tmpFoo;
		FooBarFunctor<(size > 0 ? size - 1 : 0)>::call(tmpFoo);
	}
};


struct Tata
{
	char tata[1024];
};

struct Toto
{
	char toto[128];
	Tata *one;
	Tata *two;
	Toto()
	{
		one = new Tata();
		two = new Tata();
	}
	~Toto()
	{
		delete one;
		delete two;
	}
};

#include <chrono>
#include <iostream>

int main(int ac, char **av)
{
	// Setup window
	glfwSetErrorCallback(error_callback);
	if (!glfwInit())
		exit(1);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	GLFWwindow* window = glfwCreateWindow(1280, 720, "ImGui OpenGL3 example", NULL, NULL);
	glfwMakeContextCurrent(window);
	gl3wInit();

	// Setup ImGui binding
	ImGui_ImplGlfwGL3_Init(window, true);

	ImVec4 clear_color = ImColor(114, 144, 154);

	// Main loop
	float dt = 0.0f;
	std::vector<Toto*> totoVector;
	int clearCounter = 0;
	while (!glfwWindowShouldClose(window))
	{
		auto start = std::chrono::high_resolution_clock::now();

		glfwPollEvents();
		ImGui_ImplGlfwGL3_NewFrame();

		// Rendering
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
		glClear(GL_COLOR_BUFFER_BIT);

		Foo foo;
		FooBarFunctor<1>::call(&foo);

		FooBarFunctor<200>::call(foo.next);

		Foo *p = foo.next;
		while (p != nullptr)
		{
			auto *next = p->next;
			delete[]p->ptr;
			delete  p;
			p = next;
		}

		for (int i = 0; i < 100; ++i)
		{
			totoVector.push_back(new Toto());
		}
		if (++clearCounter == 100)
		{
			for (auto &e : totoVector)
			{
				delete e;
			}
			totoVector.clear();
			clearCounter = 0;
		}

		LMT_DISPLAY(dt);

		ImGui::Render();
		glfwSwapBuffers(window);


		auto end = std::chrono::high_resolution_clock::now();
		int64_t elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
		dt = float(elapsedTime) / 1000.f;
	}

	ImGui_ImplGlfwGL3_Shutdown();
	glfwTerminate();
	LMT_EXIT();
	return 0;
}