#pragma once
#include "key.h"
/*____________________________________________________________________________________________________________

Original Author: skadro
Github: https://github.com/skadro-official
License: See end of file

skCrypter
		Compile-time, Usermode + Kernelmode, safe and lightweight string crypter library for C++11+

							*Not removing this part is appreciated*
____________________________________________________________________________________________________________*/

#ifdef _KERNEL_MODE
inline namespace SKCRYPTUTILS
{
	// STRUCT TEMPLATE remove_reference
	template <class _Ty>
	struct remove_referencee {
		using type = _Ty;
	};

	template <class _Ty>
	struct remove_referencee<_Ty&> {
		using type = _Ty;
	};

	template <class _Ty>
	struct remove_referencee<_Ty&&> {
		using type = _Ty;
	};

	template <class _Ty>
	using remove_referencee_t = typename remove_referencee<_Ty>::type;

	// STRUCT TEMPLATE remove_const
	template <class _Ty>
	struct remove_constt { // remove top-level const qualifier
		using type = _Ty;
	};

	template <class _Ty>
	struct remove_constt<const _Ty> {
		using type = _Ty;
	};

	template <class _Ty>
	using remove_const_u = typename remove_constt<_Ty>::type;
}
#else
#include <type_traits>
#endif

inline namespace skc
{
	template<class _Ty>
	using clean_type = typename SKCRYPTUTILS::remove_const_u<SKCRYPTUTILS::remove_referencee_t<_Ty>>;

	template <int _size, char _key1, char _key2, typename T>
	class skCrypter
	{
	public:
		__forceinline constexpr skCrypter(T* data)
		{
			crypt(data);
		}

		__forceinline T* get()
		{
			return _storage;
		}

		__forceinline int size() // (w)char count
		{
			return _size;
		}

		__forceinline  char key()
		{
			return _key1;
		}

		__forceinline  T* encrypt()
		{
			if (!isEncrypted())
				crypt(_storage);

			return _storage;
		}

		__forceinline  T* decrypt()
		{
			if (isEncrypted())
				crypt(_storage);

			return _storage;
		}

		__forceinline bool isEncrypted()
		{
			return _storage[_size - 1] != 0;
		}

		__forceinline void clear() // set full storage to 0
		{
			for (int i = 0; i < _size; i++)
			{
				_storage[i] = 0;
			}
		}

		__forceinline operator T* ()
		{
			decrypt();

			return _storage;
		}

	private:
		__forceinline constexpr void crypt(T* data)
		{
			for (int i = 0; i < _size; i++)
			{
				_storage[i] = data[i] ^ (_key1 + i % (1 + _key2));
			}
		}

		T _storage[_size]{};
	};
}

constexpr char kcp1[] = KCCKeyPart1;
constexpr char kcp2[] = KCCKeyPart2;
constexpr char kcp3[] = KCCKeyPart3;
constexpr char kcp4[] = KCCKeyPart4;
constexpr char kcp5[] = KCCKeyPart5;

constexpr char kcp_combined[] = {
	kcp1[0] ^ RANDOM_NUMBER, kcp1[1] ^ RANDOM_NUMBER, kcp1[2] ^ RANDOM_NUMBER, /* ... all characters of kcp1 ... */
	kcp2[0] ^ RANDOM_NUMBER, kcp2[1] ^ RANDOM_NUMBER, kcp2[2] ^ RANDOM_NUMBER, /* ... all characters of kcp2 ... */
	kcp3[0] ^ RANDOM_NUMBER, kcp3[1] ^ RANDOM_NUMBER, kcp3[2] ^ RANDOM_NUMBER, /* ... all characters of kcp3 ... */
	kcp4[0] ^ RANDOM_NUMBER, kcp4[1] ^ RANDOM_NUMBER, kcp4[2] ^ RANDOM_NUMBER, /* ... all characters of kcp4 ... */
	kcp5[0] ^ RANDOM_NUMBER, kcp5[1] ^ RANDOM_NUMBER, kcp5[2] ^ RANDOM_NUMBER, /* ... all characters of kcp5 ... */
	'\0' // Null terminator to mark the end of the string
};

#define E(str) skCrypt_key(str, kcp_combined[4] ^ RANDOM_NUMBER, kcp_combined[7] ^ RANDOM_NUMBER)
#define _(str) skCrypt_key(str, kcp_combined[4] ^ RANDOM_NUMBER, kcp_combined[7] ^ RANDOM_NUMBER)
#define skCrypt(str) skCrypt_key(str, kcp_combined[4] ^ RANDOM_NUMBER, kcp_combined[7] ^ RANDOM_NUMBER)

#define skCrypt_key(str, key1, key2) []() { \
			constexpr static auto crypted = skc::skCrypter \
				<sizeof(str) / sizeof(str[0]), key1, key2, skc::clean_type<decltype(str[0])>>((skc::clean_type<decltype(str[0])>*)str); \
					return crypted; }()
/*________________________________________________________________________________

MIT License

Copyright (c) 2020 skadro

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

________________________________________________________________________________*/