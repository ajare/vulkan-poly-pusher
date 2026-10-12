#include "LegacyGpuValues.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#include "mpp/resource-parsers/DocumentWriter.h"
#include "mpp/resource-parsers/RenderGraphSerializer.h"
#include "mpp/resource-parsers/StructuredDataWriteNode.h"

namespace mpp::resource_parsers
{
	namespace
	{
		std::string format(GraphImageFormat value)
		{
			switch (value)
			{
			case GraphImageFormat::R8: return "R8"; case GraphImageFormat::Rg8: return "RG8"; case GraphImageFormat::Rgba8: return "RGBA8";
			case GraphImageFormat::Srgb8Alpha8: return "SRGB8_ALPHA8"; case GraphImageFormat::R16f: return "R16F";
			case GraphImageFormat::Rg16f: return "RG16F"; case GraphImageFormat::Rgba16f: return "RGBA16F";
			case GraphImageFormat::R32f: return "R32F"; case GraphImageFormat::Rg32f: return "RG32F"; case GraphImageFormat::Rgba32f: return "RGBA32F";
			case GraphImageFormat::R11g11b10f: return "R11G11B10F"; case GraphImageFormat::Rgb10a2: return "RGB10_A2";
			case GraphImageFormat::Depth16: return "DEPTH16"; case GraphImageFormat::Depth24: return "DEPTH24"; case GraphImageFormat::Depth32f: return "DEPTH32F";
			case GraphImageFormat::Depth24Stencil8: return "DEPTH24_STENCIL8"; case GraphImageFormat::Depth32fStencil8: return "DEPTH32F_STENCIL8";
			}
			return "UNKNOWN";
		}
		std::string usage(GraphImageUsage value)
		{
			std::string result;
			auto add = [&](GraphImageUsage flag, char const* name) { if (hasGraphImageUsage(value, flag)) { if (!result.empty()) result += ","; result += name; } };
			add(GraphImageUsage::Sampled, "sampled"); add(GraphImageUsage::ColourAttachment, "colourAttachment"); add(GraphImageUsage::DepthAttachment, "depthAttachment"); add(GraphImageUsage::Presentation, "presentation"); add(GraphImageUsage::Exported, "exported");
			return result;
		}
		std::string colourSpace(TextureColourSpace value) { return value == TextureColourSpace::Srgb ? "SRGB" : "LINEAR"; }
		std::string minFilter(uint32_t value)
		{
			switch (value) { case GL_NEAREST: return "NEAREST"; case GL_LINEAR: return "LINEAR"; case GL_NEAREST_MIPMAP_NEAREST: return "NEAREST_MIPMAP_NEAREST"; case GL_LINEAR_MIPMAP_NEAREST: return "LINEAR_MIPMAP_NEAREST"; case GL_NEAREST_MIPMAP_LINEAR: return "NEAREST_MIPMAP_LINEAR"; default: return "LINEAR_MIPMAP_LINEAR"; }
		}
		std::string magFilter(uint32_t value) { return value == GL_NEAREST ? "NEAREST" : "LINEAR"; }
		std::string wrap(uint32_t value)
		{
			switch (value) { case GL_REPEAT: return "REPEAT"; case GL_MIRRORED_REPEAT: return "MIRRORED_REPEAT"; case GL_CLAMP_TO_BORDER: return "CLAMP_TO_BORDER"; default: return "CLAMP_TO_EDGE"; }
		}
		std::string passType(GraphPassType value)
		{
			switch (value) { case GraphPassType::Scene: return "scene"; case GraphPassType::Fullscreen: return "fullscreen"; default: return "present"; }
		}
		std::string load(GraphLoadOp value) { return value == GraphLoadOp::Load ? "load" : (value == GraphLoadOp::Clear ? "clear" : "dontCare"); }
		std::string store(GraphStoreOp value) { return value == GraphStoreOp::Store ? "store" : "dontCare"; }
		std::string imageName(RenderGraph const& graph, GraphImageHandle handle) { return graph.getImageInfo(handle).name; }
		std::string vec4(glm::vec4 const& value) { std::ostringstream out; out << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w; return out.str(); }
		std::string uvec4(glm::uvec4 const& value) { std::ostringstream out; out << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w; return out.str(); }
		std::string fillMode(GraphFillMode value) { return value == GraphFillMode::Line ? "line" : "fill"; }
		std::string frontFace(GraphFrontFace value) { return value == GraphFrontFace::Clockwise ? "clockwise" : "counterClockwise"; }
		std::string cullMode(GraphCullMode value)
		{
			switch (value) { case GraphCullMode::None: return "none"; case GraphCullMode::Front: return "front"; default: return "back"; }
		}
		std::string compareOp(GraphCompareOp value)
		{
			switch (value)
			{
			case GraphCompareOp::Never: return "never"; case GraphCompareOp::Less: return "less";
			case GraphCompareOp::Equal: return "equal"; case GraphCompareOp::LessEqual: return "lessEqual";
			case GraphCompareOp::Greater: return "greater"; case GraphCompareOp::NotEqual: return "notEqual";
			case GraphCompareOp::GreaterEqual: return "greaterEqual"; default: return "always";
			}
		}
		std::string blendOp(GraphBlendOp value)
		{
			switch (value)
			{
			case GraphBlendOp::Add: return "add"; case GraphBlendOp::Subtract: return "subtract";
			case GraphBlendOp::ReverseSubtract: return "reverseSubtract"; case GraphBlendOp::Minimum: return "minimum";
			default: return "maximum";
			}
		}
		std::string blendFactor(GraphBlendFactor value)
		{
			switch (value)
			{
			case GraphBlendFactor::Zero: return "zero"; case GraphBlendFactor::One: return "one";
			case GraphBlendFactor::SourceColour: return "sourceColour"; case GraphBlendFactor::OneMinusSourceColour: return "oneMinusSourceColour";
			case GraphBlendFactor::DestinationColour: return "destinationColour"; case GraphBlendFactor::OneMinusDestinationColour: return "oneMinusDestinationColour";
			case GraphBlendFactor::SourceAlpha: return "sourceAlpha"; case GraphBlendFactor::OneMinusSourceAlpha: return "oneMinusSourceAlpha";
			case GraphBlendFactor::DestinationAlpha: return "destinationAlpha"; default: return "oneMinusDestinationAlpha";
			}
		}
		std::string writeMask(GraphColourWriteMask const& value)
		{
			auto flag = [](bool set) { return set ? "true" : "false"; };
			return std::string(flag(value.red)) + ' ' + flag(value.green) + ' ' + flag(value.blue) + ' ' + flag(value.alpha);
		}
	}

