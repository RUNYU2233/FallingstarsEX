#pragma once

#include <vector>

class CCINIClass;

namespace FS
{

	/// <summary>
	/// A self-contained feature module. Subclass this, implement the hooks you
	/// need, expose a Meyers singleton via Instance(), then call
	/// REGISTER_MODULE(YourClass) once in the .cpp. Registration is automatic -
	/// you never edit a central registry file (unlike Phobos' PhobosTypeRegistry).
	/// </summary>
	class IModule
	{
	public:
		virtual ~IModule() = default;

		/// <summary>Human-readable module name (for logs).</summary>
		virtual const char* Name() const = 0;

		/// <summary>Called once when the game reaches the ExeRun entry hook
		/// (see FallingStars.cpp) - or from SyringeHandshake when Syringe runs
		/// with --handshakes. Keep it safe: do NOT call into game functions
		/// here (the process may not be fully initialized yet).</summary>
		virtual void OnLoad() { }

		/// <summary>Called during scenario/INI load so the module can read its
		/// configuration. Wire a hook on the game's INI-load routine to call
		/// FS::DispatchLoadFromINI (see docs/Structure.md).</summary>
		virtual void LoadFromINI(CCINIClass* pINI) { (void)pINI; }
	};

	/// <summary>
	/// Global, order-independent registry of modules. Uses an inline static
	/// vector so it is always initialized before first use regardless of the
	/// static-initialization order of the individual module registrars.
	/// </summary>
	class ModuleRegistry
	{
		static inline std::vector<IModule*> s_modules;
		static inline bool s_bLoaded = false;

	public:
		static void Register(IModule* pModule) { s_modules.push_back(pModule); }

		/// <summary>Run every module's OnLoad exactly once, even if both the
		/// ExeRun entry hook and SyringeHandshake (--handshakes) fire.</summary>
		static void OnLoadAll()
		{
			if (s_bLoaded)
				return;
			s_bLoaded = true;
			for (IModule* pModule : s_modules)
				pModule->OnLoad();
		}

		static void LoadFromINIAll(CCINIClass* pINI)
		{
			for (IModule* pModule : s_modules)
				pModule->LoadFromINI(pINI);
		}

		static size_t Count() { return s_modules.size(); }
	};

} // namespace FS

/// <summary>
/// Self-registering module. Place inside the module's .cpp, AFTER the class
/// definition. The anonymous-namespace registrar's constructor runs at DLL
/// load and adds the singleton to ModuleRegistry.
/// </summary>
#define REGISTER_MODULE(ModuleClass) \
	namespace { \
		struct ModuleClass##_Registrar \
		{ \
			ModuleClass##_Registrar() { ::FS::ModuleRegistry::Register(&ModuleClass::Instance()); } \
		} _g_##ModuleClass##_Registrar; \
	}
