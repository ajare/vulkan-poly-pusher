#include <algorithm>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <tuple>

#include "mpp/RenderGraph.h"
#include "mpp/Caps.h"
#include "mpp/MppException.h"

using namespace std;

namespace mpp
{
	namespace
	{
		bool isDepthFormat(GraphImageFormat format)
		{
			return format == GraphImageFormat::Depth16 || format == GraphImageFormat::Depth24 ||
				format == GraphImageFormat::Depth32f || format == GraphImageFormat::Depth24Stencil8 ||
				format == GraphImageFormat::Depth32fStencil8;
		}

		char const* formatName(GraphImageFormat format)
		{
			switch (format)
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

		uint32_t formatBits(GraphImageFormat format)
		{
			switch (format)
			{
			case GraphImageFormat::R8: return 8; case GraphImageFormat::Rg8: return 16; case GraphImageFormat::Rgba8: return 32;
			case GraphImageFormat::Srgb8Alpha8: return 32;
			case GraphImageFormat::R16f: return 16; case GraphImageFormat::Rg16f: return 32; case GraphImageFormat::Rgba16f: return 64;
			case GraphImageFormat::R32f: return 32; case GraphImageFormat::Rg32f: return 64; case GraphImageFormat::Rgba32f: return 128;
			case GraphImageFormat::R11g11b10f: return 32; case GraphImageFormat::Rgb10a2: return 32;
			case GraphImageFormat::Depth16: return 16; case GraphImageFormat::Depth24: return 24; case GraphImageFormat::Depth32f: return 32;
			case GraphImageFormat::Depth24Stencil8: return 32; case GraphImageFormat::Depth32fStencil8: return 64;
			}
			return 0;
		}

		char const* passTypeName(GraphPassType type)
		{
			switch (type) { case GraphPassType::Scene: return "scene"; case GraphPassType::Fullscreen: return "fullscreen"; default: return "present"; }
		}

		string descriptorError(GraphImageDesc const& desc)
		{
			bool const depth = isDepthFormat(desc.format);
			bool const colourUsage = hasGraphImageUsage(desc.usage, GraphImageUsage::ColourAttachment);
			bool const depthUsage = hasGraphImageUsage(desc.usage, GraphImageUsage::DepthAttachment);
			if (desc.mipLevels == 0 || (desc.absoluteSize.x == 0 && desc.relativeSize.x <= 0.0f) ||
				(desc.absoluteSize.y == 0 && desc.relativeSize.y <= 0.0f)) return "dimensions and mip count must be positive";
			if ((depth && (!depthUsage || colourUsage)) || (!depth && depthUsage)) return "format and attachment usage are incompatible";
			if (desc.depthCompare && (!depth || !hasGraphImageUsage(desc.usage, GraphImageUsage::Sampled))) return "depth comparison requires a sampled depth format";
			if (desc.shape == GraphImageShape::CubeMap)
			{
				if (desc.absoluteSize.x != desc.absoluteSize.y || desc.relativeSize.x != desc.relativeSize.y) return "cubemap faces must be square";
				if (depth && desc.format != GraphImageFormat::Depth24) return "depth cubemaps support only depth-only Depth24";
			}
			return {};
		}

		bool validAttachmentFace(GraphImageDesc const& desc, uint32_t face)
		{
			return desc.shape == GraphImageShape::CubeMap ? face < 6 : face == GraphNoCubeFace;
		}
	}

	char const* graphImageFormatName(GraphImageFormat format)
	{
		return formatName(format);
	}

	glm::uvec2 resolveGraphImageSize(GraphImageDesc const& desc, glm::uvec2 const& viewport)
	{
		return { desc.absoluteSize.x ? desc.absoluteSize.x : max(1u, (uint32_t)(viewport.x * desc.relativeSize.x)),
			desc.absoluteSize.y ? desc.absoluteSize.y : max(1u, (uint32_t)(viewport.y * desc.relativeSize.y)) };
	}

	uint32_t maxGraphImageMipLevels(glm::uvec2 const& size)
	{
		uint32_t levels = 1;
		for (uint32_t dimension = max(size.x, size.y); dimension > 1; dimension >>= 1) ++levels;
		return levels;
	}

	bool graphImagesCanAlias(GraphImageLifetime const& left, GraphImageLifetime const& right)
	{
		// usage is compared even though it does not change the texture that gets
		// created: sharing an allocation between images the graph declares
		// differently is not something to allow by omission. In the shipped
		// templates every non-external colour image is colourAttachment|sampled and
		// every depth image is depthAttachment|sampled, so this costs no aliasing
		// there -- and presentation images are external, hence never allocated here.
		return left.size == right.size && left.desc.format == right.desc.format &&
			left.desc.shape == right.desc.shape && left.desc.depthCompare == right.desc.depthCompare &&
			left.desc.usage == right.desc.usage &&
			left.desc.mipLevels == right.desc.mipLevels &&
			left.desc.colourSpace == right.desc.colourSpace &&
			left.desc.params.minFilter == right.desc.params.minFilter && left.desc.params.magFilter == right.desc.params.magFilter &&
			left.desc.params.wrap == right.desc.params.wrap && left.desc.params.useMipmaps == right.desc.params.useMipmaps &&
			left.desc.params.lodBaseLevel == right.desc.params.lodBaseLevel && left.desc.params.lodMaxLevel == right.desc.params.lodMaxLevel &&
			left.desc.params.lodBias == right.desc.params.lodBias && left.desc.params.maxAnisotropy == right.desc.params.maxAnisotropy;
	}

	struct RenderGraph::Image
	{
		string name;
		GraphImageDesc desc;
		string importName;
		uint32_t latestVersion{ 0 };
		// Producer pass per version. UINT32_MAX means imported/external version 0.
		vector<uint32_t> producers{ UINT32_MAX };
		vector<string> valueIds;
	};

	struct RenderGraph::Pass
	{
		string name;
		bool enabled{ true };
		GraphPassType type{ GraphPassType::Scene };
		string callbackFactory;
		string programResource;
		vector<GraphImageHandle> sampledInputs;
		vector<GraphSamplerBinding> samplerBindings;
		UniformCollection parameters;
		vector<GraphColourOutput> colourOutputs;
		vector<GraphDepthOutput> depthOutputs;
		GraphRasterState rasterState;
	};

	struct RenderGraph::PlanCache
	{
		struct DeviceCompileKey
		{
			glm::uvec2 viewport{ 0 };
			int maxTextureSize{ 0 };
			uint32_t maxColourAttachments{ 0 };
			uint32_t maxDrawBuffers{ 0 };
			bool operator <(DeviceCompileKey const& other) const
			{
				return tie(viewport.x, viewport.y, maxTextureSize, maxColourAttachments, maxDrawBuffers) <
					tie(other.viewport.x, other.viewport.y, other.maxTextureSize, other.maxColourAttachments, other.maxDrawBuffers);
			}
		};

		optional<RenderGraphCompileResult> topology;
		map<DeviceCompileKey, RenderGraphCompileResult> deviceCompiles;
		map<uint64_t, RenderGraphAllocationPlan> allocations;
	};

	RenderGraph::RenderGraph() = default;
	RenderGraph::~RenderGraph() = default;
	RenderGraph::RenderGraph(RenderGraph&&) noexcept = default;
	RenderGraph& RenderGraph::operator =(RenderGraph&&) noexcept = default;
	RenderGraph::RenderGraph(RenderGraph const& other)
		: mImages(other.mImages), mPasses(other.mPasses)
	{
	}
	RenderGraph& RenderGraph::operator =(RenderGraph const& other)
	{
		if (this == &other) return *this;
		mPlanCache.reset();
		mPlanCacheStats = {};
		mImages = other.mImages;
		mPasses = other.mPasses;
		return *this;
	}

