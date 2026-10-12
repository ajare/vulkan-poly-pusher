#include "LegacyGpuValues.h"

#include <map>
#include <sstream>

#include "utils/StringUtils.h"

#include "mpp/resource-parsers/RenderGraphParser.h"
#include "mpp/resource-parsers/MppResourceParsersException.h"
#include "StructuredDataAdapter.h"

using namespace std;

namespace mpp
{
	namespace resource_parsers
	{
		namespace
		{
			GraphImageFormat parseFormat(string value, string const& filepath)
			{
				utils::StringUtils::toUpper(value);
				if (value == "R8") return GraphImageFormat::R8;
				if (value == "RG8") return GraphImageFormat::Rg8;
				if (value == "RGBA8") return GraphImageFormat::Rgba8;
				if (value == "SRGB8_ALPHA8") return GraphImageFormat::Srgb8Alpha8;
				if (value == "R16F") return GraphImageFormat::R16f;
				if (value == "RG16F") return GraphImageFormat::Rg16f;
				if (value == "RGBA16F") return GraphImageFormat::Rgba16f;
				if (value == "R32F") return GraphImageFormat::R32f;
				if (value == "RG32F") return GraphImageFormat::Rg32f;
				if (value == "RGBA32F") return GraphImageFormat::Rgba32f;
				if (value == "R11G11B10F") return GraphImageFormat::R11g11b10f;
				if (value == "RGB10_A2") return GraphImageFormat::Rgb10a2;
				if (value == "DEPTH16") return GraphImageFormat::Depth16;
				if (value == "DEPTH24") return GraphImageFormat::Depth24;
				if (value == "DEPTH32F") return GraphImageFormat::Depth32f;
				if (value == "DEPTH24_STENCIL8") return GraphImageFormat::Depth24Stencil8;
				if (value == "DEPTH32F_STENCIL8") return GraphImageFormat::Depth32fStencil8;
				THROW_MPP_RESOURCE_PARSERS("Unknown RenderGraph image format in " + filepath + ".", __LINE__, __FILE__, __func__);
			}

			GraphLoadOp parseLoad(string value)
			{
				utils::StringUtils::toUpper(value);
				if (value == "LOAD") return GraphLoadOp::Load;
				if (value == "CLEAR") return GraphLoadOp::Clear;
				return GraphLoadOp::DontCare;
			}

			GraphStoreOp parseStore(string value)
			{
				utils::StringUtils::toUpper(value);
				return value == "STORE" ? GraphStoreOp::Store : GraphStoreOp::DontCare;
			}

			glm::vec2 parseVec2(string const& value)
			{
				istringstream input(value); glm::vec2 result; input >> result.x >> result.y; return result;
			}

			glm::vec4 parseVec4(string const& value)
			{
				istringstream input(value); glm::vec4 result; input >> result.x >> result.y >> result.z >> result.w; return result;
			}

			bool parseBool(string value)
			{
				utils::StringUtils::toUpper(value);
				return value == "TRUE" || value == "1";
			}

			TextureColourSpace parseColourSpace(string value, string const& filepath)
			{
				utils::StringUtils::toUpper(value);
				if (value == "LINEAR" || value == "LINEAR_HDR") return TextureColourSpace::Linear;
				if (value == "SRGB" || value == "DISPLAY") return TextureColourSpace::Srgb;
				THROW_MPP_RESOURCE_PARSERS("Unknown RenderGraph colour space in " + filepath + ".", __LINE__, __FILE__, __func__);
			}

			GraphPassType parsePassType(string value, string const& filepath)
			{
				utils::StringUtils::toUpper(value);
				if (value == "SCENE") return GraphPassType::Scene;
				if (value == "FULLSCREEN") return GraphPassType::Fullscreen;
				if (value == "PRESENT") return GraphPassType::Present;
				THROW_MPP_RESOURCE_PARSERS("Unknown RenderGraph pass type in " + filepath + ".", __LINE__, __FILE__, __func__);
			}

			uint32_t parseMinFilter(string value, string const& filepath)
			{
				utils::StringUtils::toUpper(value);
				static map<string, uint32_t> const values = { { "NEAREST", GL_NEAREST }, { "LINEAR", GL_LINEAR }, { "NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST }, { "LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST }, { "NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR }, { "LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR } };
				auto const found = values.find(value);
				if (found != values.end()) return found->second;
				THROW_MPP_RESOURCE_PARSERS("Unknown RenderGraph min filter in " + filepath + ".", __LINE__, __FILE__, __func__);
			}

