#pragma once

#include <YRpp.h>
#include <Core/Extension.h>

/// <summary>
/// Example extension for TechnoTypeClass (per-type data, read once when the
/// type's INI is loaded). Copy this folder as a starting point for your own
/// extension classes. The pattern is identical for instance-level extensions
/// (e.g. TechnoClass, BuildingClass) - just change the game type.
/// </summary>
class TechnoTypeExt
{
public:
	/// <summary>Per-type extension data attached to a TechnoTypeClass.</summary>
	class ExtData final : public FS::Extension<TechnoTypeClass>
	{
	public:
		explicit ExtData(TechnoTypeClass* owner)
			: FS::Extension<TechnoTypeClass>(owner)
		{
		}

		// --- Custom fields (document each in docs/INI-Keys.md) ---
		bool ExampleFlag = false;

		void LoadFromINIFile(CCINIClass* pINI) override;
	};

	using Container = FS::ExtensionContainer<TechnoTypeClass, ExtData>;
	static Container ExtMap;
};
