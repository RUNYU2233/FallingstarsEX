#include "FallingStars.h"

#include <YRpp.h>
#include <Core/Module.h>
#include <Core/Logging.h>
#include <Core/INIHelpers.h>

// ---------------------------------------------------------------------------
// Example module: demonstrates the three convenience pillars of FallingStars
//   1. Self-registration (REGISTER_MODULE) - no central registry edits.
//   2. OnLoad banner - proves the DLL was injected.
//   3. LoadFromINI - null-safe INI reading via FS::INI helpers.
// Delete or replace this with your own modules.
// ---------------------------------------------------------------------------
class ExampleModule final : public FS::IModule
{
public:
	static ExampleModule& Instance()
	{
		static ExampleModule instance;
		return instance;
	}

	const char* Name() const override { return "ExampleModule"; }

	void OnLoad() override
	{
		FS_LOG("[FallingStars] ExampleModule ready. Add features under src/Extensions and src/Hooks.\n");
	}

	void LoadFromINI(CCINIClass* pINI) override
	{
		ExampleEnabled = FS::INI::ReadBool(pINI, "FallingStars", "ExampleEnabled", false);
		FS_LOG("[FallingStars] ExampleEnabled = %d\n", ExampleEnabled ? 1 : 0);
	}

	bool ExampleEnabled = false;
};

REGISTER_MODULE(ExampleModule)
