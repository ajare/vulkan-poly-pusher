#pragma once

#include <string>
#include <utility>
#include <vector>

#include "mpp/data/Config.h"

namespace mpp::data
{
	// Project-owned structured tree used at public API boundaries. XML and other
	// external parser representations are converted to this type in adapter code.
	class _MPPDATAAPI StructuredData
	{
	public:
		using Entry = std::pair<std::string, StructuredData>;

	private:
		std::string mName;
		bool mIsValue;
		std::string mValue;
		std::vector<Entry> mEntries;

	public:
		explicit StructuredData(std::string const& name);
		StructuredData(std::string const& name, std::string const& value);

		std::string const& getName() const;
		bool isValue() const;
		void setValue(std::string const& value);
		std::string const& getValue() const;
		void addEntry(std::string const& key, std::string const& value);
		void addEntry(std::string const& key, StructuredData const& value);
		StructuredData const& getEntry(std::string const& key) const;
		StructuredData& getEntry(std::string const& key);
		void setEntryValue(std::string const& key, std::string const& value);
		bool hasEntry(std::string const& key) const;

		std::vector<Entry>::iterator begin();
		std::vector<Entry>::iterator end();
		std::vector<Entry>::const_iterator begin() const;
		std::vector<Entry>::const_iterator end() const;
	};
}