	RenderGraph::PlanCache& RenderGraph::planCache() const
	{
		if (!mPlanCache) mPlanCache = make_unique<PlanCache>();
		return *mPlanCache;
	}

	void RenderGraph::invalidatePlanCache()
	{
		if (!mPlanCache) return;
		mPlanCache.reset();
		++mPlanCacheStats.invalidations;
	}

	bool RenderGraph::validImage(GraphImageHandle image) const
	{
		return image.isValid() && image.id < mImages.size() && image.version < mImages[image.id].producers.size();
	}

	bool RenderGraph::validPass(GraphPassHandle pass) const
	{
		return pass.isValid() && pass.id < mPasses.size();
	}

	GraphImageHandle RenderGraph::createImage(string const& name, GraphImageDesc const& desc)
	{
		if (name.empty()) THROW_MPP("Render graph image name must not be empty.", __LINE__, __FILE__, __func__);
		if (auto reason = descriptorError(desc); !reason.empty())
			THROW_MPP("Invalid render graph image '" + name + "': " + reason + ".", __LINE__, __FILE__, __func__);
		if (find_if(mImages.begin(), mImages.end(), [&](Image const& image) { return image.name == name; }) != mImages.end())
		{
			THROW_MPP("Duplicate render graph image name.", __LINE__, __FILE__, __func__);
		}
		Image image{ name, desc };
		image.valueIds.push_back(name + ".Import");
		mImages.push_back(move(image));
		invalidatePlanCache();
		return { (uint32_t)mImages.size() - 1, 0 };
	}

	void RenderGraph::setImageImportName(GraphImageHandle image, string const& importName)
	{
		if (!validImage(image) || !mImages[image.id].desc.external || importName.empty())
		{
			THROW_MPP("Render graph import requires an external image and name.", __LINE__, __FILE__, __func__);
		}
		mImages[image.id].importName = importName;
	}

	void RenderGraph::clearImageImportName(GraphImageHandle image)
	{
		if (!validImage(image)) THROW_MPP("Invalid render graph image handle.", __LINE__, __FILE__, __func__);
		mImages[image.id].importName.clear();
	}

	void RenderGraph::setImageName(GraphImageHandle image, string const& name)
	{
		if (!validImage(image) || name.empty() || any_of(mImages.begin(), mImages.end(), [&](Image const& value) { return value.name == name && &value != &mImages[image.id]; }))
			THROW_MPP("Invalid or duplicate render graph image name.", __LINE__, __FILE__, __func__);
		mImages[image.id].name = name;
		invalidatePlanCache();
	}

	void RenderGraph::setImageDesc(GraphImageHandle image,GraphImageDesc const& desc)
	{
		if(!validImage(image))THROW_MPP("Invalid render graph image handle.",__LINE__,__FILE__,__func__);
		if(auto reason=descriptorError(desc);!reason.empty())THROW_MPP("Invalid render graph image '"+mImages[image.id].name+"': "+reason+".",__LINE__,__FILE__,__func__);
		mImages[image.id].desc=desc;if(!desc.external)mImages[image.id].importName.clear();invalidatePlanCache();
	}

	void RenderGraph::removeImage(GraphImageHandle image)
	{
		if (!validImage(image) || image.version != 0) THROW_MPP("Invalid render graph image removal.", __LINE__, __FILE__, __func__);
		for (auto& pass : mPasses)
		{
			pass.sampledInputs.erase(remove_if(pass.sampledInputs.begin(), pass.sampledInputs.end(), [&](auto value) { return value.id == image.id; }), pass.sampledInputs.end());
			pass.samplerBindings.erase(remove_if(pass.samplerBindings.begin(), pass.samplerBindings.end(), [&](auto const& value) { return value.image.id == image.id; }), pass.samplerBindings.end());
			pass.colourOutputs.erase(remove_if(pass.colourOutputs.begin(), pass.colourOutputs.end(), [&](auto const& value) { return value.image.id == image.id; }), pass.colourOutputs.end());
			pass.depthOutputs.erase(remove_if(pass.depthOutputs.begin(), pass.depthOutputs.end(), [&](auto const& value) { return value.image.id == image.id; }), pass.depthOutputs.end());
			auto remap = [&](auto& values) { for (auto& value : values) if (value.id > image.id) --value.id; };
			remap(pass.sampledInputs);
			for (auto& value : pass.samplerBindings) if (value.image.id > image.id) --value.image.id;
			for (auto& value : pass.colourOutputs) if (value.image.id > image.id) --value.image.id;
			for (auto& value : pass.depthOutputs) if (value.image.id > image.id) --value.image.id;
		}
		mImages.erase(mImages.begin() + image.id);
		invalidatePlanCache();
	}

	GraphImageInfo RenderGraph::getImageInfo(GraphImageHandle image) const
	{
		if (!validImage(image)) THROW_MPP("Invalid render graph image handle.", __LINE__, __FILE__, __func__);
		auto const& source = mImages[image.id];
		return { source.name, source.desc, source.importName };
	}

	vector<GraphImageHandle> RenderGraph::getImportedImages() const
	{
		vector<GraphImageHandle> result;
		for (uint32_t id = 0; id < mImages.size(); ++id)
			if (mImages[id].desc.external) result.push_back({ id, 0 });
		return result;
	}

	GraphPassHandle RenderGraph::addPass(string const& name, GraphPassType type)
	{
		if (name.empty() || find_if(mPasses.begin(), mPasses.end(), [&](Pass const& pass) { return pass.name == name; }) != mPasses.end())
		{
			THROW_MPP("Invalid or duplicate render graph pass name.", __LINE__, __FILE__, __func__);
		}
		Pass pass;
		pass.name = name;
		pass.type = type;
		mPasses.push_back(move(pass));
		invalidatePlanCache();
		return { (uint32_t)mPasses.size() - 1 };
	}

	void RenderGraph::setPassEnabled(GraphPassHandle pass, bool enabled)
	{
		if (!validPass(pass)) THROW_MPP("Invalid render graph pass handle.", __LINE__, __FILE__, __func__);
		mPasses[pass.id].enabled = enabled;
		invalidatePlanCache();
	}

	void RenderGraph::setPassName(GraphPassHandle pass, string const& name)
	{
		if (!validPass(pass) || name.empty() || any_of(mPasses.begin(), mPasses.end(), [&](Pass const& value) { return value.name == name && &value != &mPasses[pass.id]; }))
			THROW_MPP("Invalid or duplicate render graph pass name.", __LINE__, __FILE__, __func__);
		mPasses[pass.id].name = name;
		invalidatePlanCache();
	}

	void RenderGraph::setPassProgramResource(GraphPassHandle pass, string const& resourceName)
	{
		if (!validPass(pass) || resourceName.empty())
		{
			THROW_MPP("Render graph pass program resource requires a valid pass and name.", __LINE__, __FILE__, __func__);
		}
		mPasses[pass.id].programResource = resourceName;
	}

	void RenderGraph::clearPassProgramResource(GraphPassHandle pass)
	{
		if (!validPass(pass)) THROW_MPP("Invalid render graph pass handle.", __LINE__, __FILE__, __func__);
		mPasses[pass.id].programResource.clear();
	}

	void RenderGraph::setPassCallbackFactory(GraphPassHandle pass, string const& factoryName)
	{
		if (!validPass(pass) || factoryName.empty())
		{
			THROW_MPP("Render graph pass callback factory requires a valid pass and name.", __LINE__, __FILE__, __func__);
		}
		mPasses[pass.id].callbackFactory = factoryName;
	}

	void RenderGraph::setPassRasterState(GraphPassHandle pass, GraphRasterState const& state)
	{
		if (!validPass(pass)) THROW_MPP("Invalid render graph pass handle.", __LINE__, __FILE__, __func__);
		mPasses[pass.id].rasterState = state;
	}

