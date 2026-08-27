#pragma once

#include "FallingStars.version.h"

// The whole injection contract lives in Syringe.h:
//   - SYRINGE_HANDSHAKE      : the DLL export SyringeEx calls right after load
//   - declhost(exe, checksum): host (gamemd.exe) declaration - REQUIRED for
//                              SyringeEx to recognize this DLL for the target
//   - declhook / DEFINE_HOOK : hook declarations (.syhks00 section)
//   - REGISTERS / feature flags
// Include it explicitly here so every TU that pulls in FallingStars.h gets the
// contract directly instead of relying on YRpp.h -> YRPPCore.h -> Syringe.h.
#include <Syringe.h>

class CCINIClass;

namespace FS
{

	/// <summary>
	/// Entry point for scenario/INI loading. Call this from a hook placed on the
	/// game's INI-load routine (e.g. the routine that digests rulesmd/artmd) so
	/// every registered module gets a chance to read its configuration.
	/// See docs/Structure.md for where to wire this.
	/// </summary>
	void DispatchLoadFromINI(CCINIClass* pINI);

} // namespace FS
