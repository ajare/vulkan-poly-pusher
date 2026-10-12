#include <cctype>
#include <cstdint>

#include "mpp/app/DocumentId.h"

using namespace std;

namespace mpp::app
{
	bool isValidDocumentId(string const& id)
	{
		if (id.empty()) return false;
		auto validFirst = [](unsigned char c) { return isalpha(c) || c == '_'; };
		auto validRest = [](unsigned char c) { return isalnum(c) || c == '_' || c == '-' || c == '.'; };
		if (!validFirst(static_cast<unsigned char>(id.front()))) return false;
		for (size_t i = 1; i < id.size(); ++i)
			if (!validRest(static_cast<unsigned char>(id[i]))) return false;
		return true;
	}

	bool DocumentIdRegistry::contains(string const& id) const
	{
		return mIds.contains(id);
	}

	bool DocumentIdRegistry::reserve(string const& id)
	{
		return isValidDocumentId(id) && mIds.insert(id).second;
	}

	void DocumentIdRegistry::release(string const& id)
	{
		mIds.erase(id);
	}

	string DocumentIdRegistry::makeUnique(string const& preferred)
	{
		string base = isValidDocumentId(preferred) ? preferred : "Item";
		if (reserve(base)) return base;
		for (uint64_t suffix = 2;; ++suffix)
		{
			string candidate = base + "." + to_string(suffix);
			if (reserve(candidate)) return candidate;
		}
	}

	void DocumentIdRegistry::clear()
	{
		mIds.clear();
	}
}