	void RenderGraph::removeProducedValue(GraphImageHandle image)
	{
		if (!validImage(image) || image.version == 0) return;
		for (auto& pass : mPasses)
		{
			pass.sampledInputs.erase(remove_if(pass.sampledInputs.begin(), pass.sampledInputs.end(), [&](auto value) { return value.id == image.id && value.version == image.version; }), pass.sampledInputs.end());
			pass.samplerBindings.erase(remove_if(pass.samplerBindings.begin(), pass.samplerBindings.end(), [&](auto const& value) { return value.image.id == image.id && value.image.version == image.version; }), pass.samplerBindings.end());
			for (auto& value : pass.sampledInputs) if (value.id == image.id && value.version > image.version) --value.version;
			for (auto& value : pass.samplerBindings) if (value.image.id == image.id && value.image.version > image.version) --value.image.version;
			for (auto& value : pass.colourOutputs) if (value.image.id == image.id && value.image.version > image.version) --value.image.version;
			for (auto& value : pass.depthOutputs) if (value.image.id == image.id && value.image.version > image.version) --value.image.version;
		}
		auto& target = mImages[image.id];
		target.producers.erase(target.producers.begin() + image.version);
		target.valueIds.erase(target.valueIds.begin() + image.version);
		--target.latestVersion;
	}

	void RenderGraph::removePass(GraphPassHandle pass)
	{
		if (!validPass(pass)) THROW_MPP("Invalid render graph pass removal.", __LINE__, __FILE__, __func__);
		vector<GraphImageHandle> outputs;
		for (auto const& value : mPasses[pass.id].colourOutputs) outputs.push_back(value.image);
		for (auto const& value : mPasses[pass.id].depthOutputs) outputs.push_back(value.image);
		sort(outputs.begin(), outputs.end(), [](auto left, auto right) { return left.id == right.id ? left.version > right.version : left.id > right.id; });
		for (auto value : outputs) removeProducedValue(value);
		mPasses.erase(mPasses.begin() + pass.id);
		for (auto& image : mImages) for (auto& producer : image.producers) if (producer != UINT32_MAX && producer > pass.id) --producer;
		invalidatePlanCache();
	}

	GraphPassHandle RenderGraph::duplicatePass(GraphPassHandle pass, string const& name)
	{
		if (!validPass(pass)) THROW_MPP("Invalid render graph pass duplication.", __LINE__, __FILE__, __func__);
		auto source = mPasses[pass.id];
		auto colour = source.colourOutputs;
		auto depth = source.depthOutputs;
		source.name = name;
		source.colourOutputs.clear();
		source.depthOutputs.clear();
		if (name.empty() || any_of(mPasses.begin(), mPasses.end(), [&](Pass const& value) { return value.name == name; })) THROW_MPP("Invalid or duplicate render graph pass name.", __LINE__, __FILE__, __func__);
		mPasses.push_back(move(source));
		invalidatePlanCache();
		GraphPassHandle result{ static_cast<uint32_t>(mPasses.size() - 1) };
		for (auto const& value : colour) writeColour(result, { value.image.id, mImages[value.image.id].latestVersion }, value.load, value.store, value.clearColour, value.mipLevel, value.cubeFace);
		for (auto const& value : depth) writeDepth(result, { value.image.id, mImages[value.image.id].latestVersion }, value.load, value.store, value.clearDepth, value.mipLevel, value.cubeFace);
		return result;
	}

	void RenderGraph::movePass(GraphPassHandle pass, uint32_t destination)
	{
		if (!validPass(pass) || destination >= mPasses.size()) THROW_MPP("Invalid render graph pass move.", __LINE__, __FILE__, __func__);
		vector<GraphPassHandle> order;
		for (uint32_t index = 0; index < mPasses.size(); ++index) if (index != pass.id) order.push_back({ index });
		order.insert(order.begin() + destination, pass);
		reorderPasses(order);
	}

	void RenderGraph::readSampled(GraphPassHandle pass, GraphImageHandle image)
	{
		if (!validPass(pass) || !validImage(image))
		{
			THROW_MPP("Invalid render graph sampled input.", __LINE__, __FILE__, __func__);
		}
		mPasses[pass.id].sampledInputs.push_back(image);
		invalidatePlanCache();
	}

	void RenderGraph::bindSampler(GraphPassHandle pass, string const& sampler, GraphImageHandle image, uint32_t mipLevel)
	{
		if (!validPass(pass) || !validImage(image) || sampler.empty() || (mipLevel != UINT32_MAX && mipLevel >= mImages[image.id].desc.mipLevels))
		{
			THROW_MPP("Invalid render graph sampler binding.", __LINE__, __FILE__, __func__);
		}
		auto& bindings = mPasses[pass.id].samplerBindings;
		if (find_if(bindings.begin(), bindings.end(), [&](GraphSamplerBinding const& binding) { return binding.sampler == sampler; }) != bindings.end())
		{
			THROW_MPP("Duplicate render graph sampler binding.", __LINE__, __FILE__, __func__);
		}
		readSampled(pass, image);
		bindings.push_back({ sampler, image, mipLevel });
	}

	void RenderGraph::setPassParameters(GraphPassHandle pass, UniformCollection const& parameters)
	{
		if (!validPass(pass)) THROW_MPP("Invalid render graph pass handle.", __LINE__, __FILE__, __func__);
		mPasses[pass.id].parameters = parameters;
	}

	GraphImageHandle RenderGraph::writeColour(GraphPassHandle pass, GraphImageHandle image, GraphLoadOp load, GraphStoreOp store, glm::vec4 const& clear, uint32_t mipLevel, uint32_t cubeFace)
	{
		if (!validPass(pass) || !validImage(image) || image.version != mImages[image.id].latestVersion || mipLevel >= mImages[image.id].desc.mipLevels || !validAttachmentFace(mImages[image.id].desc, cubeFace) ||
			!hasGraphImageUsage(mImages[image.id].desc.usage, GraphImageUsage::ColourAttachment) || isDepthFormat(mImages[image.id].desc.format))
		{
			THROW_MPP("Invalid render graph colour output.", __LINE__, __FILE__, __func__);
		}
		auto& target = mImages[image.id];
		GraphImageHandle output{ image.id, ++target.latestVersion };
		target.producers.push_back(pass.id);
		target.valueIds.push_back(target.name + ".v" + to_string(output.version));
		mPasses[pass.id].colourOutputs.push_back({ output, mipLevel, load, store, clear, cubeFace });
		invalidatePlanCache();
		return output;
	}

	GraphImageHandle RenderGraph::writeDepth(GraphPassHandle pass, GraphImageHandle image, GraphLoadOp load, GraphStoreOp store, float clear, uint32_t mipLevel, uint32_t cubeFace)
	{
		if (!validPass(pass) || !validImage(image) || image.version != mImages[image.id].latestVersion || mipLevel >= mImages[image.id].desc.mipLevels || !validAttachmentFace(mImages[image.id].desc, cubeFace) ||
			!hasGraphImageUsage(mImages[image.id].desc.usage, GraphImageUsage::DepthAttachment) || !isDepthFormat(mImages[image.id].desc.format))
		{
			THROW_MPP("Invalid render graph depth output.", __LINE__, __FILE__, __func__);
		}
		auto& target = mImages[image.id];
		GraphImageHandle output{ image.id, ++target.latestVersion };
		target.producers.push_back(pass.id);
		target.valueIds.push_back(target.name + ".v" + to_string(output.version));
		mPasses[pass.id].depthOutputs.push_back({ output, mipLevel, load, store, clear, cubeFace });
		invalidatePlanCache();
		return output;
	}

