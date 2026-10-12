#include <fstream>
#include <stdexcept>

#include "utils/FileSystem.h"

#include "mpp/resource-parsers/FileStream.h"
#include "mpp/resource-parsers/XmlSerializer.h"
#include "mpp/resource-parsers/YamlSerializer.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		FileStream::FileStream(string const& filepath)
			: mFilepath(filepath)
			, mData("")
		{
			mFactories[".xml"] = []() {return new XmlSerializer(); };
				mFactories[".yaml"] = []() {return new YamlSerializer(); };
		}

		FileStream::FileStream(string const& filepath, mpp::data::StructuredData const& data)
			: mFilepath(filepath)
			, mData(data)
		{
			mFactories[".xml"] = []() {return new XmlSerializer(); };
				mFactories[".yaml"] = []() {return new YamlSerializer(); };
		}

		SerializerPtr FileStream::getSerializer(string const& type) const
		{
			auto it = mFactories.find(type);

			if (it == mFactories.end())
			{
				string errMsg = "Can't find serializer of type '" + type + "'.";
				throw runtime_error(errMsg);
			}

			return shared_ptr<Serializer>(it->second());
		}

		string const& FileStream::getFilepath() const
		{
			return mFilepath;
		}

		mpp::data::StructuredData const& FileStream::getStructuredData() const
		{
			if (mData.getName() == "")
			{
				auto fi = utils::FileSystem::getFile(getFilepath());
				auto ext = fi.getExtension();

				auto ser = getSerializer(ext);

				ser->loadFromFile(getFilepath());
				mData = ser->getData();
			}

			return mData;
		}

		string FileStream::readTextFile(string const& filepath, string const& root)
		{
			string fpath{ filepath };

			// if the filepath is relative, prepend the path of the xml file
			filesystem::path fp(filepath);
			if (fp.is_relative())
			{
				filesystem::path fileFp(root);
				fpath = utils::FileSystem::concatPaths(fileFp.parent_path().string(), filepath);
			}

			// Load
			return utils::FileSystem::readTextFile(fpath);
		}

	}
}