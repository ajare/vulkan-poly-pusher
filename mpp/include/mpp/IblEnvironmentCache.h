#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>

#include "mpp/Config.h"
#include "mpp/Resource.h"

namespace mpp
{
	struct _MPPAPI IblEnvironmentCacheKey
	{
		std::filesystem::path source;
		uint32_t environmentResolution{512};
		uint32_t irradianceResolution{32};
		uint32_t prefilterResolution{128};
		uint32_t preprocessingVersion{1};
		auto operator<=>(IblEnvironmentCacheKey const&) const = default;
	};

	struct _MPPAPI IblEnvironmentResources
	{
		ResourcePtr environmentCubemap;
		ResourcePtr irradianceCubemap;
		ResourcePtr prefilteredSpecularCubemap;
		ResourcePtr brdfLut;
		std::filesystem::file_time_type sourceWriteTime{};
	};

	// Renderer-owned cache. Generated resources are shared_ptr-owned so an
	// active pipeline may safely retain an older generation during replacement.
	class _MPPAPI IblEnvironmentCache
	{
		std::map<IblEnvironmentCacheKey, std::shared_ptr<IblEnvironmentResources>> mEntries;
	public:
		std::shared_ptr<IblEnvironmentResources> find(IblEnvironmentCacheKey const& key) const;
		void store(IblEnvironmentCacheKey const& key, std::shared_ptr<IblEnvironmentResources> resources);
		void invalidate(std::filesystem::path const& source);
		void clear();
		size_t size() const;
	};
}
