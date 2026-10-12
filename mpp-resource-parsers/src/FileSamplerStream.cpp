#include "LegacyGpuValues.h"

#include "utils/StringUtils.h"


#include "utils/FileSystem.h"

#include "mpp/resource-parsers/FileSamplerStream.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		FileSamplerStream::FileSamplerStream(ResourceManager* resourceMgr, string const& filepath)
			: SamplerStream(resourceMgr)
			, FileStream(filepath)
		{
			setup();
		}

		FileSamplerStream::FileSamplerStream(ResourceManager* resourceMgr, string const& filepath, mpp::data::StructuredData const& data)
			: SamplerStream(resourceMgr)
			, FileStream(filepath, data)
		{
			setup();
		}

		void FileSamplerStream::setup()
		{
			// Filtering methods
			mMinFilters["LINEAR"] = GL_LINEAR;
			mMinFilters["NEAREST"] = GL_NEAREST;
			mMinFilters["NEAREST_MIPMAP_NEAREST"] = GL_NEAREST_MIPMAP_NEAREST;
			mMinFilters["NEAREST_MIPMAP_LINEAR"] = GL_NEAREST_MIPMAP_LINEAR;
			mMinFilters["LINEAR_MIPMAP_NEAREST"] = GL_LINEAR_MIPMAP_NEAREST;
			mMinFilters["LINEAR_MIPMAP_LINEAR"] = GL_LINEAR_MIPMAP_LINEAR;

			mMagFilters["LINEAR"] = GL_LINEAR;
			mMagFilters["NEAREST"] = GL_NEAREST;

			// Wrapping
			mWrapping["REPEAT"] = GL_REPEAT;
			mWrapping["MIRRORED_REPEAT"] = GL_MIRRORED_REPEAT;
			mWrapping["CLAMP_TO_EDGE"] = GL_CLAMP_TO_EDGE;
			mWrapping["CLAMP_TO_BORDER"] = GL_CLAMP_TO_BORDER;
		}

		uint32_t FileSamplerStream::parseMinFilter(string const& value)
		{
			auto it = mMinFilters.find(value);

			if (it != mMinFilters.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + getFilepath() + ".  Unknown/unsupported min-filter method '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		uint32_t FileSamplerStream::parseMagFilter(string const& value)
		{
			auto it = mMagFilters.find(value);

			if (it != mMagFilters.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + getFilepath() + ".  Unknown/unsupported mag-filter method '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		uint32_t FileSamplerStream::parseWrapping(string const& value)
		{
			auto it = mWrapping.find(value);

			if (it != mWrapping.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + getFilepath() + ".  Unknown/unsupported wrap method '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		void FileSamplerStream::parseDefinition(mpp::data::StructuredData const& data)
		{

			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = utils::StringUtils::toUpper(entry.second.getValue());

				if (entry.first == "minFilter")
				{
					mParams.minFilter = parseMinFilter(value);
				}
				else if (entry.first == "magFilter")
				{
					mParams.magFilter = parseMagFilter(value);
				}
				else if (entry.first == "lodMinLevel")
				{
					mParams.lodMinLevel = utils::StringUtils::parseFloat(value);
				}
				else if (entry.first == "lodMaxLevel")
				{
					mParams.lodMaxLevel = utils::StringUtils::parseFloat(value);
				}
				else if (entry.first == "lodBias")
				{
					mParams.lodBias = utils::StringUtils::parseFloat(value);
				}
				else if (entry.first == "wrap")
				{
					mParams.wrap = parseWrapping(value);
				}
				else if (entry.first == "maxAnisotropy")
				{
					mParams.maxAnisotropy = utils::StringUtils::parseFloat(value);
				}
			}
		}

		void FileSamplerStream::loadImpl()
		{
			auto const& data = getStructuredData();

			// Parse data.  Root element should be 'Sampler'
			auto rootName = data.getName();

			if (rootName != "Sampler" && rootName != "Resource")
			{
				string errMsg = "Error loading " + getFilepath() + ".  Root element is neither 'Sampler' nor 'Resource'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (data.hasEntry("Quality")) THROW_MPP_RESOURCE_PARSERS("Embedded <Quality> is no longer supported; split each sampler variant into a separate resource.", __LINE__, __FILE__, __func__);
			parseDefinition(data);

			SamplerStream::loadImpl();
		}
	}
}