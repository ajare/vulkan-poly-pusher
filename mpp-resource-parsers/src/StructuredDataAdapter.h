#pragma once

#include <string>

#include "mpp/data/StructuredData.h"
#include "utils/StructuredData.h"

#include "mpp/resource-parsers/FileStream.h"

namespace mpp::resource_parsers::detail
{
	inline mpp::data::StructuredData importStructuredData(utils::StructuredData const& source)
	{
		if (source.isValue())
			return mpp::data::StructuredData(source.getName(), source.getValue());

		mpp::data::StructuredData result(source.getName());
		for (auto const& entry : source)
			result.addEntry(entry.first, importStructuredData(entry.second));
		return result;
	}

	inline utils::StructuredData exportStructuredData(mpp::data::StructuredData const& source)
	{
		if (source.isValue())
			return utils::StructuredData(source.getName(), source.getValue());

		utils::StructuredData result(source.getName());
		for (auto const& entry : source)
			result.addEntry(entry.first, exportStructuredData(entry.second));
		return result;
	}

	// Reads a document's root StructuredData via FileStream's extension-based
	// dispatch (.xml/.yaml), so every document-level parser accepts either
	// format transparently instead of hardcoding utils::XmlReader.
	inline mpp::data::StructuredData readDocumentRoot(std::string const& filepath)
	{
		FileStream stream(filepath);
		return stream.getStructuredData();
	}
}
