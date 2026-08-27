#include "TechnoTypeBody.h"

#include <Core/INIHelpers.h>

// Definition of the static container.
TechnoTypeExt::Container TechnoTypeExt::ExtMap;

void TechnoTypeExt::ExtData::LoadFromINIFile(CCINIClass* pINI)
{
	// Example: read a custom boolean flag from the [FallingStars] section.
	// For a real per-type extension you would read from the section whose name
	// is the object's own ID (the caller passes it to the hook). Here we use a
	// fixed demo section so the template compiles and runs out of the box.
	FS_INI_READ_BOOL(pINI, "FallingStars", "TechnoTypeExampleFlag", ExampleFlag, false);
}