	void RenderGraph::setColourOutput(GraphPassHandle pass, size_t output, GraphLoadOp load, GraphStoreOp store, glm::vec4 const& clear, uint32_t mipLevel, uint32_t cubeFace)
	{
		if (!validPass(pass) || output >= mPasses[pass.id].colourOutputs.size()) THROW_MPP("Invalid render graph colour output edit.", __LINE__, __FILE__, __func__);
		auto& value = mPasses[pass.id].colourOutputs[output];
		if (mipLevel >= mImages[value.image.id].desc.mipLevels || !validAttachmentFace(mImages[value.image.id].desc, cubeFace)) THROW_MPP("Invalid render graph colour output subresource.", __LINE__, __FILE__, __func__);
		value.load = load; value.store = store; value.clearColour = clear; value.mipLevel = mipLevel; value.cubeFace = cubeFace;
		invalidatePlanCache();
	}

	GraphImageHandle RenderGraph::retargetColourOutput(GraphPassHandle pass, size_t output, GraphImageHandle image)
	{
		if (!validPass(pass) || output >= mPasses[pass.id].colourOutputs.size() || !validImage(image) || image.version != mImages[image.id].latestVersion || !hasGraphImageUsage(mImages[image.id].desc.usage, GraphImageUsage::ColourAttachment) || isDepthFormat(mImages[image.id].desc.format)) THROW_MPP("Invalid render graph colour output target.", __LINE__, __FILE__, __func__);
		auto value = mPasses[pass.id].colourOutputs[output]; auto old = value.image;
		if (old.id == image.id) return old;
		auto stableId = getValueId(old); auto replacement = writeColour(pass, image, value.load, value.store, value.clearColour, value.mipLevel, value.cubeFace);
		mPasses[pass.id].colourOutputs.pop_back(); mPasses[pass.id].colourOutputs[output].image = replacement;
		for (auto& candidate : mPasses) { for (auto& input : candidate.sampledInputs) if (input.id == old.id && input.version == old.version) input = replacement; for (auto& binding : candidate.samplerBindings) if (binding.image.id == old.id && binding.image.version == old.version) binding.image = replacement; }
		removeProducedValue(old); setValueId(replacement, stableId); return replacement;
	}

	void RenderGraph::removeColourOutput(GraphPassHandle pass, size_t output)
	{
		if (!validPass(pass) || output >= mPasses[pass.id].colourOutputs.size()) THROW_MPP("Invalid render graph colour output removal.", __LINE__, __FILE__, __func__);
		auto image = mPasses[pass.id].colourOutputs[output].image;
		mPasses[pass.id].colourOutputs.erase(mPasses[pass.id].colourOutputs.begin() + output);
		removeProducedValue(image);
		invalidatePlanCache();
	}

	void RenderGraph::setDepthOutput(GraphPassHandle pass, size_t output, GraphLoadOp load, GraphStoreOp store, float clear, uint32_t mipLevel, uint32_t cubeFace)
	{
		if (!validPass(pass) || output >= mPasses[pass.id].depthOutputs.size()) THROW_MPP("Invalid render graph depth output edit.", __LINE__, __FILE__, __func__);
		auto& value = mPasses[pass.id].depthOutputs[output];
		if (mipLevel >= mImages[value.image.id].desc.mipLevels || !validAttachmentFace(mImages[value.image.id].desc, cubeFace)) THROW_MPP("Invalid render graph depth output subresource.", __LINE__, __FILE__, __func__);
		value.load = load; value.store = store; value.clearDepth = clear; value.mipLevel = mipLevel; value.cubeFace = cubeFace;
		invalidatePlanCache();
	}

	GraphImageHandle RenderGraph::retargetDepthOutput(GraphPassHandle pass, size_t output, GraphImageHandle image)
	{
		if (!validPass(pass) || output >= mPasses[pass.id].depthOutputs.size() || !validImage(image) || image.version != mImages[image.id].latestVersion || !hasGraphImageUsage(mImages[image.id].desc.usage, GraphImageUsage::DepthAttachment) || !isDepthFormat(mImages[image.id].desc.format)) THROW_MPP("Invalid render graph depth output target.", __LINE__, __FILE__, __func__);
		auto value = mPasses[pass.id].depthOutputs[output]; auto old = value.image;
		if (old.id == image.id) return old;
		auto stableId = getValueId(old); auto replacement = writeDepth(pass, image, value.load, value.store, value.clearDepth, value.mipLevel, value.cubeFace);
		mPasses[pass.id].depthOutputs.pop_back(); mPasses[pass.id].depthOutputs[output].image = replacement;
		for (auto& candidate : mPasses) { for (auto& input : candidate.sampledInputs) if (input.id == old.id && input.version == old.version) input = replacement; for (auto& binding : candidate.samplerBindings) if (binding.image.id == old.id && binding.image.version == old.version) binding.image = replacement; }
		removeProducedValue(old); setValueId(replacement, stableId); return replacement;
	}

	void RenderGraph::removeDepthOutput(GraphPassHandle pass, size_t output)
	{
		if (!validPass(pass) || output >= mPasses[pass.id].depthOutputs.size()) THROW_MPP("Invalid render graph depth output removal.", __LINE__, __FILE__, __func__);
		auto image = mPasses[pass.id].depthOutputs[output].image;
		mPasses[pass.id].depthOutputs.erase(mPasses[pass.id].depthOutputs.begin() + output);
		removeProducedValue(image);
		invalidatePlanCache();
	}

	void RenderGraph::setSamplerBinding(GraphPassHandle pass, size_t binding, string const& sampler, GraphImageHandle image, uint32_t mipLevel)
	{
		if (!validPass(pass) || binding >= mPasses[pass.id].samplerBindings.size() || !validImage(image) || sampler.empty() || (mipLevel != UINT32_MAX && mipLevel >= mImages[image.id].desc.mipLevels))
			THROW_MPP("Invalid render graph sampler binding edit.", __LINE__, __FILE__, __func__);
		auto& source = mPasses[pass.id];
		for (size_t index = 0; index < source.samplerBindings.size(); ++index) if (index != binding && source.samplerBindings[index].sampler == sampler) THROW_MPP("Duplicate render graph sampler binding.", __LINE__, __FILE__, __func__);
		auto old = source.samplerBindings[binding].image;
		auto sampled = find_if(source.sampledInputs.begin(), source.sampledInputs.end(), [&](auto value) { return value.id == old.id && value.version == old.version; });
		if (sampled != source.sampledInputs.end()) *sampled = image; else source.sampledInputs.push_back(image);
		source.samplerBindings[binding] = { sampler, image, mipLevel };
		invalidatePlanCache();
	}

	void RenderGraph::removeSamplerBinding(GraphPassHandle pass, size_t binding)
	{
		if (!validPass(pass) || binding >= mPasses[pass.id].samplerBindings.size()) THROW_MPP("Invalid render graph sampler binding removal.", __LINE__, __FILE__, __func__);
		auto& source = mPasses[pass.id]; auto image = source.samplerBindings[binding].image;
		source.samplerBindings.erase(source.samplerBindings.begin() + binding);
		if (none_of(source.samplerBindings.begin(), source.samplerBindings.end(), [&](auto const& value) { return value.image.id == image.id && value.image.version == image.version; }))
			source.sampledInputs.erase(remove_if(source.sampledInputs.begin(), source.sampledInputs.end(), [&](auto value) { return value.id == image.id && value.version == image.version; }), source.sampledInputs.end());
		invalidatePlanCache();
	}

	void RenderGraph::setValueId(GraphImageHandle image, string const& valueId)
	{
		if (!validImage(image) || valueId.empty()) THROW_MPP("Invalid render graph value ID.", __LINE__, __FILE__, __func__);
		if (mImages[image.id].valueIds[image.version] == valueId) return;
		for (auto const& candidate : mImages)
			if (find(candidate.valueIds.begin(), candidate.valueIds.end(), valueId) != candidate.valueIds.end())
				THROW_MPP("Duplicate render graph value ID.", __LINE__, __FILE__, __func__);
		mImages[image.id].valueIds[image.version] = valueId;
		invalidatePlanCache();
	}

