#pragma once

#ifdef LMT_IMPLEMENTED

#ifndef LMT_PLATFORM_WINDOWS
static_assert(false, "LMT_PLATFORM_WINDOWS should be defined. Do not include this file in your code, only include LiveMemTracer.hpp");
#endif

#include <Windows.h>
#include "imagehlp.h"
#pragma comment(lib, "imagehlp.lib")

#define INTERNAL_LMT_ALLOC_ALIGNED_OFFSET(size, alignment, offset)_aligned_offset_malloc(size, alignment, offset)
#define INTERNAL_LMT_DEALLOC_ALIGNED(ptr)_aligned_free(ptr)

namespace LiveMemTracer
{
	static inline uint32_t getCallstack(size_t maxStackSize, void **stack, Hash *hash)
	{
		uint32_t count = CaptureStackBackTrace(INTERNAL_FRAME_TO_SKIP, maxStackSize, stack, (PDWORD)hash);

		if (count == maxStackSize)
		{
			void* tmpStack[INTERNAL_MAX_STACK_DEPTH];
			uint32_t tmpSize = CaptureStackBackTrace(INTERNAL_FRAME_TO_SKIP, INTERNAL_MAX_STACK_DEPTH, tmpStack, (PDWORD)hash);

			for (uint32_t i = 0; i < maxStackSize - 1; i++)
				stack[maxStackSize - 1 - i] = tmpStack[tmpSize - 1 - i];
			stack[0] = (void*)~0;
		}
		return count;
	}

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

		static inline const char *getSymbol(void *ptr, void *& absoluteAddress)
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
				return TRUNCATED_STACK_NAME;
			}
			absoluteAddress = (void*)(DWORD64(ptr) - dwDisplacement);
			return _strdup(pSymbol->Name);
		}
	}
}

#endif