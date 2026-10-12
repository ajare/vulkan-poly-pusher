#if defined(_WIN32)
#	include <Windows.h>
#endif

#include <filesystem>

#include "LegacyGpuValues.h"

#include "utils/StringUtils.h"


#include "utils/FileSystem.h"

#include "mpp/resource-parsers/FileTextureStream.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"

namespace mpp
{
	namespace resource_parsers
	{

		using namespace std;

		FileTextureStream::FileTextureStream(ResourceManager* resourceMgr, string const& filepath, bool relativisePaths)
			: TextureStream(resourceMgr)
			, FileStream(filepath)
			, mRelativisePaths(relativisePaths)
		{
			setup();
		}

		FileTextureStream::FileTextureStream(ResourceManager* resourceMgr, string const& filepath, mpp::data::StructuredData const& data, bool relativisePaths)
			: TextureStream(resourceMgr)
			, FileStream(filepath, data)
			, mRelativisePaths(relativisePaths)
		{
			setup();
		}

		void FileTextureStream::setup()
		{
		}

		uint32_t FileTextureStream::parseInternalFormat(string const& value, std::string const& filepath)
		{
			map<string, uint32_t> internalFormats;

			// Internal formats
			internalFormats["R8_SNORM"] = GL_R8_SNORM;
			internalFormats["RG8_SNORM"] = GL_RG8_SNORM;
			internalFormats["RGB8_SNORM"] = GL_RGB8_SNORM;
			internalFormats["RGBA8_SNORM"] = GL_RGBA8_SNORM;
			internalFormats["R16_SNORM"] = GL_RG16_SNORM;
			internalFormats["RG16_SNORM"] = GL_RGB16_SNORM;
			internalFormats["RGB16_SNORM"] = GL_RGB16_SNORM;
			internalFormats["RGBA16_SNORM"] = GL_RGBA16_SNORM;
			internalFormats["R8"] = GL_R8;
			internalFormats["RG8"] = GL_RG8;
			internalFormats["RGB8"] = GL_RGB8;
			internalFormats["RGBA8"] = GL_RGBA8;
			internalFormats["R16"] = GL_R16;
			internalFormats["RG16"] = GL_RG16;
			internalFormats["RGB16"] = GL_RGB16;
			internalFormats["RGBA16"] = GL_RGBA16;
			internalFormats["R16F"] = GL_R16F;
			internalFormats["RG16F"] = GL_RG16F;
			internalFormats["RGB16F"] = GL_RGB16F;
			internalFormats["RGBA16F"] = GL_RGBA16F;
			internalFormats["R32F"] = GL_R32F;
			internalFormats["RG32F"] = GL_RG32F;
			internalFormats["RGB32F"] = GL_RGB32F;
			internalFormats["RGBA32F"] = GL_RGBA32F;

			auto it = internalFormats.find(value);
			
			if (it != internalFormats.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + filepath + ".  Unknown/unsupported internal format '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		uint32_t FileTextureStream::parseMinFilter(string const& value, string const& filepath)
		{
			// Filtering methods
			map<string, uint32_t> minFilters;

			minFilters["LINEAR"] = GL_LINEAR;
			minFilters["NEAREST"] = GL_NEAREST;
			minFilters["NEAREST_MIPMAP_NEAREST"] = GL_NEAREST_MIPMAP_NEAREST;
			minFilters["NEAREST_MIPMAP_LINEAR"] = GL_NEAREST_MIPMAP_LINEAR;
			minFilters["LINEAR_MIPMAP_NEAREST"] = GL_LINEAR_MIPMAP_NEAREST;
			minFilters["LINEAR_MIPMAP_LINEAR"] = GL_LINEAR_MIPMAP_LINEAR;

			auto it = minFilters.find(value);

			if (it != minFilters.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + filepath + ".  Unknown/unsupported min-filter method '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		uint32_t FileTextureStream::parseMagFilter(string const& value, string const& filepath)
		{
			// Filtering methods
			map<string, uint32_t> magFilters;

			magFilters["LINEAR"] = GL_LINEAR;
			magFilters["NEAREST"] = GL_NEAREST;

			auto it = magFilters.find(value);

			if (it != magFilters.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + filepath + ".  Unknown/unsupported mag-filter method '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		uint32_t FileTextureStream::parseWrapping(string const& value, string const& filepath)
		{
			// Wrapping
			map<string, uint32_t> wrapping;

			wrapping["REPEAT"] = GL_REPEAT;
			wrapping["MIRRORED_REPEAT"] = GL_MIRRORED_REPEAT;
			wrapping["CLAMP_TO_EDGE"] = GL_CLAMP_TO_EDGE;
			wrapping["CLAMP_TO_BORDER"] = GL_CLAMP_TO_BORDER;

			auto it = wrapping.find(value);

			if (it != wrapping.end())
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + filepath + ".  Unknown/unsupported wrap method '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		uint32_t FileTextureStream::parseTarget(string const& value, string const& filepath)
		{
			map<string, uint32_t> targets;

			// Texture targets
			targets["1D"] = GL_TEXTURE_1D;
			targets["2D"] = GL_TEXTURE_2D;
			targets["3D"] = GL_TEXTURE_3D;
			targets["CUBEMAP"] = GL_TEXTURE_CUBE_MAP;

			auto it = targets.find(value);

			if (it != targets.end())	
			{
				return it->second;
			}
			else
			{
				string errMsg = "Error loading " + filepath + ".  Unknown/unsupported target '" + value + "' specified.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __FUNCTION__);
			}
		}

		pair<string, FileTextureStream::Definition> FileTextureStream::parseDefinition(mpp::data::StructuredData const& data, ResourceManager* resourceMgr, string const& filepath, bool relativisePaths)
		{
			string name;
			Definition qs;

			for (auto it = data.begin(); it != data.end(); ++it)
			{
				auto const& entry = *it;
				string value = utils::StringUtils::toUpper(entry.second.getValue());

				if (entry.first == "name")
				{
					name = entry.second.getValue();
				}
				else if (entry.first == "filename")
				{
					// If filename is specified, then load from disk
					string texFilepath = entry.second.getValue();

					// if the filepath is relative, prepend the path of the xml file
					filesystem::path texFp(texFilepath);
					if (texFp.is_relative() && relativisePaths)
					{
						filesystem::path fileFp(filepath);
						texFilepath = utils::FileSystem::concatPaths(fileFp.parent_path().string(), texFilepath);
					}

					qs.source = texFilepath;
				}
				if (entry.first == "target")
				{
					qs.target = parseTarget(value, filepath);
				}
				else if (entry.first == "internalFormat")
				{
					qs.internalFormat = parseInternalFormat(value, filepath);
				}
				else if (entry.first == "sampler")
				{
					qs.sampler = entry.second.getValue();
				}
				else if (entry.first == "colourSpace")
				{
					if (value == "SRGB")
					{
						qs.params.colourSpace = TextureColourSpace::Srgb;
					}
					else if (value == "LINEAR")
					{
						qs.params.colourSpace = TextureColourSpace::Linear;
					}
					else
					{
						THROW_MPP_RESOURCE_PARSERS("Unknown texture colour space in " + filepath + ".", __LINE__, __FILE__, __func__);
					}
				}
				else if (entry.first == "mipmaps")
				{
					qs.params.useMipmaps = utils::StringUtils::parseBool(value);
				}
				else if (entry.first == "minFilter")
				{
					qs.params.minFilter = parseMinFilter(value, filepath);
				}
				else if (entry.first == "magFilter")
				{
					qs.params.magFilter = parseMagFilter(value, filepath);
				}
				else if (entry.first == "lodBaseLevel")
				{
					qs.params.lodBaseLevel = utils::StringUtils::parseInt(value);
				}
				else if (entry.first == "lodMaxLevel")
				{
					qs.params.lodMaxLevel = utils::StringUtils::parseInt(value);
				}
				else if (entry.first == "lodBias")
				{
					qs.params.lodBias = utils::StringUtils::parseFloat(value);
				}
				else if (entry.first == "maxAnisotropy")
				{
					qs.params.maxAnisotropy = utils::StringUtils::parseFloat(value);
				}
				else if (entry.first == "wrap")
				{
					qs.params.wrap = parseWrapping(value, filepath);
				}
			}

			return make_pair(name, qs);
		}

		void FileTextureStream::loadImpl()
		{
			auto const& data = getStructuredData();

			// Parse data.  Root element should be 'Texture'
			auto rootName = data.getName();

			if (rootName != "Texture" && rootName != "Resource")
			{
				string errMsg = "Error loading " + getFilepath() + ".  Root element is neither 'Texture' nor 'Resource'.";
				THROW_MPP_RESOURCE_PARSERS(errMsg, __LINE__, __FILE__, __func__);
			}

			if (data.hasEntry("Quality")) THROW_MPP_RESOURCE_PARSERS("Embedded <Quality> is no longer supported; split each texture variant into a separate resource.", __LINE__, __FILE__, __func__);
			mDefinition = parseDefinition(data, getResourceMgr(), getFilepath(), mRelativisePaths).second;
			if (mDefinition.target == 0) THROW_MPP_RESOURCE_PARSERS("Error loading " + getFilepath() + ". 'target' not specified.", __LINE__, __FILE__, __func__);
			if (mDefinition.source.empty()) THROW_MPP_RESOURCE_PARSERS("Error loading " + getFilepath() + ". 'filename' not specified.", __LINE__, __FILE__, __func__);
			if (getResourceMgr()) mDefinition.loadFunc = getResourceMgr()->getImageLoadFunction();

			TextureStream::loadImpl();
		}
	}
}