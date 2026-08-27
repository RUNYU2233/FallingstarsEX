#pragma once

#include <YRpp.h>
#include <unordered_map>

namespace FS
{

	/// <summary>
	/// Base class for per-game-object extension data. Subclass this for each
	/// game type you extend (e.g. TechnoTypeClass, BuildingClass). The extension
	/// object lives in the DLL's own heap (normal new/delete) - it is NOT a game
	/// object, so it must never cross the DLL boundary into game code.
	/// </summary>
	template <typename TGameObject>
	class Extension
	{
	public:
		TGameObject* const Owner;

		explicit Extension(TGameObject* owner) : Owner(owner) { }

		virtual ~Extension() = default;

		/// <summary>Called when the owning object's INI (type) is loaded.
		/// Override to read custom INI keys into this extension.</summary>
		virtual void LoadFromINIFile(CCINIClass* pINI) { (void)pINI; }
	};

	/// <summary>
	/// Maps game objects to their Extension data. Self-contained (no dependency
	/// on Phobos' Container machinery). Typical usage inside an extension class:
	///
	///   using Container = FS::ExtensionContainer<TechnoTypeClass, ExtData>;
	///   static Container ExtMap;
	///
	///   TechnoTypeExt::ExtMap.Fetch(pType)->ExampleFlag = true;   // allocate if missing
	///   if (auto* pExt = TechnoTypeExt::ExtMap.Find(pType)) ...   // null if absent
	/// </summary>
	template <typename TGameObject, typename TExt>
	class ExtensionContainer
	{
		std::unordered_map<TGameObject*, TExt*> Map;

	public:
		/// <summary>Return existing data or null (does not allocate).</summary>
		TExt* Find(TGameObject* key) const
		{
			auto it = Map.find(key);
			return it == Map.end() ? nullptr : it->second;
		}

		/// <summary>Alias of Find - returns null when absent.</summary>
		TExt* TryFetch(TGameObject* key) { return Find(key); }

		/// <summary>Return existing data, allocating a new instance if needed.</summary>
		TExt* Fetch(TGameObject* key)
		{
			if (TExt* existing = Find(key))
				return existing;
			TExt* created = new TExt(key);
			Map[key] = created;
			return created;
		}

		/// <summary>Delete and forget the data for a key (if present).</summary>
		void Remove(TGameObject* key)
		{
			auto it = Map.find(key);
			if (it != Map.end())
			{
				delete it->second;
				Map.erase(it);
			}
		}

		/// <summary>Release all extension data (e.g. on scenario exit).</summary>
		void Clear()
		{
			for (auto& pair : Map)
				delete pair.second;
			Map.clear();
		}
	};

} // namespace FS