			uint32_t parseMagFilter(string value, string const& filepath)
			{
				utils::StringUtils::toUpper(value);
				if (value == "NEAREST") return GL_NEAREST;
				if (value == "LINEAR") return GL_LINEAR;
				THROW_MPP_RESOURCE_PARSERS("Unknown RenderGraph mag filter in " + filepath + ".", __LINE__, __FILE__, __func__);
			}

			uint32_t parseWrap(string value, string const& filepath)
			{
				utils::StringUtils::toUpper(value);
				static map<string, uint32_t> const values = { { "REPEAT", GL_REPEAT }, { "MIRRORED_REPEAT", GL_MIRRORED_REPEAT }, { "CLAMP_TO_EDGE", GL_CLAMP_TO_EDGE }, { "CLAMP_TO_BORDER", GL_CLAMP_TO_BORDER } };
				auto const found = values.find(value);
				if (found != values.end()) return found->second;
				THROW_MPP_RESOURCE_PARSERS("Unknown RenderGraph wrapping in " + filepath + ".", __LINE__, __FILE__, __func__);
			}

			// Raster-state enumerations reject unknown spellings rather than
			// defaulting, so a typo in an authored pipeline is reported instead of
			// silently changing how the pass rasterizes.
			template<typename Value>
			Value parseEnumeration(string value, map<string, Value> const& values, char const* field, string const& filepath)
			{
				utils::StringUtils::toUpper(value);
				auto const found = values.find(value);
				if (found != values.end()) return found->second;
				THROW_MPP_RESOURCE_PARSERS("Unknown RenderGraph raster " + string(field) + " in " + filepath + ".", __LINE__, __FILE__, __func__);
			}

			GraphFillMode parseFillMode(string const& value, string const& filepath)
			{
				static map<string, GraphFillMode> const values = { { "FILL", GraphFillMode::Fill }, { "LINE", GraphFillMode::Line } };
				return parseEnumeration(value, values, "fill mode", filepath);
			}

			GraphFrontFace parseFrontFace(string const& value, string const& filepath)
			{
				static map<string, GraphFrontFace> const values = { { "COUNTERCLOCKWISE", GraphFrontFace::CounterClockwise }, { "CCW", GraphFrontFace::CounterClockwise },
					{ "CLOCKWISE", GraphFrontFace::Clockwise }, { "CW", GraphFrontFace::Clockwise } };
				return parseEnumeration(value, values, "front face", filepath);
			}

			GraphCullMode parseCullMode(string const& value, string const& filepath)
			{
				static map<string, GraphCullMode> const values = { { "NONE", GraphCullMode::None }, { "FRONT", GraphCullMode::Front }, { "BACK", GraphCullMode::Back } };
				return parseEnumeration(value, values, "cull mode", filepath);
			}

			GraphCompareOp parseCompareOp(string const& value, string const& filepath)
			{
				static map<string, GraphCompareOp> const values = { { "NEVER", GraphCompareOp::Never }, { "LESS", GraphCompareOp::Less },
					{ "EQUAL", GraphCompareOp::Equal }, { "LESSEQUAL", GraphCompareOp::LessEqual }, { "GREATER", GraphCompareOp::Greater },
					{ "NOTEQUAL", GraphCompareOp::NotEqual }, { "GREATEREQUAL", GraphCompareOp::GreaterEqual }, { "ALWAYS", GraphCompareOp::Always } };
				return parseEnumeration(value, values, "depth comparison", filepath);
			}

			GraphBlendOp parseBlendOp(string const& value, string const& filepath)
			{
				static map<string, GraphBlendOp> const values = { { "ADD", GraphBlendOp::Add }, { "SUBTRACT", GraphBlendOp::Subtract },
					{ "REVERSESUBTRACT", GraphBlendOp::ReverseSubtract }, { "MINIMUM", GraphBlendOp::Minimum }, { "MAXIMUM", GraphBlendOp::Maximum } };
				return parseEnumeration(value, values, "blend operation", filepath);
			}

