#include "mpp/PbrMaterial.h"
#include "mpp/PbrMaterialFeatures.h"
#include "mpp/PbrMaterialTests.h"
#include "mpp/PbrShaders.h"
#include "mpp/RenderPass.h"

namespace mpp
{
	bool runPbrMaterialSpecializationTests(std::string* failure)
	{
		auto fail = [&](std::string const& message) { if (failure) *failure = message; return false; };
		PbrMaterialSpecification::PbrSurface surface;
		surface.metallicFactor = 0.0f;
		surface.roughnessFactor = 0.0f;
		surface.normalScale = 0.0f;
		surface.occlusionStrength = 0.0f;
		std::vector<PbrMaterialSpecification::TextureOptions> textures;
		auto minimal = derivePbrMaterialFeatures(surface, textures);
		if (minimal != 0 || describePbrMaterialFeatures(minimal) != "Minimal") return fail("minimal feature derivation failed");

		auto addMap = [&](char const* sampler)
		{
			PbrMaterialSpecification::TextureOptions texture;
			texture.resourceExists = true; texture.sampler = sampler; textures.push_back(texture);
		};
		addMap("PBR_BASE_COLOUR_MAP"); addMap("PBR_METALLIC_ROUGHNESS_MAP"); addMap("PBR_NORMAL_MAP");
		addMap("PBR_OCCLUSION_MAP"); addMap("PBR_EMISSIVE_MAP");
		surface.metallicFactor = 1.0f; surface.roughnessFactor = 0.5f; surface.normalScale = 1.0f;
		surface.occlusionStrength = 1.0f; surface.emissiveFactor = { 0.0f, 2.0f, 0.0f };
		surface.alphaMode = PbrMaterialSpecification::PbrAlphaMode::Mask; surface.doubleSided = true;
		auto full = derivePbrMaterialFeatures(surface, textures);
		for (auto feature : { PbrMaterialFeature::BaseColourMap, PbrMaterialFeature::Metallic, PbrMaterialFeature::Roughness,
			PbrMaterialFeature::MetallicRoughnessMap, PbrMaterialFeature::NormalMap, PbrMaterialFeature::Occlusion,
			PbrMaterialFeature::Emissive, PbrMaterialFeature::AlphaMask, PbrMaterialFeature::DoubleSided })
			if (!hasPbrFeature(full, feature)) return fail("full feature derivation omitted a required bit");
		if (hasPbrFeature(full, PbrMaterialFeature::AlphaBlend)) return fail("mask derivation also enabled blend");

		// The generic caster policy deliberately does not infer arbitrary fragment
		// alpha. PBR is the one standardized adapter: Mask follows base-colour
		// alpha, while Blend stays a receiver but never becomes a solid caster.
		surface.baseColourFactor.a = 0.75f;
		surface.alphaCutoff = 0.42f;
		auto maskedCaster = makePbrShadowCasterContract(surface);
		if (maskedCaster.behaviour != ShadowCasterContract::Behaviour::AlphaMask ||
			maskedCaster.alphaSampler != "PBR_BASE_COLOUR_MAP" || maskedCaster.alphaCutoff != 0.42f ||
			maskedCaster.alphaFactor != 0.75f || !castsShadow(maskedCaster))
			return fail("PBR alpha-mask did not adapt to the generic shadow caster contract");
		surface.alphaMode = PbrMaterialSpecification::PbrAlphaMode::Blend;
		if (castsShadow(makePbrShadowCasterContract(surface))) return fail("PBR blend material casts an opaque shadow");
		surface.alphaMode = PbrMaterialSpecification::PbrAlphaMode::Opaque;
		if (!castsShadow(makePbrShadowCasterContract(surface))) return fail("opaque material no longer casts by default");
		ShadowCasterContract disabled; disabled.behaviour = ShadowCasterContract::Behaviour::Disabled;
		ShadowCasterContract custom; custom.behaviour = ShadowCasterContract::Behaviour::Custom;
		if (castsShadow(disabled) || !castsShadow(custom)) return fail("explicit generic caster policy was not retained");

		auto defines = makePbrSpecializationDefines(full);
		if (defines.find("#define PBR_SPEC_NORMAL_MAP 1") == std::string::npos || defines.find("#define PBR_SPEC_ALPHA_BLEND 0") == std::string::npos)
			return fail("specialization define generation failed");
		auto injected = injectPbrSpecializationDefines("@@Version\nvoid main() {}", full);
		if (injected.rfind("@@Version\n#define PBR_SPEC_", 0) != 0) return fail("specialization defines were not inserted after @@Version");
		if (!hasPbrFeature(derivePbrMaterialFeatures(surface, textures, true), PbrMaterialFeature::LegacyFullContract)) return fail("legacy contract marker was lost");
		PbrMaterialSpecification::PbrSurface scalarSurface;
		scalarSurface.metallicFactor = 0.0f; scalarSurface.roughnessFactor = 0.0f;
		std::vector<PbrMaterialSpecification::TextureOptions> scalarMaps;
		PbrMaterialSpecification::TextureOptions metallic; metallic.resourceExists = true; metallic.sampler = "PBR_METALLIC_MAP"; metallic.channel = 2; scalarMaps.push_back(metallic);
		PbrMaterialSpecification::TextureOptions roughness; roughness.resourceExists = true; roughness.sampler = "PBR_ROUGHNESS_MAP"; roughness.channel = 3; scalarMaps.push_back(roughness);
		auto scalar = derivePbrMaterialFeatures(scalarSurface, scalarMaps);
		if (!hasPbrFeature(scalar, PbrMaterialFeature::Metallic) || !hasPbrFeature(scalar, PbrMaterialFeature::Roughness) ||
		    !hasPbrFeature(scalar, PbrMaterialFeature::MetallicMap) || !hasPbrFeature(scalar, PbrMaterialFeature::RoughnessMap) ||
		    makePbrSpecializationDefines(scalar).find("#define PBR_SPEC_METALLIC_MAP 1") == std::string::npos)
			return fail("independent scalar-map feature derivation failed");
		PbrMaterialSpecification::PbrSurface flatEmissive;
		flatEmissive.emissiveFactor = {1.0f, 0.25f, 0.0f};
		auto flat = derivePbrMaterialFeatures(flatEmissive, {});
		if (!hasPbrFeature(flat, PbrMaterialFeature::Emissive) || hasPbrFeature(flat, PbrMaterialFeature::EmissiveMap))
			return fail("flat emissive feature derivation failed");
		PbrMaterialSpecification::TextureOptions emissive; emissive.resourceExists = true; emissive.sampler = "PBR_EMISSIVE_MAP";
		auto image = derivePbrMaterialFeatures(flatEmissive, {emissive});
		if (!hasPbrFeature(image, PbrMaterialFeature::EmissiveMap) || makePbrSpecializationDefines(image).find("#define PBR_SPEC_EMISSIVE_MAP 1") == std::string::npos)
			return fail("emissive image-map feature derivation failed");

		// The prefiltered specular chain length depends on the authored prefilter
		// resolution, so the IBL fetch must scale roughness by the renderer-supplied
		// PBR_PREFILTERED_MAX_LOD rather than by an assumed constant. A literal
		// multiplier silently mismatches every chain that is not five levels deep.
		std::string const fragment = BuiltInPbrFragmentShader;
		if (fragment.find("@@Uniform(float PBR_PREFILTERED_MAX_LOD)") == std::string::npos)
			return fail("built-in PBR shader does not declare PBR_PREFILTERED_MAX_LOD");
		auto const specularFetch = fragment.find("textureLod(@Texture(PBR_PREFILTERED_SPECULAR_MAP)");
		if (specularFetch == std::string::npos) return fail("built-in PBR shader has no prefiltered specular fetch");
		auto const fetchEnd = fragment.find(';', specularFetch);
		if (fetchEnd == std::string::npos ||
		    fragment.find("@Uniform(PBR_PREFILTERED_MAX_LOD)", specularFetch) > fetchEnd)
			return fail("prefiltered specular fetch does not scale roughness by PBR_PREFILTERED_MAX_LOD");

		// Water is opted into, never inferred: the SSR block needs renderer-bound
		// scene colour and depth that only a water graph pass supplies, so a
		// material must not acquire it from a texture slot the way maps do.
		PbrMaterialSpecification::PbrSurface waterSurface;
		waterSurface.alphaMode = PbrMaterialSpecification::PbrAlphaMode::Blend;
		if (hasPbrFeature(derivePbrMaterialFeatures(waterSurface, {}), PbrMaterialFeature::Water))
			return fail("water feature was derived without being authored");
		waterSurface.water.enabled = true;
		auto const water = derivePbrMaterialFeatures(waterSurface, {});
		if (!hasPbrFeature(water, PbrMaterialFeature::Water) || !hasPbrFeature(water, PbrMaterialFeature::AlphaBlend))
			return fail("water feature derivation failed");
		if (describePbrMaterialFeatures(water).find("Water") == std::string::npos)
			return fail("water feature is missing from the feature summary");
		if (makePbrSpecializationDefines(water).find("#define PBR_SPEC_WATER 1") == std::string::npos ||
		    makePbrSpecializationDefines(full).find("#define PBR_SPEC_WATER 0") == std::string::npos)
			return fail("water specialization define generation failed");
		// Confidence 0 has to reproduce the ordinary IBL specular path exactly, which
		// it only does if the SSR mix consumes the same `prefiltered` value the
		// non-water path computed rather than a second, divergent cubemap fetch.
		if (fragment.find("prefiltered = mix(prefiltered, waterSsr,") == std::string::npos)
			return fail("water SSR does not blend into the existing prefiltered specular term");
		if (fragment.find("textureLod(@Texture(PBR_SCENE_COLOUR_RESOLVED), waterHitUv, waterLod)") == std::string::npos)
			return fail("water SSR does not sample the resolved scene colour through a roughness LOD");

		// doubleSided reached the shader and the feature bitset but never the
		// rasterizer, so back faces were culled before the normal-flipping branch
		// could run. The material has to win over the model's culling flag here:
		// a surface with no meaningful back face cannot be drawn single-sided.
		if (classifyPbrForwardMesh(true, false, true, true).cullBackFaces)
			return fail("a double-sided PBR material did not override the model's back-face culling flag");
		if (classifyPbrForwardMesh(true, false, false, true).cullBackFaces != true)
			return fail("a single-sided PBR material lost the model's back-face culling flag");
		if (classifyPbrForwardMesh(true, false, true, false).cullBackFaces)
			return fail("a double-sided PBR material enabled culling that the model had not asked for");
		// Only PBR surfaces carry the concept, so a BasicMaterial must keep the
		// model flag even if some future material type reports doubleSided.
		if (!classifyPbrForwardMesh(false, false, true, true).cullBackFaces)
			return fail("a non-PBR material had its culling flag overridden");
		// The alpha semantics sharing this classification must not have moved.
		if (classifyPbrForwardMesh(true, true, false, true) != PbrForwardMeshClassification{ true, true, true })
			return fail("PBR blend classification changed");
		if (classifyPbrForwardMesh(false, true, false, true) != PbrForwardMeshClassification{ false, false, true })
			return fail("a non-PBR material was classified as blended");
		return true;
	}
}
