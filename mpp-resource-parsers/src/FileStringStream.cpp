#include "utils/FileSystem.h"

#include "mpp/resource-parsers/FileStringStream.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		FileStringStream::FileStringStream(ResourceManager* resourceMgr, string const& filepath)
			: StringStream(resourceMgr)
			, FileStream(filepath)
		{
		}

		FileStringStream::FileStringStream(ResourceManager* resourceMgr, string const& filepath, mpp::data::StructuredData const& data)
			: StringStream(resourceMgr)
			, FileStream(filepath, data)
		{
		}

		void FileStringStream::parseDefinition(mpp::data::StructuredData const& data)
		{
			string value, file;

			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;

				if (entry.first == "value")
				{
					value = entry.second.getValue();
				}
				else if (entry.first == "file")
				{
					file = entry.second.getValue();
				}
			}

			if (value == "" & file == "")
			{
				string errMsg = "Error loading " + getFilepath() + ".  Neither 'value' nor 'file' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}
			if (value != "" && file != "")
			{
				string errMsg = "Error loading " + getFilepath() + ".  Both 'value' and 'file' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (value != "")
			{
				StringStream::mData = value;
			}
			else
			{
				// if the filepath is relative, prepend the path of the xml file
				filesystem::path fp(file);
				if (fp.is_relative())
				{
					filesystem::path fileFp(getFilepath());
					file = utils::FileSystem::concatPaths(fileFp.parent_path().string(), file);
				}

				// Load file
				StringStream::mData = utils::FileSystem::readTextFile(file);
			}
		}

		void FileStringStream::loadImpl()
		{
			auto const& data = getStructuredData();

			// Parse data.  Root element should be 'String'
			auto rootName = data.getName();

			if (rootName != "String" && rootName != "Resource")
			{
				string errMsg = "Error loading " + getFilepath() + ".  Root element is neither 'String' nor 'Resource'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (data.hasEntry("Quality")) THROW_MPP_RESOURCE_PARSERS("Embedded <Quality> is no longer supported; split each string variant into a separate resource.", __LINE__, __FILE__, __func__);
			parseDefinition(data);
		}
	}
}