	string const& RenderGraph::getValueId(GraphImageHandle image) const
	{
		if (!validImage(image)) THROW_MPP("Invalid render graph image handle.", __LINE__, __FILE__, __func__);
		return mImages[image.id].valueIds[image.version];
	}

	GraphImageHandle RenderGraph::findValue(string const& valueId) const
	{
		for (uint32_t image = 0; image < mImages.size(); ++image)
		{
			auto const found = find(mImages[image].valueIds.begin(), mImages[image].valueIds.end(), valueId);
			if (found != mImages[image].valueIds.end()) return { image, static_cast<uint32_t>(found - mImages[image].valueIds.begin()) };
		}
		return {};
	}

	size_t RenderGraph::getImageCount() const
	{
		return mImages.size();
	}

	size_t RenderGraph::getImageVersionCount(uint32_t imageId) const
	{
		if(imageId>=mImages.size())THROW_MPP("Invalid render graph image ID.",__LINE__,__FILE__,__func__);return mImages[imageId].producers.size();
	}

	size_t RenderGraph::getPassCount() const
	{
		return mPasses.size();
	}

	GraphPassInfo RenderGraph::getPassInfo(GraphPassHandle pass) const
	{
		if (!validPass(pass))
		{
			THROW_MPP("Invalid render graph pass handle.", __LINE__, __FILE__, __func__);
		}
		auto const& source = mPasses[pass.id];
		return { source.name, source.enabled, source.type, source.callbackFactory, source.programResource, source.sampledInputs, source.samplerBindings, source.parameters, source.colourOutputs, source.depthOutputs, source.rasterState };
	}

	RenderGraphCompileResult RenderGraph::compile() const
	{
		auto& cache = planCache();
		if (cache.topology)
		{
			++mPlanCacheStats.compileHits;
			return *cache.topology;
		}
		++mPlanCacheStats.compileMisses;
		RenderGraphCompileResult result;
		for (uint32_t passId = 0; passId < mPasses.size(); ++passId)
		{
			auto const& pass = mPasses[passId];
			if (!pass.enabled) continue;
			auto error = [&](string message, GraphImageHandle image = GraphImageHandle{})
			{
				result.diagnostics.push_back(message);
				result.messages.push_back({ GraphCompileMessageSeverity::Error, "RG-COMPILER-ERROR", std::move(message), { passId }, image });
			};
			for (auto const& input : pass.sampledInputs)
			{
				if (!validImage(input))
				{
					error("Pass '" + pass.name + "' reads an invalid image handle.");
					continue;
				}
				auto const& image = mImages[input.id];
				if (!hasGraphImageUsage(image.desc.usage, GraphImageUsage::Sampled))
				{
					error("Pass '" + pass.name + "' samples image '" + image.name + "' without Sampled usage.", input);
				}
				uint32_t producer = image.producers[input.version];
				if (producer == UINT32_MAX && !image.desc.external)
				{
					error("Pass '" + pass.name + "' reads unwritten non-external image '" + image.name + "'.", input);
				}
				else if (producer != UINT32_MAX && producer != passId)
				{
					if (!mPasses[producer].enabled)
						error("Pass '" + pass.name + "' reads value '" + image.valueIds[input.version] + "' produced by disabled pass '" + mPasses[producer].name + "'.", input);
				}
				else if (producer == passId)
				{
					error("Pass '" + pass.name + "' reads and writes the same image version.", input);
				}
			}

			auto effectiveSize = [&](GraphImageDesc const& desc, uint32_t mip)
			{
				glm::vec2 size = desc.absoluteSize.x && desc.absoluteSize.y ? glm::vec2(desc.absoluteSize) : desc.relativeSize;
				return size / (float)(1u << mip);
			};
			// A transient image is allowed to share an allocation with another whose
			// lifetime does not overlap, so at pass start it holds whatever the
			// previous occupant left. clearPassOutputs honours Clear and lets both
			// Load and DontCare through as "keep what is there", so Load on such an
			// image reads contents that change with the aliasing decision.
			auto checkTransientLoad = [&](GraphImageHandle const& handle, GraphLoadOp load, char const* attachment)
			{
				if (load != GraphLoadOp::Load) return;
				auto const& image = mImages[handle.id];
				if (image.desc.external || !image.desc.transient || handle.version == 0) return;
				auto const producer = image.producers[handle.version - 1];
				if (producer != UINT32_MAX && mPasses[producer].enabled) return;
				error("Pass '" + pass.name + "' loads " + attachment + " image '" + image.name +
					"', which is transient with no producer, so its contents depend on which image the allocator aliased it onto. Clear it, mark the image non-transient, or produce it in another pass.", handle);
			};
			for (auto const& output : pass.colourOutputs) checkTransientLoad(output.image, output.load, "colour");
			for (auto const& output : pass.depthOutputs) checkTransientLoad(output.image, output.load, "depth");

			if (pass.colourOutputs.size() > 1)
			{
				auto const& firstOutput = pass.colourOutputs.front();
				auto const& first = mImages[firstOutput.image.id].desc;
				for (auto const& output : pass.colourOutputs)
				{
					auto const& desc = mImages[output.image.id].desc;
					if (desc.shape != first.shape || effectiveSize(desc, output.mipLevel) != effectiveSize(first, firstOutput.mipLevel))
					{
						error("MRT pass '" + pass.name + "' has incompatible colour attachment mip dimensions.", output.image);
					}
				}
			}
			if (pass.depthOutputs.size() > 1)
			{
				error("Pass '" + pass.name + "' declares more than one depth output.");
			}
			if (!pass.colourOutputs.empty() && !pass.depthOutputs.empty())
			{
				auto const& colourOutput = pass.colourOutputs.front();
				auto const& depthOutput = pass.depthOutputs.front();
				auto const& colour = mImages[colourOutput.image.id].desc;
				auto const& depth = mImages[depthOutput.image.id].desc;
				if (colour.shape != depth.shape || effectiveSize(colour, colourOutput.mipLevel) != effectiveSize(depth, depthOutput.mipLevel))
				{
					error("Pass '" + pass.name + "' has incompatible colour and depth attachment dimensions.", colourOutput.image);
				}
			}
		}

		if (!result.diagnostics.empty())
		{
			cache.topology = result;
			return result;
		}

		auto dependencyOrder = buildDependencyOrder();
		if (!dependencyOrder.valid)
		{
			result.diagnostics = std::move(dependencyOrder.diagnostics);
			result.messages = std::move(dependencyOrder.messages);
			cache.topology = result;
			return result;
		}
		result.passOrder = std::move(dependencyOrder.passOrder);

		vector<uint32_t> consumerCounts;
		for (auto const& image : mImages) consumerCounts.insert(consumerCounts.end(), image.producers.size(), 0);
		vector<size_t> imageOffsets(mImages.size());
		size_t offset = 0;
		for (size_t image = 0; image < mImages.size(); ++image) { imageOffsets[image] = offset; offset += mImages[image].producers.size(); }
		auto countConsumer = [&](GraphImageHandle value)
		{
			if (validImage(value)) ++consumerCounts[imageOffsets[value.id] + value.version];
		};
		for (uint32_t passId = 0; passId < mPasses.size(); ++passId)
		{
			auto const& pass = mPasses[passId];
			if (!pass.enabled)
			{
				result.messages.push_back({ GraphCompileMessageSeverity::Information, "RG-COMPILER-DISABLED-PASS",
					"Pass '" + pass.name + "' is disabled and omitted from execution.", { passId }, {} });
				continue;
			}
			for (auto input : pass.sampledInputs) countConsumer(input);
			for (auto const& output : pass.colourOutputs) if (output.load == GraphLoadOp::Load && output.image.version) countConsumer({ output.image.id, output.image.version - 1 });
			for (auto const& output : pass.depthOutputs) if (output.load == GraphLoadOp::Load && output.image.version) countConsumer({ output.image.id, output.image.version - 1 });
		}
		bool hasExplicitRoots = false;
		vector<bool> livePasses(mPasses.size(), false);
		vector<uint32_t> liveStack;
		auto rootOutput = [&](GraphImageHandle value)
		{
			auto const& image = mImages[value.id];
			// Usage belongs to the logical image, but only its latest SSA value is
			// externally observable. Earlier writes stay live through real sampled/load
			// dependencies instead of becoming roots merely because they share a name.
			return value.version + 1 == image.producers.size() && (image.desc.external || hasGraphImageUsage(image.desc.usage, GraphImageUsage::Presentation) || hasGraphImageUsage(image.desc.usage, GraphImageUsage::Exported));
		};
		for (uint32_t passId = 0; passId < mPasses.size(); ++passId)
		{
			auto const& pass = mPasses[passId];
			if (!pass.enabled) continue;
			bool root = any_of(pass.colourOutputs.begin(), pass.colourOutputs.end(), [&](auto const& output) { return rootOutput(output.image); }) ||
				any_of(pass.depthOutputs.begin(), pass.depthOutputs.end(), [&](auto const& output) { return rootOutput(output.image); });
			if (root) { hasExplicitRoots = true; livePasses[passId] = true; liveStack.push_back(passId); }
		}
		if (hasExplicitRoots)
		{
			auto retainProducer = [&](GraphImageHandle value)
			{
				if (!validImage(value)) return;
				auto const producer = mImages[value.id].producers[value.version];
				if (producer != UINT32_MAX && mPasses[producer].enabled && !livePasses[producer]) { livePasses[producer] = true; liveStack.push_back(producer); }
			};
			while (!liveStack.empty())
			{
				auto const passId = liveStack.back(); liveStack.pop_back(); auto const& pass = mPasses[passId];
				for (auto input : pass.sampledInputs) retainProducer(input);
				for (auto const& output : pass.colourOutputs) if (output.load == GraphLoadOp::Load && output.image.version) retainProducer({ output.image.id, output.image.version - 1 });
				for (auto const& output : pass.depthOutputs) if (output.load == GraphLoadOp::Load && output.image.version) retainProducer({ output.image.id, output.image.version - 1 });
			}
		}
		for (uint32_t passId = 0; passId < mPasses.size(); ++passId)
		{
			auto const& pass = mPasses[passId];
			if (!pass.enabled) continue;
			auto inspectOutput = [&](GraphImageHandle value, GraphStoreOp store)
			{
				auto const& image = mImages[value.id];
				if (consumerCounts[imageOffsets[value.id] + value.version] || rootOutput(value)) return;
				result.unusedOutputs.push_back(value);
				auto const severity = store == GraphStoreOp::Store ? GraphCompileMessageSeverity::Warning : GraphCompileMessageSeverity::Information;
				result.messages.push_back({ severity, "RG-COMPILER-UNUSED-OUTPUT",
					"Pass '" + pass.name + "' writes unused value '" + image.valueIds[value.version] + "'.", { passId }, value });
			};
			for (auto const& output : pass.colourOutputs) inspectOutput(output.image, output.store);
			for (auto const& output : pass.depthOutputs) inspectOutput(output.image, output.store);
			bool cullable = hasExplicitRoots ? !livePasses[passId] : (!pass.colourOutputs.empty() || !pass.depthOutputs.empty());
			if (!hasExplicitRoots)
			{
				auto retainedOutput = [&](GraphImageHandle value, GraphStoreOp store)
				{
					return store == GraphStoreOp::Store || consumerCounts[imageOffsets[value.id] + value.version] || rootOutput(value);
				};
				for (auto const& output : pass.colourOutputs) cullable = cullable && !retainedOutput(output.image, output.store);
				for (auto const& output : pass.depthOutputs) cullable = cullable && !retainedOutput(output.image, output.store);
			}
			if (cullable)
			{
				result.culledPasses.push_back({ passId });
				result.messages.push_back({ GraphCompileMessageSeverity::Information, "RG-COMPILER-DEAD-PASS",
					"Pass '" + pass.name + (hasExplicitRoots ? "' is culled because it does not contribute to an exported or presentation output." : "' is culled because all of its outputs are unused and discarded."), { passId }, {} });
			}
		}
		if (!result.culledPasses.empty())
			result.passOrder.erase(remove_if(result.passOrder.begin(), result.passOrder.end(), [&](GraphPassHandle pass)
			{
				return find_if(result.culledPasses.begin(), result.culledPasses.end(), [&](GraphPassHandle culled) { return culled.id == pass.id; }) != result.culledPasses.end();
			}), result.passOrder.end());
		vector<GraphPassHandle> authoredExecutionOrder;
		for (uint32_t pass = 0; pass < mPasses.size(); ++pass)
			if (mPasses[pass].enabled && none_of(result.culledPasses.begin(), result.culledPasses.end(), [&](GraphPassHandle culled) { return culled.id == pass; })) authoredExecutionOrder.push_back({ pass });
		for (size_t position = 0; position < result.passOrder.size(); ++position)
		{
			auto const pass = result.passOrder[position];
			if (authoredExecutionOrder[position].id != pass.id)
			{
				auto const authored = find_if(authoredExecutionOrder.begin(), authoredExecutionOrder.end(), [&](GraphPassHandle value) { return value.id == pass.id; });
				result.messages.push_back({ GraphCompileMessageSeverity::Information, "RG-COMPILER-REORDERED-PASS",
					"Pass '" + mPasses[pass.id].name + "' moves from enabled authored position " + to_string(authored - authoredExecutionOrder.begin()) + " to dependency position " + to_string(position) + ".", pass, {} });
			}
		}
		result.valid = true;
		cache.topology = result;
		return result;
	}

