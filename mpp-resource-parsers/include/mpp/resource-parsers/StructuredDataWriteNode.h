#pragma once

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mpp/data/StructuredData.h"

namespace mpp::resource_parsers
{
	// A tree builder with the same createChild()/setValue() shape as
	// utils::XmlWriteNode, so existing hand-written document serializers can
	// build one tree and hand it to either utils::XmlWriter::writeTree() or
	// utils::YamlWriter::writeTree() instead of writing XML directly. Children
	// are heap-owned (as XmlWriteNode's are) so returned pointers stay valid
	// while later siblings are appended.
	class StructuredDataWriteNode
	{
		std::string mName;
		bool mIsValue{false};
		std::string mValue;
		std::vector<std::pair<std::string, std::unique_ptr<StructuredDataWriteNode>>> mChildren;

	public:

		explicit StructuredDataWriteNode(std::string name)
			: mName(std::move(name))
		{
		}

		StructuredDataWriteNode* createChild(std::string const& name)
		{
			mChildren.emplace_back(name, std::make_unique<StructuredDataWriteNode>(name));
			return mChildren.back().second.get();
		}

		void setValue(std::string const& value) { mIsValue = true; mValue = value; }
		void setValue(char const* value) { setValue(std::string(value)); }
		void setValue(int value) { setValue(std::format("{}", value)); }
		void setValue(unsigned int value) { setValue(std::format("{}", value)); }
		void setValue(uint64_t value) { setValue(std::format("{}", value)); }
		void setValue(float value) { setValue(std::format("{}", value)); }
		void setValue(bool value) { setValue(std::string(value ? "true" : "false")); }

		mpp::data::StructuredData toStructuredData() const
		{
			if (mIsValue) return mpp::data::StructuredData(mName, mValue);

			mpp::data::StructuredData result(mName);
			for (auto const& child : mChildren) result.addEntry(child.first, child.second->toStructuredData());
			return result;
		}
	};
}