	void RenderGraphSerializer::toNode(RenderGraph const& graph, StructuredDataWriteNode* root)
	{
		if (!root) return;
		auto images = root->createChild("Images");
		for (uint32_t id = 0; id < graph.getImageCount(); ++id)
		{
			auto info = graph.getImageInfo({ id, 0 });
			auto image = images->createChild("Image");
			image->createChild("name")->setValue(info.name);
			image->createChild("format")->setValue(format(info.desc.format));
			if (info.desc.shape == GraphImageShape::CubeMap) image->createChild("shape")->setValue("cubemap");
			image->createChild("scale")->setValue(std::to_string(info.desc.relativeSize.x) + " " + std::to_string(info.desc.relativeSize.y));
			if (info.desc.absoluteSize.x) image->createChild("width")->setValue(info.desc.absoluteSize.x);
			if (info.desc.absoluteSize.y) image->createChild("height")->setValue(info.desc.absoluteSize.y);
			image->createChild("mipLevels")->setValue(info.desc.mipLevels);
			image->createChild("usage")->setValue(usage(info.desc.usage));
			image->createChild("colourSpace")->setValue(colourSpace(info.desc.colourSpace));
			image->createChild("minFilter")->setValue(minFilter(info.desc.params.minFilter));
			image->createChild("magFilter")->setValue(magFilter(info.desc.params.magFilter));
			image->createChild("wrap")->setValue(wrap(info.desc.params.wrap));
			if (info.desc.depthCompare) image->createChild("depthCompare")->setValue(true);
			image->createChild("external")->setValue(info.desc.external);
			image->createChild("transient")->setValue(info.desc.transient);
			if (!info.importName.empty()) image->createChild("import")->setValue(info.importName);
			image->createChild("value")->setValue(graph.getValueId({ id, 0 }));
		}
		auto passes = root->createChild("Passes");
		for (uint32_t id = 0; id < graph.getPassCount(); ++id)
		{
			auto info = graph.getPassInfo({ id });
			auto pass = passes->createChild("Pass");
			pass->createChild("name")->setValue(info.name);
			pass->createChild("enabled")->setValue(info.enabled);
			pass->createChild("type")->setValue(passType(info.type));
			if (!info.callbackFactory.empty()) pass->createChild("factory")->setValue(info.callbackFactory);
			if (!info.programResource.empty()) pass->createChild("program")->setValue(info.programResource);
			if (!info.sampledInputs.empty())
			{
				auto inputs = pass->createChild("Inputs");
				for (auto const& image : info.sampledInputs)
				{
					auto sampled = inputs->createChild("Sampled");
					auto binding = std::find_if(info.samplerBindings.begin(), info.samplerBindings.end(), [&](GraphSamplerBinding const& current) { return current.image.id == image.id && current.image.version == image.version; });
					if (binding != info.samplerBindings.end()) { sampled->createChild("sampler")->setValue(binding->sampler); if (binding->mipLevel != UINT32_MAX) sampled->createChild("mipLevel")->setValue(binding->mipLevel); }
					sampled->createChild("image")->setValue(imageName(graph, image));
					sampled->createChild("source")->setValue(graph.getValueId(image));
				}
			}
			if (!info.parameters.getUniformData().empty())
			{
				auto parameters = pass->createChild("Parameters");
				for (auto const& entry : info.parameters.getUniformData())
				{
					auto const& value = entry.second;
					if (value.count != 1) continue;
					auto node = parameters->createChild(value.type == program::GLSLType::Int ? "Int" : (value.type == program::GLSLType::Bool ? "Bool" : value.numElements == 1 ? "Float" : value.numElements == 2 ? "Vec2" : value.numElements == 3 ? "Vec3" : "Vec4"));
					node->createChild("name")->setValue(value.name);
					std::ostringstream text;
					if (value.type == program::GLSLType::Int || value.type == program::GLSLType::Bool) { int32_t v; memcpy(&v, value.data, sizeof(v)); text << v; }
					else { auto values = reinterpret_cast<float const*>(value.data); for (size_t i = 0; i < value.numElements; ++i) { if (i) text << ' '; text << values[i]; } }
					node->createChild("value")->setValue(text.str());
				}
			}
			if (!info.colourOutputs.empty())
			{
				auto colours = pass->createChild("Colours");
				for (auto const& output : info.colourOutputs) { auto node = colours->createChild("Output"); node->createChild("image")->setValue(imageName(graph, output.image)); node->createChild("value")->setValue(graph.getValueId(output.image)); if (output.mipLevel) node->createChild("mipLevel")->setValue(output.mipLevel); if (output.cubeFace != GraphNoCubeFace) node->createChild("face")->setValue(output.cubeFace); node->createChild("load")->setValue(load(output.load)); node->createChild("store")->setValue(store(output.store)); if (output.load == GraphLoadOp::Clear) node->createChild("clear")->setValue(vec4(output.clearColour)); }
			}
			if (!info.depthOutputs.empty()) { auto const& output = info.depthOutputs.front(); auto node = pass->createChild("Depth"); node->createChild("image")->setValue(imageName(graph, output.image)); node->createChild("value")->setValue(graph.getValueId(output.image)); if (output.mipLevel) node->createChild("mipLevel")->setValue(output.mipLevel); if (output.cubeFace != GraphNoCubeFace) node->createChild("face")->setValue(output.cubeFace); node->createChild("load")->setValue(load(output.load)); node->createChild("store")->setValue(store(output.store)); if (output.load == GraphLoadOp::Clear) node->createChild("clear")->setValue(output.clearDepth); }
			// Emitted whenever the state is not the default, rather than only when
			// explicitState is set, so a configuration the author has temporarily
			// switched off is still preserved across a save/reload.
			if (!(info.rasterState == GraphRasterState{}))
			{
				auto const& state = info.rasterState;
				auto raster = pass->createChild("Raster");
				raster->createChild("explicit")->setValue(state.explicitState);
				raster->createChild("fill")->setValue(fillMode(state.fillMode));
				raster->createChild("frontFace")->setValue(frontFace(state.frontFace));
				raster->createChild("cull")->setValue(cullMode(state.cullMode));
				raster->createChild("depthTest")->setValue(state.depthTest);
				raster->createChild("depthWrite")->setValue(state.depthWrite);
				raster->createChild("depthCompare")->setValue(compareOp(state.depthCompare));
				raster->createChild("blend")->setValue(state.blend);
				raster->createChild("colourBlendOp")->setValue(blendOp(state.colourBlendOp));
				raster->createChild("alphaBlendOp")->setValue(blendOp(state.alphaBlendOp));
				raster->createChild("sourceColourBlend")->setValue(blendFactor(state.sourceColourBlend));
				raster->createChild("destinationColourBlend")->setValue(blendFactor(state.destinationColourBlend));
				raster->createChild("sourceAlphaBlend")->setValue(blendFactor(state.sourceAlphaBlend));
				raster->createChild("destinationAlphaBlend")->setValue(blendFactor(state.destinationAlphaBlend));
				raster->createChild("multisample")->setValue(state.multisample);
				raster->createChild("alphaToCoverage")->setValue(state.alphaToCoverage);
				raster->createChild("scissor")->setValue(state.scissor);
				raster->createChild("scissorRectangle")->setValue(uvec4(state.scissorRectangle));
				if (!state.colourWriteMasks.empty())
				{
					auto masks = raster->createChild("ColourWriteMasks");
					for (auto const& mask : state.colourWriteMasks) masks->createChild("Mask")->setValue(writeMask(mask));
				}
			}
		}
	}

	void RenderGraphSerializer::toFile(RenderGraph const& graph, std::string const& filepath)
	{
		StructuredDataWriteNode root("RenderGraph");
		toNode(graph, &root);
		detail::writeDocument(root.toStructuredData(), filepath);
	}
}
