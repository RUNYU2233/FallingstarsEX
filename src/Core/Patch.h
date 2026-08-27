#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace FS::Patch
{

	/// <summary>
	/// Runtime patching helpers. Write memory with the right page protection
	/// and restore it afterwards. Prefer the static DEFINE_* macros (Phobos
	/// style) when a patch is unconditional; use these for runtime-decided
	/// patches (e.g. toggled by an INI setting).
	/// </summary>
	inline bool Unprotect(DWORD address, size_t size, DWORD& oldProtect)
	{
		return VirtualProtect(reinterpret_cast<LPVOID>(address), size,
			PAGE_EXECUTE_READWRITE, &oldProtect) != 0;
	}

	/// <summary>E9 relative JMP from `address` to `target`.</summary>
	inline void Apply_LJMP(DWORD address, DWORD target)
	{
		DWORD old{};
		if (!Unprotect(address, 5, old))
			return;
		BYTE* p = reinterpret_cast<BYTE*>(address);
		p[0] = 0xE9;
		*reinterpret_cast<DWORD*>(p + 1) = target - (address + 5);
		DWORD tmp{};
		VirtualProtect(reinterpret_cast<LPVOID>(address), 5, old, &tmp);
	}

	/// <summary>E8 relative CALL from `address` to `func`.</summary>
	inline void Apply_CALL(DWORD address, void* func)
	{
		DWORD old{};
		if (!Unprotect(address, 5, old))
			return;
		BYTE* p = reinterpret_cast<BYTE*>(address);
		p[0] = 0xE8;
		*reinterpret_cast<DWORD*>(p + 1) = reinterpret_cast<DWORD>(func) - (address + 5);
		DWORD tmp{};
		VirtualProtect(reinterpret_cast<LPVOID>(address), 5, old, &tmp);
	}

	/// <summary>Write raw bytes at `address`.</summary>
	inline void Apply_RAW(DWORD address, const BYTE* bytes, size_t size)
	{
		DWORD old{};
		if (!Unprotect(address, size, old))
			return;
		memcpy(reinterpret_cast<void*>(address), bytes, size);
		DWORD tmp{};
		VirtualProtect(reinterpret_cast<LPVOID>(address), size, old, &tmp);
	}

} // namespace FS::Patch