			GraphBlendFactor parseBlendFactor(string const& value, string const& filepath)
			{
				static map<string, GraphBlendFactor> const values = { { "ZERO", GraphBlendFactor::Zero }, { "ONE", GraphBlendFactor::One },
					{ "SOURCECOLOUR", GraphBlendFactor::SourceColour }, { "ONEMINUSSOURCECOLOUR", GraphBlendFactor::OneMinusSourceColour },
					{ "DESTINATIONCOLOUR", GraphBlendFactor::DestinationColour }, { "ONEMINUSDESTINATIONCOLOUR", GraphBlendFactor::OneMinusDestinationColour },
					{ "SOURCEALPHA", GraphBlendFactor::SourceAlpha }, { "ONEMINUSSOURCEALPHA", GraphBlendFactor::OneMinusSourceAlpha },
					{ "DESTINATIONALPHA", GraphBlendFactor::DestinationAlpha }, { "ONEMINUSDESTINATIONALPHA", GraphBlendFactor::OneMinusDestinationAlpha } };
				return parseEnumeration(value, values, "blend factor", filepath);
			}

			glm::uvec4 parseUvec4(string const& value)
			{
				istringstream input(value); glm::uvec4 result{ 0 }; input >> result.x >> result.y >> result.z >> result.w; return result;
			}

			GraphColourWriteMask parseWriteMask(string const& value)
			{
				istringstream input(value); GraphColourWriteMask mask; string channel;
				bool* const channels[] = { &mask.red, &mask.green, &mask.blue, &mask.alpha };
				for (auto* target : channels) if (input >> channel) *target = parseBool(channel);
				return mask;
			}

			GraphRasterState parseRasterState(mpp::data::StructuredData const& data, string const& filepath)
			{
				GraphRasterState state;
				auto flag = [&](char const* name, bool& target) { if (data.hasEntry(name)) target = parseBool(data.getEntry(name).getValue()); };
				flag("explicit", state.explicitState);
				if (data.hasEntry("fill")) state.fillMode = parseFillMode(data.getEntry("fill").getValue(), filepath);
				if (data.hasEntry("frontFace")) state.frontFace = parseFrontFace(data.getEntry("frontFace").getValue(), filepath);
				if (data.hasEntry("cull")) state.cullMode = parseCullMode(data.getEntry("cull").getValue(), filepath);
				flag("depthTest", state.depthTest);
				flag("depthWrite", state.depthWrite);
				if (data.hasEntry("depthCompare")) state.depthCompare = parseCompareOp(data.getEntry("depthCompare").getValue(), filepath);
				flag("blend", state.blend);
				if (data.hasEntry("colourBlendOp")) state.colourBlendOp = parseBlendOp(data.getEntry("colourBlendOp").getValue(), filepath);
				if (data.hasEntry("alphaBlendOp")) state.alphaBlendOp = parseBlendOp(data.getEntry("alphaBlendOp").getValue(), filepath);
				if (data.hasEntry("sourceColourBlend")) state.sourceColourBlend = parseBlendFactor(data.getEntry("sourceColourBlend").getValue(), filepath);
				if (data.hasEntry("destinationColourBlend")) state.destinationColourBlend = parseBlendFactor(data.getEntry("destinationColourBlend").getValue(), filepath);
				if (data.hasEntry("sourceAlphaBlend")) state.sourceAlphaBlend = parseBlendFactor(data.getEntry("sourceAlphaBlend").getValue(), filepath);
				if (data.hasEntry("destinationAlphaBlend")) state.destinationAlphaBlend = parseBlendFactor(data.getEntry("destinationAlphaBlend").getValue(), filepath);
				flag("multisample", state.multisample);
				flag("alphaToCoverage", state.alphaToCoverage);
				flag("scissor", state.scissor);
				if (data.hasEntry("scissorRectangle")) state.scissorRectangle = parseUvec4(data.getEntry("scissorRectangle").getValue());
				if (data.hasEntry("ColourWriteMasks"))
					for (auto const& mask : data.getEntry("ColourWriteMasks"))
					{
						if (mask.first != "Mask") continue;
						state.colourWriteMasks.push_back(parseWriteMask(mask.second.getValue()));
					}
				return state;
			}
		}

		RenderGraph RenderGraphParser::fromFile(string const& filepath)
		{
			auto data = detail::readDocumentRoot(filepath);
			return fromData(data, filepath);
		}

