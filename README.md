LiveMemTracer
=============

LiveMemTracer trace and display allocation stacks in real time.

It's header only and use ImGui for display ( https://github.com/ocornut/imgui )

__/!\__ In a very early development stage __/!\__

Features
--------

- Inspect stack
- Search for callee
- Trace functions or stacked calls
- Detailed function view
- Header only, easy to setup
- Orbis version (contact me)

Inspect stack

![Stack](http://i.imgur.com/pi8HJnj.png "Stack")

Search for callee

![Callee](http://i.imgur.com/IqpkcGS.png "Callee")

Trace functions and stacked calls

![Histograms](http://i.imgur.com/8gUFZER.png "Histograms")

Detailed function view

![Detailed](http://i.imgur.com/QEgvGsy.png "Detailed")

Using it
--------

Define LiveMemTracer settings :
```cpp

    // Will enable "capture" feature :
    // Capture all stack infos and display diff between capture state
    // and current state.
    // (use more memory)
    #define LMT_CAPTURE_ACTIVATED 1

    // Will enable "instance count" feature :
    // Display the number of time functions allocate
    // Increment at allocation, decrement at free
    // (use more memory)
    #define LMT_INSTANCE_COUNT_ACTIVATED 1

    // Add more stats to "(?)" menu tooltip
    // Made it easy to setup dictionary size
    #define LMT_STATS 1

    // Define platform
    #define LMT_x64
    // Or
    #define LMT_x86

    // Number of allocs register per chunks
    // Adapt it to the number of allocations your program do
    // ( default : 1024 * 8 )
    #define LMT_ALLOC_NUMBER_PER_CHUNK 1024

    // Max depth of the stack
    // ( default : 50 )
    #define LMT_STACK_SIZE_PER_ALLOC 50

    // Pre allocated chunks per thread
    // ( default : 8 )
    #define LMT_CHUNK_NUMBER_PER_THREAD 4

    // Cache size (per thread cache)
    // ( default : 16 )
    #define LMT_CACHE_SIZE 8

    // Will assert one errors, example if Dictionnary is full
    #define LMT_DEBUG_DEV 1

    // ImGui header path
    #define LMT_IMGUI_INCLUDE_PATH "External/imgui/imgui.h"

    // Disable imgui
    #define LMT_IMGUI 0

    // Allocation dictionnary max entry
    // ( default : 1024 * 16 )
    #define LMT_ALLOC_DICTIONARY_SIZE 1024 * 16

    // Stack dictionnary max entry
    // ( default : 1024 * 16 )
    #define LMT_STACK_DICTIONARY_SIZE 1024 * 16

    // Leaf dictionnary max entry
    // Depend of the size of your program
    // ( default : 1024 * 16 * 16 )
    #define LMT_TREE_DICTIONARY_SIZE 1024 * 16 * 16

    // Allocation functions used by LiveMemTracer
    #define LMT_USE_MALLOC ::malloc
    #define LMT_USE_REALLOC ::realloc
    #define LMT_USE_FREE ::free

    // Your assert function
    #define LMT_ASSERT(condition, message, ...) assert(condition)

    // Important if you want that LiveMemTracer treat allocation chunks asynchronously :
    // You can override it and ask to LMT to do the job differently.
    // Example :
    #define LMT_TREAT_CHUNK(chunk) MyTaskScheduler::getInstancer().pushTask([=](){LiveMemTracer::treatChunk(chunk);})
```

In one `.cpp`, declare IMPL and include "LiveMemTracer.hpp" :

```cpp
    #define LMT_IMPL 1
    #include "../Src/LiveMemTracer.hpp"
```

In your main function :

```cpp
    int main(int ac, char **av)
    {
        LMT_INIT_SYMBOLS();
        LMT_INIT();
        // Your code
        LMT_EXIT();
        return EXIT_SUCCESS;
    }
```

If you want to enable LiveMemTracer, add `LMT_ENABLED` to your pre-processor variables. If not, it'll be totally deactivated.

Finally, used LMT macros for your allocations :

```cpp
    #define MY_MALLOC(size) LMT_ALLOC(size)
    #define MY_FREE(ptr) LMT_DEALLOC(ptr)
    #define MY_REALLOC(ptr, size) LMT_REALLOC(ptr, size)
    #define MY_MALLOC_ALIGNED(size, alignment) LMT_ALLOC_ALIGNED(size, alignment)
    #define MY_FREE_ALIGNED(ptr) LMT_DEALLOC_ALIGNED(ptr)
    #define MY_REALLOC_ALIGNED(ptr, size, alignment) LMT_REALLOC_ALIGNED(ptr, size, alignment)

    // Override new/delete operators
    void* operator new(size_t count) throw(std::bad_alloc)
    {
        return MY_MALLOC(count);
    }
    ...
    ...
    ...
```

Note :

If some of your threads do the same allocations / deallocations so that the cache is hit everytime and so the chunk is never full and so never treated, you can force the current thread to treat chunk, with `LMT_FLUSH()`.
