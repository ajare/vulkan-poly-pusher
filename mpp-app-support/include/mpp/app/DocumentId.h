#pragma once

#include <string>
#include <unordered_set>

namespace mpp::app
{
	bool isValidDocumentId(std::string const& id);

	class DocumentIdRegistry
	{
		std::unordered_set<std::string> mIds;

	public:
		bool contains(std::string const& id) const;

		bool reserve(std::string const& id);

		void release(std::string const& id);

		std::string makeUnique(std::string const& preferred);

		void clear();
	};
}
