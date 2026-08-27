#include "FallingStars.h"

#include <YRpp.h>
#include <Core/Module.h>
#include <Core/Logging.h>

// ---------------------------------------------------------------------------
// .syhks00 keep-alive entry (safety net only).
//
// Once the real entry hook below (0x7CD810 ExeRun) is compiled in, this
// placeholder is redundant, but it stays as a safety net: if someone disables
// every DEFINE_HOOK, the section still exists so SyringeEx still recognizes
// and loads the DLL. SyringeEx skips entries whose hookName is nullptr
// (SyringeDebugger.cpp, ParseHooksSection) - no breakpoint is registered.
// ---------------------------------------------------------------------------
namespace SyringeData { namespace Hooks {
	__declspec(allocate(".syhks00")) hookdecl _hk__FallingStarsKeepAlive { 0, 0, nullptr };
}; };

// Anchor so the linker does not strip the section (/OPT:REF).
extern "C" __declspec(selectany) const void* YrKeepHook_FallingStarsKeepAlive =
	&SyringeData::Hooks::_hk__FallingStarsKeepAlive;
_YR_LINKER_FORCE_INCLUDE(YrKeepHook_FallingStarsKeepAlive)

// Module instance handle (stored by DllMain, same pattern as Phobos).
static HMODULE s_hModule = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
		s_hModule = hModule;
	return TRUE;
}

// ---------------------------------------------------------------------------
// Entry hook - the SAME address Phobos uses (see Phobos.cpp:
// DEFINE_HOOK(0x7CD810, ExeRun, 0x9)). It is the game's main-run entry and
// fires once at startup, so module initialization (banner + debug popup)
// happens here, exactly like Phobos::ExeRun().
//
// This is what makes injection "just work" like Phobos: Syringe registers the
// breakpoint from the .syhks00 section, and as soon as the game executes
// 0x7CD810 the loader loads FallingStars.dll and calls this function. No
// --handshakes flag is required.
// ---------------------------------------------------------------------------
DEFINE_HOOK(0x7CD810, FallingStars_ExeRun, 0x9)
{
	static bool s_bInitialized = false;
	if (!s_bInitialized)
	{
		s_bInitialized = true;
#ifdef FS_STABLE
		// 稳定应用版：无调试日志、无注入提示；log 只写一句欢迎语。
		// （FS_LOG 在 FS_STABLE 下已为空操作，此处直接调 Print。）
		FS::Log::ResetLog();
		FS::Log::Print("欢迎使用FallingstarsEX\n");
#else
		FS::Log::BeginSession(); // 清空上次日志，开启本次会话
		FS_LOG("[FallingStars] v%d.%d.%d (commit %s, branch %s) injected. %zu module(s) registered.\n",
			FALLINGSTARS_VERSION_MAJOR,
			FALLINGSTARS_VERSION_MINOR,
			FALLINGSTARS_VERSION_PATCH,
			FALLINGSTARS_GIT_COMMIT,
			FALLINGSTARS_GIT_BRANCH,
			FS::ModuleRegistry::Count());
#endif
		FS::ModuleRegistry::OnLoadAll();
	}
	return 0; // execute the stolen 9 bytes, then continue the original flow
}

// ---------------------------------------------------------------------------
// Optional handshake - only called when Syringe runs with --handshakes.
// Kept for compatibility; not required for injection (the ExeRun hook above
// is the primary path, same as Phobos). OnLoadAll() is idempotent, so module
// init still runs exactly once even if both this and ExeRun fire.
// ---------------------------------------------------------------------------
SYRINGE_HANDSHAKE(pInfo)
{
	pInfo->cbSize = sizeof(SyringeHandshakeInfo);
	FS::ModuleRegistry::OnLoadAll();
	return S_OK;
}

namespace FS
{

	void DispatchLoadFromINI(CCINIClass* pINI)
	{
		ModuleRegistry::LoadFromINIAll(pINI);
	}

} // namespace FS
