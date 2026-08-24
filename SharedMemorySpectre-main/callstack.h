#pragma once
#ifdef KERNEL_MODE
#include <ntddk.h>
#include <ntdef.h>
#else
#include <Windows.h>
#include <utility>
#endif

// _AddressOfReturnAddress is compiler-provided. Declaring it directly keeps
// the kernel build independent of the user-mode VCRuntime headers pulled in by
// <intrin.h> on recent MSVC toolsets.
extern "C" void* _AddressOfReturnAddress(void);
#pragma intrinsic(_AddressOfReturnAddress)

namespace CallSpooferTraits
{
	template <typename Type>
	struct remove_reference
	{
		using type = Type;
	};

	template <typename Type>
	struct remove_reference<Type&>
	{
		using type = Type;
	};

	template <typename Type>
	struct remove_reference<Type&&>
	{
		using type = Type;
	};

	template <typename Type>
	using remove_reference_t = typename remove_reference<Type>::type;

	template <typename Left, typename Right>
	struct is_same
	{
		static constexpr bool value = false;
	};

	template <typename Type>
	struct is_same<Type, Type>
	{
		static constexpr bool value = true;
	};
}

/*
 *  Copyright 2022 Barracudach
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 // === FAQ === documentation is available at https://github.com/Barracudach
//Supports 2 modes: kernelmode and usermode(x64)
//For kernel- disable Control Flow Guard (CFG) /guard:cf 
//usermode c++17 and above
//kernelmode c++14 and above


#pragma optimize("", off)
#define SPOOF_FUNC CallSpoofer::SpoofFunction spoof(_AddressOfReturnAddress());
#ifdef KERNEL_MODE
#define SPOOF_CALL(ret_type,name) (CallSpoofer::SafeCall<ret_type, CallSpooferTraits::remove_reference_t<decltype(*name)>>(name))
#else
#define SPOOF_CALL(name) (CallSpoofer::SafeCall(name))
#endif


#define MAX_FUNC_BUFFERED 100
#define SHELLCODE_GENERATOR_SIZE 500

namespace CallSpoofer
{
#ifndef KERNEL_MODE
	using namespace std;
#endif
}

namespace CallSpoofer
{
	class SpoofFunction
	{
	public:
		ULONG_PTR temp = 0;
		const ULONG_PTR xor_key = 0xff00ff00ff00ff00;
		/*
		const ULONG_PTR xor_key = 0xff00ff00ff00ff00;
		*/
		void* ret_addr_in_stack = 0;

		SpoofFunction(void* addr) :ret_addr_in_stack(addr)
		{
			temp = *(ULONG_PTR*)ret_addr_in_stack;
			temp ^= xor_key;
			*(ULONG_PTR*)ret_addr_in_stack = 0;
		}
		~SpoofFunction()
		{
			temp ^= xor_key;
			*(ULONG_PTR*)ret_addr_in_stack = temp;
		}
	};

#ifdef KERNEL_MODE
	__forceinline PVOID LocateShellCode(PVOID func, SIZE_T size = 500)
	{
		void* addr = ExAllocatePoolWithTag(NonPagedPool, size, 'CpSC');
		if (!addr)
			return nullptr;
		return memcpy(addr, func, size);
	}
#else
	__forceinline PVOID LocateShellCode(PVOID func, SIZE_T size = SHELLCODE_GENERATOR_SIZE)
	{
		void* addr = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		if (!addr)
			return nullptr;
		return memcpy(addr, func, size);
	}
#endif

#ifdef KERNEL_MODE
	template <typename RetType, typename Func, typename ...Args>
	RetType
#else
	template <typename Func, typename ...Args>
	typename std::invoke_result<Func, Args...>::type
#endif
		__declspec(safebuffers)ShellCodeGenerator(Func f, Args&... args)
	{
#ifdef KERNEL_MODE
		using this_func_type = decltype(ShellCodeGenerator<RetType, Func, Args&...>);
		using return_type = RetType;
#else
		using this_func_type = decltype(ShellCodeGenerator<Func, Args&...>);
		using return_type = typename std::invoke_result<Func, Args...>::type;
#endif
		const ULONG_PTR xor_key = 0xff00ff00ff00ff00;
		void* ret_addr_in_stack = _AddressOfReturnAddress();
		ULONG_PTR temp = *(ULONG_PTR*)ret_addr_in_stack;
		temp ^= xor_key;
		*(ULONG_PTR*)ret_addr_in_stack = 0;

		if constexpr (CallSpooferTraits::is_same<return_type, void>::value)
		{
			f(args...);
			temp ^= xor_key;
			*(ULONG_PTR*)ret_addr_in_stack = temp;
		}
		else
		{
			return_type&& ret = f(args...);
			temp ^= xor_key;
			*(ULONG_PTR*)ret_addr_in_stack = temp;
			return ret;
		}
	}



#ifdef KERNEL_MODE
	template<typename RetType, class Func>
#else
	template<class Func >
#endif
	class SafeCall
	{

		Func* funcPtr;

	public:
		SafeCall(Func* func) :funcPtr(func) {}


		template<typename... Args>
		__forceinline decltype(auto) operator()(Args&&... args)
		{
			SPOOF_FUNC;

#ifdef KERNEL_MODE
			using return_type = RetType;
			using p_shell_code_generator_type = decltype(&ShellCodeGenerator<RetType, Func*, Args...>);
			PVOID self_addr = static_cast<PVOID>(&ShellCodeGenerator<RetType, Func*, Args&&...>);
#else	
			using return_type = typename std::invoke_result<Func, Args...>::type;
			using p_shell_code_generator_type = decltype(&ShellCodeGenerator<Func*, Args...>);
			p_shell_code_generator_type self_addr = static_cast<p_shell_code_generator_type>(&ShellCodeGenerator<Func*, Args&&...>);
#endif

			p_shell_code_generator_type p_shellcode{};

			static SIZE_T count{};
			static p_shell_code_generator_type orig_generator[MAX_FUNC_BUFFERED]{};
			static p_shell_code_generator_type alloc_generator[MAX_FUNC_BUFFERED]{};

			unsigned index{};
			while (orig_generator[index])
			{
				if (orig_generator[index] == self_addr)
				{
#ifdef KERNEL_MODE
					//DbgPrint("Found allocated generator");
#else
					//std::cout << "Found allocated generator" << std::endl;
#endif

					p_shellcode = alloc_generator[index];
					break;
				}
				index++;
			}

			if (!p_shellcode)
			{
#ifdef KERNEL_MODE
				//DbgPrint("Alloc generator");
#else	
				//std::cout << "Alloc generator" << std::endl;
#endif

				p_shellcode = reinterpret_cast<p_shell_code_generator_type>(LocateShellCode(self_addr));
				orig_generator[count] = self_addr;
				alloc_generator[count] = p_shellcode;
				count++;
			}

			if (!p_shellcode)
			{
				//DbgPrint("!p_shellcode");
			}

			return p_shellcode(funcPtr, args...);
		}
	};
}
#pragma optimize("", on)