	RenderGraphCompileResult RenderGraph::buildDependencyOrder() const
	{
		RenderGraphCompileResult result;
		vector<set<uint32_t>> edges(mPasses.size());
		vector<uint32_t> indegree(mPasses.size());
		size_t enabledCount = 0;
		for (uint32_t passId = 0; passId < mPasses.size(); ++passId)
		{
			auto const& pass = mPasses[passId];
			if (!pass.enabled) continue;
			++enabledCount;
			auto error = [&](string message, GraphImageHandle image = GraphImageHandle{})
			{
				result.diagnostics.push_back(message);
				result.messages.push_back({ GraphCompileMessageSeverity::Error, "RG-COMPILER-DEPENDENCY", std::move(message), { passId }, image });
			};
			auto addDependency = [&](GraphImageHandle input, char const* operation, bool allowUnwritten)
			{
				if (!validImage(input))
				{
					error("Pass '" + pass.name + "' " + operation + " an invalid image handle.");
					return;
				}
				auto const producer = mImages[input.id].producers[input.version];
				if (producer == UINT32_MAX)
				{
					if (allowUnwritten) return;
					if (!mImages[input.id].desc.external)
						error("Pass '" + pass.name + "' " + operation + " unwritten non-external value '" + mImages[input.id].valueIds[input.version] + "'.", input);
				}
				else if (producer == passId)
					error("Pass '" + pass.name + "' reads and writes the same image value.", input);
				else if (!mPasses[producer].enabled)
					error("Pass '" + pass.name + "' depends on disabled pass '" + mPasses[producer].name + "'.", input);
				else if (edges[producer].insert(passId).second)
					++indegree[passId];
			};
			for (auto const& input : pass.sampledInputs) addDependency(input, "reads", false);
			for (auto const& output : pass.colourOutputs) if (output.load == GraphLoadOp::Load && output.image.version) addDependency({ output.image.id, output.image.version - 1 }, "loads", true);
			for (auto const& output : pass.depthOutputs) if (output.load == GraphLoadOp::Load && output.image.version) addDependency({ output.image.id, output.image.version - 1 }, "loads", true);
		}
		if (!result.diagnostics.empty()) return result;
		priority_queue<uint32_t, vector<uint32_t>, greater<uint32_t>> ready;
		for (uint32_t pass = 0; pass < mPasses.size(); ++pass)
			if (mPasses[pass].enabled && indegree[pass] == 0) ready.push(pass);
		while (!ready.empty())
		{
			auto const pass = ready.top(); ready.pop();
			result.passOrder.push_back({ pass });
			for (auto const next : edges[pass]) if (--indegree[next] == 0) ready.push(next);
		}
		if (result.passOrder.size() != enabledCount)
		{
			auto message = string("Render graph contains a pass dependency cycle.");
			result.diagnostics.push_back(message);
			result.messages.push_back({ GraphCompileMessageSeverity::Error, "RG-COMPILER-CYCLE", std::move(message), {}, {} });
			result.passOrder.clear();
			return result;
		}
		result.valid = true;
		return result;
	}

