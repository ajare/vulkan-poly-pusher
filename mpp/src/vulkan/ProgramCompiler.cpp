#include "mpp/CompiledProgram.h"
#include "mpp/ProgramStream.h"
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_reflect.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <regex>
#include <stdexcept>

namespace mpp {
namespace {
[[noreturn]] void fail(std::string const& name, std::string const& message) {
    throw std::runtime_error("Vulkan program '" + name + "': " + message);
}
// Only parser-generated loose uniforms are lifted. Authored Vulkan blocks and
// explicit bindings remain untouched, and glslang validates all other GLSL.
void adapt(std::string& vertex, std::string& fragment, std::string const& name) {
    std::regex uniform(R"(uniform\s+(\w+)\s+(_mpp_[ut]_\w+)\s*(\[\s*\d+\s*\])?\s*;)");
    std::map<std::string, std::string> members, samplers;
    for (auto const* source : {&vertex, &fragment}) {
        for (std::sregex_iterator it(source->begin(), source->end(), uniform), end; it != end; ++it) {
            auto type = (*it)[1].str(), variable = (*it)[2].str(), array = (*it)[3].str();
            bool sampler = type.starts_with("sampler");
            if (sampler && type != "sampler2D") fail(name, "only sampler2D is delivered in the M01 shader subset: " + variable);
            auto& declarations = sampler ? samplers : members;
            auto declaration = type + " " + variable + array + ";";
            auto [entry, inserted] = declarations.emplace(variable, declaration);
            if (!inserted && entry->second != declaration) fail(name, "conflicting uniform declaration: " + variable);
        }
    }
    std::string block;
    if (!members.empty()) {
        block = "\nlayout(std140, set=0, binding=0) uniform _mpp_Uniforms {\n";
        for (auto const& [variable, declaration] : members) block += declaration + "\n";
        block += "};\n";
    }
    for (auto* source : {&vertex, &fragment}) {
        std::string result;
        size_t cursor = 0;
        for (std::sregex_iterator it(source->begin(), source->end(), uniform), end; it != end; ++it) {
            auto const& match = *it;
            // Do not silently override authored layout qualifiers.
            auto lineStart = source->rfind('\n', match.position());
            auto prefix = source->substr(lineStart == std::string::npos ? 0 : lineStart + 1,
                match.position() - (lineStart == std::string::npos ? 0 : lineStart + 1));
            if (prefix.find("layout") != std::string::npos) fail(name, "parser-prefixed loose uniforms cannot have layout qualifiers; author a uniform block instead");
            result += source->substr(cursor, match.position() - cursor);
            auto variable = match[2].str();
            if (samplers.contains(variable)) {
                auto binding = 1 + std::distance(samplers.begin(), samplers.find(variable));
                result += "layout(set=0, binding=" + std::to_string(binding) + ") " + match.str();
            }
            cursor = match.position() + match.length();
        }
        result += source->substr(cursor);
        auto version = result.find("#version");
        if (version == std::string::npos) fail(name, "missing shader version line (authored source requires @@Version)");
        auto versionEnd = result.find('\n', version);
        if (versionEnd == std::string::npos) fail(name, "missing shader version line");
        result.insert(versionEnd + 1, block);
        *source = std::move(result);
    }
}
void reflect(CompiledShader& shader, std::string const& name) {
    SpvReflectShaderModule module{};
    if (spvReflectCreateShaderModule(shader.spirv.size() * sizeof(uint32_t), shader.spirv.data(), &module) != SPV_REFLECT_RESULT_SUCCESS)
        fail(name, "SPIR-V reflection failed");
    struct Cleanup { SpvReflectShaderModule* module; ~Cleanup() { spvReflectDestroyShaderModule(module); } } cleanup{&module};
    auto scalar = [](SpvReflectTypeDescription const* type) {
        if (type->type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) return ShaderScalar::Float;
        if (type->type_flags & SPV_REFLECT_TYPE_FLAG_BOOL) return ShaderScalar::Boolean;
        return type->traits.numeric.scalar.signedness ? ShaderScalar::SignedInt : ShaderScalar::UnsignedInt;
    };
    auto variables = [&](uint32_t count, SpvReflectInterfaceVariable** data, auto& destination) {
        for (uint32_t i = 0; i < count; ++i) {
            auto const& v = *data[i];
            if (v.decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) continue;
            if (v.member_count || v.array.dims_count) fail(name, "interface blocks/arrays are not delivered: " + std::string(v.name ? v.name : "unnamed"));
            destination.push_back({v.name ? v.name : "", v.location, v.numeric.vector.component_count,
                v.numeric.matrix.column_count, v.numeric.scalar.width, scalar(v.type_description)});
        }
    };
    variables(module.input_variable_count, module.input_variables, shader.inputs);
    variables(module.output_variable_count, module.output_variables, shader.outputs);
    if (module.push_constant_block_count) fail(name, "push constants are not delivered in the M01 shader subset");
    for (uint32_t i = 0; i < module.descriptor_binding_count; ++i) {
        auto const& b = module.descriptor_bindings[i];
        ShaderBinding out;
        out.name = b.name ? b.name : "";
        out.set = b.set; out.binding = b.binding; out.count = b.count;
        if (!out.count) fail(name, "runtime descriptor arrays are not delivered: " + out.name);
        if (b.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            out.kind = ShaderBinding::Kind::UniformBlock;
            out.size = b.block.padded_size;
            for (uint32_t j = 0; j < b.block.member_count; ++j) {
                auto const& m = b.block.members[j];
                if (m.member_count) fail(name, "nested uniform structs are not delivered: " + out.name);
                UniformMember member{m.name ? m.name : "", m.offset, m.size, m.array.stride, m.numeric.matrix.stride,
                    m.numeric.vector.component_count, m.numeric.matrix.column_count, m.numeric.scalar.width, scalar(m.type_description),
                    {m.array.dims, m.array.dims + m.array.dims_count}, bool(m.decoration_flags & SPV_REFLECT_DECORATION_ROW_MAJOR)};
                out.members.push_back(std::move(member));
            }
        } else if (b.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && b.image.dim == SpvDim2D && !b.image.arrayed && !b.image.ms) {
            out.kind = ShaderBinding::Kind::CombinedImageSampler;
        } else fail(name, "unsupported descriptor kind: " + out.name);
        for (auto const& previous : shader.bindings)
            if (previous.set == out.set && previous.binding == out.binding)
                fail(name, "duplicate descriptor binding set=" + std::to_string(out.set) + " binding=" + std::to_string(out.binding));
        shader.bindings.push_back(std::move(out));
    }
}
}
std::shared_ptr<CompiledProgram const> compileProgram(ProgramStream& stream, std::string const& logicalName) {
    try { stream.load(); }
    catch (std::exception const& error) { fail(logicalName, error.what()); }
    auto vertex = stream.getVertexSource(), fragment = stream.getFragmentSource();
    if (vertex.empty()) fail(logicalName, "missing vertex stage");
    if (fragment.empty()) fail(logicalName, "missing fragment stage");
    if (!stream.getGeometrySource().empty()) fail(logicalName, "geometry shaders are not delivered");
    adapt(vertex, fragment, logicalName);
    // Length-prefixed source/layout keys avoid ambiguous stage concatenation.
    auto key = stream.getConcatenatedSource();
    static std::mutex mutex;
    static std::map<std::string, std::shared_ptr<CompiledProgram const>> cache;
    std::lock_guard lock(mutex);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    struct Process {
        bool initialized = glslang::InitializeProcess();
        ~Process() { if (initialized) glslang::FinalizeProcess(); }
    };
    static Process process;
    if (!process.initialized) fail(logicalName, "glslang initialization failed");
    glslang::TShader vs(EShLangVertex), fs(EShLangFragment);
    auto parse = [&](glslang::TShader& shader, std::string const& source, EShLanguage stage, char const* stageName) {
        auto text = source.c_str();
        auto label = (logicalName + "." + stageName);
        auto filename = label.c_str();
        shader.setStringsWithLengthsAndNames(&text, nullptr, &filename, 1);
        shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
        shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
        shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
        auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        if (!shader.parse(GetDefaultResources(), 440, false, messages)) fail(logicalName, std::string(stageName) + ": " + shader.getInfoLog());
    };
    parse(vs, vertex, EShLangVertex, "vertex");
    parse(fs, fragment, EShLangFragment, "fragment");
    // MPP uses different input/output prefixes. Vulkan links explicit locations,
    // whereas glslang's multi-stage linker additionally requires matching names.
    // Link stages separately and validate their reflected location/type contracts.
    glslang::TProgram vertexProgram, fragmentProgram;
    vertexProgram.addShader(&vs); fragmentProgram.addShader(&fs);
    auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!vertexProgram.link(messages)) fail(logicalName, std::string("vertex link: ") + vertexProgram.getInfoLog());
    if (!fragmentProgram.link(messages)) fail(logicalName, std::string("fragment link: ") + fragmentProgram.getInfoLog());
    auto result = std::make_shared<CompiledProgram>();
    glslang::SpvOptions options;
    options.disableOptimizer = true;
    glslang::GlslangToSpv(*vertexProgram.getIntermediate(EShLangVertex), result->vertex.spirv, &options);
    glslang::GlslangToSpv(*fragmentProgram.getIntermediate(EShLangFragment), result->fragment.spirv, &options);
    reflect(result->vertex, logicalName + " vertex");
    reflect(result->fragment, logicalName + " fragment");
    for (auto const& input : result->fragment.inputs) {
        auto output = std::find_if(result->vertex.outputs.begin(), result->vertex.outputs.end(), [&](auto const& value) { return value.location == input.location; });
        if (output == result->vertex.outputs.end() || output->components != input.components || output->columns != input.columns || output->scalarWidth != input.scalarWidth || output->scalar != input.scalar)
            fail(logicalName, "stage interface mismatch at fragment location=" + std::to_string(input.location));
    }
    for (auto const& a : result->vertex.bindings) for (auto const& b : result->fragment.bindings) {
        if (a.set != b.set || a.binding != b.binding) continue;
        if (a.kind != b.kind || a.count != b.count || a.size != b.size || a.name != b.name || a.members != b.members)
            fail(logicalName, "incompatible cross-stage descriptor binding set=" + std::to_string(a.set) + " binding=" + std::to_string(a.binding));
    }
    if (cache.size() >= 64) cache.erase(cache.begin());
    cache.emplace(std::move(key), result);
    return result;
}
}
