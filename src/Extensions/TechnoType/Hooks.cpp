#include "TechnoTypeBody.h"

#include <FallingStars.version.h>
#include <Core/Macro.h>
#include <Core/INIHelpers.h>

// ---------------------------------------------------------------------------
// Example hook template (DISABLED by default).
//
// To activate:
//   1. In IDA/Ghidra, locate TechnoTypeClass::LoadFromINI and copy its address.
//   2. Set FALLINGSTARS_TECHNOTYPE_LOAD_ADDR to that address in
//      src/FallingStars.version.h (or FallingStars.props).
//   3. Set FALLINGSTARS_ENABLE_EXAMPLE_HOOKS=1 (in FallingStars.props).
//
// The hook below fetches (allocating if needed) the extension data for the
// type being loaded and reads its custom INI keys, then returns 0 so the
// game's original logic runs as normal.
// ---------------------------------------------------------------------------
#if FALLINGSTARS_ENABLE_EXAMPLE_HOOKS && (FALLINGSTARS_TECHNOTYPE_LOAD_ADDR != 0)

DEFINE_HOOK(FALLINGSTARS_TECHNOTYPE_LOAD_ADDR, TechnoTypeClass_LoadFromINI_Example, 0x6)
{
	// __thiscall: `this` is in ECX, first stack arg (CCINIClass*) is at ESP+4.
	GET(TechnoTypeClass*, pThis, ECX);
	GET_STACK(CCINIClass*, pINI, 0x4);

	TechnoTypeExt::ExtData* pExt = TechnoTypeExt::ExtMap.Fetch(pThis);
	pExt->LoadFromINIFile(pINI);

	return 0; // execute the stolen bytes, then continue into original code
}

#endif // FALLINGSTARS_ENABLE_EXAMPLE_HOOKS