	void RenderGraph::reorderPasses(vector<GraphPassHandle> const& order)
	{
		if(order.size()!=mPasses.size())THROW_MPP("Render graph pass order must include every pass.",__LINE__,__FILE__,__func__);vector<uint32_t> oldToNew(mPasses.size(),UINT32_MAX);for(uint32_t next=0;next<order.size();++next){if(!validPass(order[next])||oldToNew[order[next].id]!=UINT32_MAX)THROW_MPP("Render graph pass order contains an invalid or duplicate pass.",__LINE__,__FILE__,__func__);oldToNew[order[next].id]=next;}vector<Pass> reordered;reordered.reserve(mPasses.size());for(auto handle:order)reordered.push_back(std::move(mPasses[handle.id]));mPasses=std::move(reordered);for(auto& image:mImages)for(auto& producer:image.producers)if(producer!=UINT32_MAX)producer=oldToNew[producer];invalidatePlanCache();
	}

	RenderGraphAllocationPlan RenderGraph::buildAllocationPlan(glm::uvec2 const& viewport) const
	{
		auto& cache = planCache();
		auto const cacheKey = (static_cast<uint64_t>(viewport.x) << 32) | viewport.y;
		auto const cached = cache.allocations.find(cacheKey);
		if (cached != cache.allocations.end())
		{
			++mPlanCacheStats.allocationHits;
			return cached->second;
		}
		++mPlanCacheStats.allocationMisses;
		auto cachePlan = [&](RenderGraphAllocationPlan const& value)
		{
			cache.allocations.emplace(cacheKey, value);
			return value;
		};
		RenderGraphAllocationPlan plan;
		if (viewport.x == 0 || viewport.y == 0)
		{
			plan.diagnostics.push_back("Render graph allocation viewport must be non-zero.");
			return cachePlan(plan);
		}
		auto compiled = compile();
		if (!compiled.valid)
		{
			plan.diagnostics = move(compiled.diagnostics);
			return cachePlan(plan);
		}

		vector<vector<uint32_t>> allocationIndex;
		allocationIndex.reserve(mImages.size());
		for (auto const& image : mImages)
			allocationIndex.emplace_back(image.producers.size(), UINT32_MAX);

		auto markImage = [&](GraphImageHandle handle, uint32_t passPosition)
		{
			auto const& image = mImages[handle.id];
			if (image.desc.external)
			{
				if (find_if(plan.importedImages.begin(), plan.importedImages.end(), [&](GraphImageHandle current)
					{ return current.id == handle.id && current.version == handle.version; }) == plan.importedImages.end())
				{
					plan.importedImages.push_back(handle);
				}
				return;
			}
			auto& index = allocationIndex[handle.id][handle.version];
			if (index == UINT32_MAX)
			{
				GraphImageLifetime lifetime;
				lifetime.image = handle;
				lifetime.debugName = image.name;
				lifetime.desc = image.desc;
				lifetime.size = resolveGraphImageSize(image.desc, viewport);
				if (image.desc.shape == GraphImageShape::CubeMap && lifetime.size.x != lifetime.size.y)
				{
					plan.diagnostics.push_back("Cubemap image '" + image.name + "' resolves to non-square dimensions " + to_string(lifetime.size.x) + "x" + to_string(lifetime.size.y) + ".");
					return;
				}
				auto const maxMipLevels = maxGraphImageMipLevels(lifetime.size);
				if (image.desc.mipLevels > maxMipLevels)
				{
					plan.diagnostics.push_back("Image '" + image.name + "' requests " + to_string(image.desc.mipLevels) +
						" mip levels but its resolved dimensions support at most " + to_string(maxMipLevels) + ".");
					return;
				}
				lifetime.firstPass = passPosition;
				lifetime.lastPass = passPosition;
				uint64_t texels = 0;
				for (uint32_t mip = 0; mip < image.desc.mipLevels; ++mip)
					texels += static_cast<uint64_t>(max(1u, lifetime.size.x >> mip)) * max(1u, lifetime.size.y >> mip);
				lifetime.estimatedBytes = (texels * formatBits(image.desc.format) * (image.desc.shape == GraphImageShape::CubeMap ? 6 : 1) + 7) / 8;
				index = (uint32_t)plan.allocatedImages.size();
				plan.allocatedImages.push_back(lifetime);
			}
			else
			{
				plan.allocatedImages[index].firstPass = min(plan.allocatedImages[index].firstPass, passPosition);
				plan.allocatedImages[index].lastPass = max(plan.allocatedImages[index].lastPass, passPosition);
			}
		};

		for (uint32_t position = 0; position < compiled.passOrder.size(); ++position)
		{
			auto const& pass = mPasses[compiled.passOrder[position].id];
			for (auto const& input : pass.sampledInputs) markImage(input, position);
			for (auto const& output : pass.colourOutputs) markImage(output.image, position);
			for (auto const& output : pass.depthOutputs) markImage(output.image, position);
		}
		if (plan.diagnostics.empty())
		{
			// An allocation is only reusable if everything already in it is transient.
			// Testing the incoming image alone is not enough: a non-transient image
			// takes an allocation of its own on first sight, and a later transient one
			// with a compatible descriptor and a disjoint lifetime would then be
			// planned on top of contents that have to survive the frame. Carrying the
			// flag on the group states the rule directly, rather than leaning on the
			// fact that only a group's first member can be non-transient.
			struct AllocationGroup { vector<uint32_t> members; bool aliasable; };
			vector<AllocationGroup> allocationGroups;
			for (uint32_t imageIndex = 0; imageIndex < plan.allocatedImages.size(); ++imageIndex)
			{
				auto& lifetime = plan.allocatedImages[imageIndex];
				uint32_t allocation = UINT32_MAX;
				if (lifetime.desc.shape == GraphImageShape::CubeMap)
				{
					for (uint32_t candidate = 0; candidate < allocationGroups.size(); ++candidate)
						if (plan.allocatedImages[allocationGroups[candidate].members.front()].image.id == lifetime.image.id) { allocation = candidate; break; }
				}
				if (allocation == UINT32_MAX && lifetime.desc.transient)
				{
					for (uint32_t candidate = 0; candidate < allocationGroups.size(); ++candidate)
					{
						if (!allocationGroups[candidate].aliasable) continue;
						auto const& representative = plan.allocatedImages[allocationGroups[candidate].members.front()];
						if (!graphImagesCanAlias(representative, lifetime)) continue;
						bool overlaps = false;
						for (auto member : allocationGroups[candidate].members)
						{
							auto const& previous = plan.allocatedImages[member];
							if (!(previous.lastPass < lifetime.firstPass || lifetime.lastPass < previous.firstPass)) { overlaps = true; break; }
						}
						if (!overlaps) { allocation = candidate; break; }
					}
				}
				if (allocation == UINT32_MAX)
				{
					allocation = static_cast<uint32_t>(allocationGroups.size());
					allocationGroups.push_back({ {}, lifetime.desc.transient });
					plan.estimatedPhysicalBytes += lifetime.estimatedBytes;
				}
				lifetime.physicalAllocation = allocation;
				allocationGroups[allocation].members.push_back(imageIndex);
			}
		}
		plan.valid = plan.diagnostics.empty();
		if (!plan.valid)
		{
			plan.allocatedImages.clear();
			plan.importedImages.clear();
		}
		return cachePlan(plan);
	}

