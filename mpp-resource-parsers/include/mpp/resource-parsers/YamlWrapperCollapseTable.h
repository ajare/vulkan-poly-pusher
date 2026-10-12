#pragma once

#include <map>
#include <string>

namespace mpp::resource_parsers
{
	// Container element names, across every resource schema this project reads,
	// whose entire content is one repeated child tag AND whose parser code
	// treats that content as a list (iterates every entry), mapped to that
	// child's tag name. Used by YamlSerializer so utils::YamlReader/YamlWriter
	// can collapse these to a bare YAML sequence instead of an extra nested
	// map level.
	//
	// Deliberately excluded, even though they structurally look the same
	// (a node whose children currently share one tag): single-slot wrapper
	// elements that are never iterated as a list by the parsers that read them
	// (e.g. FragmentShader/VertexShader/Library/PreviewScene/String all wrap a
	// single "file" field; BaseColourMap/EmissiveMap/MetallicRoughnessMap/
	// NormalMap/OcclusionMap each wrap a single texture Resource/Ref), and tags
	// that mean different things in different schemas (Extensions holds
	// Extension in pipelines but Uniform in materials; Materials holds either
	// BasicMaterial or PbrMaterial; Program holds either Ref or Resource).
	// Leaving those out of the table is always safe: YamlSerializer falls back
	// to the generic, uncollapsed per-tag rendering for them.
	inline std::map<std::string, std::string> const& yamlWrapperCollapseTable()
	{
		static std::map<std::string, std::string> const table{
			{"Layers", "Layer"},
			{"Models", "Model"},
			{"Lights", "Light"},
			{"Images", "Image"},
			{"Emitters", "Emitter"},
			{"Keys", "Key"},
			{"Passes", "Pass"},
			{"Buffer", "Channel"},
			{"Inputs", "Sampled"},
			{"Colours", "Output"},
			{"Outputs", "Output"},
			{"ResourceLibraries", "Library"},
			{"Imports", "Import"},
			{"PreviewBindings", "Material"},
			{"PreviewOverrides", "Override"},
			{"SamplerSlots", "Slot"},
			{"Uniforms", "Uniform"},
			{"Textures", "Texture"},
		};
		return table;
	}
}
