#include "mpp/IblEnvironmentCache.h"

namespace mpp
{
	std::shared_ptr<IblEnvironmentResources> IblEnvironmentCache::find(IblEnvironmentCacheKey const& key) const
	{
		auto found = mEntries.find(key);
		if (found == mEntries.end()) return {};
		std::error_code error;
		auto current = std::filesystem::last_write_time(key.source, error);
		if (error || current != found->second->sourceWriteTime) return {};
		return found->second;
	}
	void IblEnvironmentCache::store(IblEnvironmentCacheKey const& key, std::shared_ptr<IblEnvironmentResources> resources)
	{
		if (!resources) return;
		std::error_code error;
		resources->sourceWriteTime = std::filesystem::last_write_time(key.source, error);
		mEntries[key] = std::move(resources);
	}
	void IblEnvironmentCache::invalidate(std::filesystem::path const& source)
	{
		for (auto entry = mEntries.begin(); entry != mEntries.end();) entry = entry->first.source == source ? mEntries.erase(entry) : std::next(entry);
	}
	void IblEnvironmentCache::clear() { mEntries.clear(); }
	size_t IblEnvironmentCache::size() const { return mEntries.size(); }
}
