#include <stdexcept>

#include "mpp/data/StructuredData.h"

namespace mpp::data
{
	StructuredData::StructuredData(std::string const& name)
		: mName(name), mIsValue(false)
	{
	}

	StructuredData::StructuredData(std::string const& name, std::string const& value)
		: mName(name), mIsValue(true), mValue(value)
	{
	}

	std::string const& StructuredData::getName() const { return mName; }
	bool StructuredData::isValue() const { return mIsValue; }

	void StructuredData::setValue(std::string const& value)
	{
		mIsValue = true;
		mValue = value;
	}

	std::string const& StructuredData::getValue() const { return mValue; }

	void StructuredData::addEntry(std::string const& key, std::string const& value)
	{
		mEntries.emplace_back(key, StructuredData(key, value));
	}

	void StructuredData::addEntry(std::string const& key, StructuredData const& value)
	{
		mEntries.emplace_back(key, value);
	}

	StructuredData const& StructuredData::getEntry(std::string const& key) const
	{
		for (auto const& entry : mEntries)
			if (entry.first == key) return entry.second;
		throw std::runtime_error("Structured data entry not found: " + key);
	}

	StructuredData& StructuredData::getEntry(std::string const& key)
	{
		for (auto& entry : mEntries)
			if (entry.first == key) return entry.second;
		throw std::runtime_error("Structured data entry not found: " + key);
	}

	void StructuredData::setEntryValue(std::string const& key, std::string const& value)
	{
		for (auto& entry : mEntries)
		{
			if (entry.first == key)
			{
				entry.second.setValue(value);
				return;
			}
		}
		addEntry(key, value);
	}

	bool StructuredData::hasEntry(std::string const& key) const
	{
		for (auto const& entry : mEntries)
			if (entry.first == key) return true;
		return false;
	}

	std::vector<StructuredData::Entry>::iterator StructuredData::begin() { return mEntries.begin(); }
	std::vector<StructuredData::Entry>::iterator StructuredData::end() { return mEntries.end(); }
	std::vector<StructuredData::Entry>::const_iterator StructuredData::begin() const { return mEntries.begin(); }
	std::vector<StructuredData::Entry>::const_iterator StructuredData::end() const { return mEntries.end(); }
}
