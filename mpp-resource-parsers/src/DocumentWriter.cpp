#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include "utils/XmlWriter.h"
#include "utils/YamlWriter.h"

#include "mpp/resource-parsers/DocumentWriter.h"
#include "mpp/resource-parsers/YamlWrapperCollapseTable.h"
#include "StructuredDataAdapter.h"

namespace mpp::resource_parsers::detail
{
	using namespace std;

	void writeDocument(mpp::data::StructuredData const& root, string const& filepath)
	{
		auto extension = std::filesystem::path(filepath).extension().string();
		for (auto& character : extension) character = (char)std::tolower((unsigned char)character);

		auto temporary = filepath + ".tmp";
		auto source = exportStructuredData(root);

		if (extension == ".yaml")
		{
			utils::YamlWriter(yamlWrapperCollapseTable()).writeTree(source, temporary);
		}
		else
		{
			utils::XmlWriter::writeTree(source, temporary);
		}

#ifdef _WIN32
		auto from = std::filesystem::path(temporary).wstring(), to = std::filesystem::path(filepath).wstring();
		if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			std::filesystem::remove(temporary);
			throw std::runtime_error("Could not replace '" + filepath + "' atomically.");
		}
#else
		std::filesystem::rename(temporary, filepath);
#endif
	}
}