	RenderGraphCompiledPlan RenderGraph::buildCompiledPlan(Caps const& caps, glm::uvec2 const& viewport) const
	{
		RenderGraphCompiledPlan result;
		result.compilation = compile(caps, viewport);
		if (!result.compilation.valid)
		{
			result.allocation.diagnostics = result.compilation.diagnostics;
			return result;
		}
		result.allocation = buildAllocationPlan(viewport);
		result.valid = result.allocation.valid;
		return result;
	}

	RenderGraphPlanCacheStats RenderGraph::getPlanCacheStats() const
	{
		return mPlanCacheStats;
	}

	string RenderGraph::describe() const
	{
		ostringstream output;
		output << "RenderGraph: " << mImages.size() << " image(s), " << mPasses.size() << " pass(es)\n";
		for (uint32_t id = 0; id < mImages.size(); ++id)
		{
			auto const& image = mImages[id];
			output << "  Image[" << id << "] '" << image.name << "': format=" << formatName(image.desc.format)
				<< ", versions=0.." << image.latestVersion << ", size=" << image.desc.absoluteSize.x << "x" << image.desc.absoluteSize.y
				<< " @ " << image.desc.relativeSize.x << "x" << image.desc.relativeSize.y << ", shape=" << (image.desc.shape == GraphImageShape::CubeMap ? "cubemap" : "2D") << ", mips=" << image.desc.mipLevels << ", colourSpace=" << (image.desc.colourSpace == TextureColourSpace::Srgb ? "sRGB" : "linear")
				<< ", filters=" << image.desc.params.minFilter << "/" << image.desc.params.magFilter << ", wrap=" << image.desc.params.wrap
				<< ", transient=" << (image.desc.transient ? "true" : "false") << ", external=" << (image.desc.external ? "true" : "false");
			if (!image.importName.empty()) output << ", import='" << image.importName << "'";
			output << "\n";
		}
		for (uint32_t id = 0; id < mPasses.size(); ++id)
		{
			auto const& pass = mPasses[id];
			output << "  Pass[" << id << "] '" << pass.name << "': enabled=" << (pass.enabled ? "true" : "false") << ", type=" << passTypeName(pass.type)
				<< ", sampled=" << pass.sampledInputs.size() << ", colour=" << pass.colourOutputs.size() << ", depth=" << pass.depthOutputs.size()
				<< ", parameters=" << pass.parameters.getNumUniforms();
			if (!pass.programResource.empty()) output << ", program='" << pass.programResource << "'";
			if (!pass.callbackFactory.empty()) output << ", factory='" << pass.callbackFactory << "'";
			output << "\n";
			for (auto const& binding : pass.samplerBindings)
				output << "    sampler '" << binding.sampler << "' = Image[" << binding.image.id << "]@" << binding.image.version << (binding.mipLevel == UINT32_MAX ? " full chain" : " mip " + to_string(binding.mipLevel)) << "\n";
			for (auto const& input : pass.sampledInputs)
				output << "    reads '" << mImages[input.id].valueIds[input.version] << "' Image[" << input.id << "]@" << input.version << "\n";
			for (auto const& target : pass.colourOutputs)
				output << "    writes colour '" << mImages[target.image.id].valueIds[target.image.version] << "' Image[" << target.image.id << "]@" << target.image.version << " mip " << target.mipLevel << (target.cubeFace == GraphNoCubeFace ? "" : " face " + to_string(target.cubeFace)) << "\n";
			for (auto const& target : pass.depthOutputs)
				output << "    writes depth '" << mImages[target.image.id].valueIds[target.image.version] << "' Image[" << target.image.id << "]@" << target.image.version << " mip " << target.mipLevel << (target.cubeFace == GraphNoCubeFace ? "" : " face " + to_string(target.cubeFace)) << "\n";
		}
		return output.str();
	}

	RenderGraphCompileResult RenderGraph::compile(Caps const& caps) const
	{
		return compile(caps, glm::uvec2(0));
	}

	RenderGraphCompileResult RenderGraph::compile(Caps const& caps, glm::uvec2 const& viewport) const
	{
		auto& cache = planCache();
		PlanCache::DeviceCompileKey const key{ viewport, caps.maxTextureSize, caps.maxColourAttachments, caps.maxDrawBuffers };
		auto const cached = cache.deviceCompiles.find(key);
		if (cached != cache.deviceCompiles.end())
		{
			++mPlanCacheStats.compileHits;
			return cached->second;
		}
		++mPlanCacheStats.compileMisses;
		auto result = compile();
		auto const topologyDiagnosticCount = result.diagnostics.size();
		// A zero viewport means the caller has none to offer, so relative sizes stay
		// unresolved and unchecked here -- buildAllocationPlan still catches them,
		// just later and with a throw rather than a diagnostic.
		bool const haveViewport = viewport.x != 0 && viewport.y != 0;
		for (auto const& image : mImages)
		{
			// External images are supplied by the importer, not allocated from this
			// descriptor, so their size is not ours to validate.
			if (image.desc.external) continue;
			bool const absolute = image.desc.absoluteSize.x != 0 && image.desc.absoluteSize.y != 0;
			if (!absolute && !haveViewport) continue;
			auto const size = resolveGraphImageSize(image.desc, viewport);
			auto const maxSize = (uint32_t)max(0, caps.maxTextureSize);
			if (image.desc.shape == GraphImageShape::CubeMap && size.x != size.y)
			{
				result.diagnostics.push_back("Cubemap image '" + image.name + "' resolves to non-square dimensions " + to_string(size.x) + "x" + to_string(size.y) + ".");
			}
			if (maxSize && (size.x > maxSize || size.y > maxSize))
			{
				result.diagnostics.push_back("Image '" + image.name + "' resolves to " + to_string(size.x) + "x" + to_string(size.y) +
					", exceeding the maximum texture size of " + to_string(maxSize) + ".");
			}
			auto const maxMipLevels = maxGraphImageMipLevels(size);
			if (image.desc.mipLevels > maxMipLevels)
			{
				result.diagnostics.push_back("Image '" + image.name + "' requests " + to_string(image.desc.mipLevels) +
					" mip levels but its resolved dimensions " + to_string(size.x) + "x" + to_string(size.y) +
					" support at most " + to_string(maxMipLevels) + ".");
			}
		}
		for (auto const& pass : mPasses)
		{
			if (pass.colourOutputs.size() > caps.maxColourAttachments || pass.colourOutputs.size() > caps.maxDrawBuffers)
			{
				result.diagnostics.push_back("Pass '" + pass.name + "' declares " + to_string(pass.colourOutputs.size()) +
					" colour outputs, exceeding the renderer MRT capability.");
			}
		}
		for (size_t index = topologyDiagnosticCount; index < result.diagnostics.size(); ++index)
			result.messages.push_back({ GraphCompileMessageSeverity::Error, "RG-COMPILER-CAPABILITY", result.diagnostics[index], {}, {} });
		if (!result.diagnostics.empty())
		{
			result.valid = false;
			result.passOrder.clear();
		}
		cache.deviceCompiles.emplace(key, result);
		return result;
	}
}