		RenderGraph RenderGraphParser::fromData(mpp::data::StructuredData const& data, string const& filepath)
		{
			if (data.getName() != "RenderGraph")
			{
				THROW_MPP_RESOURCE_PARSERS("Render graph root must be RenderGraph: " + filepath, __LINE__, __FILE__, __func__);
			}

			RenderGraph graph;
			map<string, GraphImageHandle> images;
			if (!data.hasEntry("Images"))
			{
				THROW_MPP_RESOURCE_PARSERS("Render graph has no Images block: " + filepath, __LINE__, __FILE__, __func__);
			}
			for (auto const& entry : data.getEntry("Images"))
			{
				if (entry.first != "Image") continue;
				auto const& image = entry.second;
				GraphImageDesc desc;
				desc.format = parseFormat(image.getEntry("format").getValue(), filepath);
				if (image.hasEntry("shape")) { auto shape = image.getEntry("shape").getValue(); utils::StringUtils::toUpper(shape); if (shape == "CUBEMAP") desc.shape = GraphImageShape::CubeMap; else if (shape != "2D" && shape != "TEXTURE2D") THROW_MPP_RESOURCE_PARSERS("Unknown render graph image shape '" + shape + "' in " + filepath, __LINE__, __FILE__, __func__); }
				desc.relativeSize = image.hasEntry("scale") ? parseVec2(image.getEntry("scale").getValue()) : glm::vec2(1.0f);
				if (image.hasEntry("width")) desc.absoluteSize.x = utils::StringUtils::parseUInt(image.getEntry("width").getValue());
				if (image.hasEntry("height")) desc.absoluteSize.y = utils::StringUtils::parseUInt(image.getEntry("height").getValue());
				if (image.hasEntry("samples")) THROW_MPP_RESOURCE_PARSERS("Legacy RenderGraph <samples> is no longer supported in " + filepath + "; migrate the pipeline to an explicit named output <AntiAliasing> block.", __LINE__, __FILE__, __func__);
				if (image.hasEntry("mipLevels")) desc.mipLevels = utils::StringUtils::parseUInt(image.getEntry("mipLevels").getValue());
				if (image.hasEntry("colourSpace")) desc.colourSpace = parseColourSpace(image.getEntry("colourSpace").getValue(), filepath);
				if (image.hasEntry("minFilter")) desc.params.minFilter = parseMinFilter(image.getEntry("minFilter").getValue(), filepath);
				if (image.hasEntry("magFilter")) desc.params.magFilter = parseMagFilter(image.getEntry("magFilter").getValue(), filepath);
				if (image.hasEntry("wrap")) desc.params.wrap = parseWrap(image.getEntry("wrap").getValue(), filepath);
				desc.params.useMipmaps = desc.mipLevels > 1;
				desc.depthCompare = image.hasEntry("depthCompare") && parseBool(image.getEntry("depthCompare").getValue());
				desc.external = image.hasEntry("import") || (image.hasEntry("external") && parseBool(image.getEntry("external").getValue()));
				desc.transient = !image.hasEntry("transient") || parseBool(image.getEntry("transient").getValue());
				string usage = image.getEntry("usage").getValue();
				utils::StringUtils::toUpper(usage);
				if (usage.find("SAMPLED") != string::npos) desc.usage = desc.usage | GraphImageUsage::Sampled;
				if (usage.find("COLOURATTACHMENT") != string::npos) desc.usage = desc.usage | GraphImageUsage::ColourAttachment;
				if (usage.find("DEPTHATTACHMENT") != string::npos) desc.usage = desc.usage | GraphImageUsage::DepthAttachment;
				if (usage.find("PRESENTATION") != string::npos) desc.usage = desc.usage | GraphImageUsage::Presentation;
				if (usage.find("EXPORTED") != string::npos) desc.usage = desc.usage | GraphImageUsage::Exported;
				auto handle = graph.createImage(image.getEntry("name").getValue(), desc);
				if (image.hasEntry("import")) graph.setImageImportName(handle, image.getEntry("import").getValue());
				if (image.hasEntry("value")) graph.setValueId(handle, image.getEntry("value").getValue());
				images[image.getEntry("name").getValue()] = handle;
			}

			if (!data.hasEntry("Passes")) return graph;
			for (auto const& entry : data.getEntry("Passes"))
			{
				if (entry.first != "Pass") continue;
				auto const& passData = entry.second;
				auto pass = graph.addPass(passData.getEntry("name").getValue(), passData.hasEntry("type") ? parsePassType(passData.getEntry("type").getValue(), filepath) : GraphPassType::Scene);
				if (passData.hasEntry("enabled")) graph.setPassEnabled(pass, parseBool(passData.getEntry("enabled").getValue()));
				if (passData.hasEntry("factory")) graph.setPassCallbackFactory(pass, passData.getEntry("factory").getValue());
				else if (passData.hasEntry("callback")) graph.setPassCallbackFactory(pass, passData.getEntry("callback").getValue());
				if (passData.hasEntry("program")) graph.setPassProgramResource(pass, passData.getEntry("program").getValue());
				if (passData.hasEntry("Parameters"))
				{
					UniformCollection parameters;
					for (auto const& parameter : passData.getEntry("Parameters"))
					{
						auto const& value = parameter.second;
						auto const& name = value.getEntry("name").getValue();
						if (parameter.first == "Float") parameters.setUniform(name, utils::StringUtils::parseFloat(value.getEntry("value").getValue()));
						else if (parameter.first == "Int") parameters.setUniform(name, (int32_t)utils::StringUtils::parseInt(value.getEntry("value").getValue()));
						else if (parameter.first == "Bool") parameters.setUniform(name, (int32_t)(parseBool(value.getEntry("value").getValue()) ? 1 : 0));
						else if (parameter.first == "Vec2") parameters.setUniform(name, parseVec2(value.getEntry("value").getValue()));
						else if (parameter.first == "Vec3") { istringstream input(value.getEntry("value").getValue()); glm::vec3 v; input >> v.x >> v.y >> v.z; parameters.setUniform(name, v); }
						else if (parameter.first == "Vec4") parameters.setUniform(name, parseVec4(value.getEntry("value").getValue()));
					}
					graph.setPassParameters(pass, parameters);
				}
				if (passData.hasEntry("Inputs"))
				{
					for (auto const& input : passData.getEntry("Inputs"))
					{
						if (input.first != "Sampled") continue;
						GraphImageHandle sampled;
						if (input.second.hasEntry("source")) sampled = graph.findValue(input.second.getEntry("source").getValue());
						else
						{
							auto it = images.find(input.second.getEntry("image").getValue());
							if (it != images.end()) sampled = it->second;
						}
						if (!sampled.isValid()) THROW_MPP_RESOURCE_PARSERS("Unknown sampled graph image value in " + filepath, __LINE__, __FILE__, __func__);
						if (input.second.hasEntry("sampler")) graph.bindSampler(pass, input.second.getEntry("sampler").getValue(), sampled, input.second.hasEntry("mipLevel") ? utils::StringUtils::parseUInt(input.second.getEntry("mipLevel").getValue()) : UINT32_MAX);
						else graph.readSampled(pass, sampled);
					}
				}
				if (passData.hasEntry("Colours"))
				{
					for (auto const& output : passData.getEntry("Colours"))
					{
						if (output.first != "Output") continue;
						auto it = images.find(output.second.getEntry("image").getValue());
						if (it == images.end()) THROW_MPP_RESOURCE_PARSERS("Unknown colour graph image in " + filepath, __LINE__, __FILE__, __func__);
						auto next = graph.writeColour(pass, it->second, parseLoad(output.second.getEntry("load").getValue()), parseStore(output.second.getEntry("store").getValue()), output.second.hasEntry("clear") ? parseVec4(output.second.getEntry("clear").getValue()) : glm::vec4(0.0f), output.second.hasEntry("mipLevel") ? utils::StringUtils::parseUInt(output.second.getEntry("mipLevel").getValue()) : 0, output.second.hasEntry("face") ? utils::StringUtils::parseUInt(output.second.getEntry("face").getValue()) : GraphNoCubeFace);
						if (output.second.hasEntry("value")) graph.setValueId(next, output.second.getEntry("value").getValue());
						it->second = next;
					}
				}
				if (passData.hasEntry("Depth"))
				{
					auto const& output = passData.getEntry("Depth");
					auto it = images.find(output.getEntry("image").getValue());
					if (it == images.end()) THROW_MPP_RESOURCE_PARSERS("Unknown depth graph image in " + filepath, __LINE__, __FILE__, __func__);
					auto next = graph.writeDepth(pass, it->second, parseLoad(output.getEntry("load").getValue()), parseStore(output.getEntry("store").getValue()), output.hasEntry("clear") ? utils::StringUtils::parseFloat(output.getEntry("clear").getValue()) : 1.0f, output.hasEntry("mipLevel") ? utils::StringUtils::parseUInt(output.getEntry("mipLevel").getValue()) : 0, output.hasEntry("face") ? utils::StringUtils::parseUInt(output.getEntry("face").getValue()) : GraphNoCubeFace);
					if (output.hasEntry("value")) graph.setValueId(next, output.getEntry("value").getValue());
					it->second = next;
				}
				if (passData.hasEntry("Raster")) graph.setPassRasterState(pass, parseRasterState(passData.getEntry("Raster"), filepath));
			}
			return graph;
		}
	}
}
