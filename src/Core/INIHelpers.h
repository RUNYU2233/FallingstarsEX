#pragma once

#include <YRpp.h>
#include <cstring>

namespace FS::INI
{

	/// <summary>Null-safe wrappers around CCINIClass readers. A null pINI
	/// returns the supplied default, so callers never crash during early init.
	/// </summary>
	inline bool ReadBool(CCINIClass* pINI, const char* section, const char* key, bool def)
	{
		return pINI ? pINI->ReadBool(section, key, def) : def;
	}

	inline int ReadInt(CCINIClass* pINI, const char* section, const char* key, int def)
	{
		return pINI ? pINI->ReadInteger(section, key, def) : def;
	}

	inline double ReadDouble(CCINIClass* pINI, const char* section, const char* key, double def)
	{
		return pINI ? pINI->ReadDouble(section, key, def) : def;
	}

	inline bool ReadString(CCINIClass* pINI, const char* section, const char* key,
		const char* def, char* buffer, size_t size)
	{
		if (size > 0)
			buffer[size - 1] = '\0';
		if (pINI)
		{
			pINI->ReadString(section, key, def, buffer, size);
			return true;
		}
		if (def)
			strncpy(buffer, def, size);
		return false;
	}

} // namespace FS::INI

// Convenience macros: read an INI value directly into a member, with a default.
// Usage: FS_INI_READ_BOOL(pINI, "MySection", "MyFlag", this->MyFlag, false);
#define FS_INI_READ_BOOL(pINI, section, key, member, def) \
	(member) = FS::INI::ReadBool((pINI), (section), (key), (def))
#define FS_INI_READ_INT(pINI, section, key, member, def) \
	(member) = FS::INI::ReadInt((pINI), (section), (key), (def))
#define FS_INI_READ_DOUBLE(pINI, section, key, member, def) \
	(member) = FS::INI::ReadDouble((pINI), (section), (key), (def))
#define FS_INI_READ_STRING(pINI, section, key, buffer, size, def) \
	FS::INI::ReadString((pINI), (section), (key), (def), (buffer), (size))
