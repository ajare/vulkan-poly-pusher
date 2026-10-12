#include "mpp/CompiledProgram.h"
#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/resource-parsers/FileProgramStream.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace {
void require(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }
std::string read(std::string const& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), {}};
}
}
bool shaderCompilationTests(std::string* failure) {
    try {
        std::string root = VPP_SOURCE_DIR "/docs/comparison/m01-basics/shaders/";
        mpp::resource_parsers::FileProgramStream authored(nullptr, root + "vpp/Triangle.program.yaml");
        auto compiled = mpp::compileProgram(authored, "Basics.Triangle");
        require(compiled->vertex.spirv.front() == 0x07230203 && compiled->fragment.spirv.front() == 0x07230203, "not SPIR-V");
        auto const& inputs = compiled->vertex.inputs;
        auto position = std::find_if(inputs.begin(), inputs.end(), [](auto const& v) { return v.name == "_mpp_i_POSITION"; });
        auto colour = std::find_if(inputs.begin(), inputs.end(), [](auto const& v) { return v.name == "_mpp_i_COLOUR"; });
        require(position != inputs.end() && position->location == 0 && position->components == 3 && position->scalarWidth == 32, "position reflection differs from mesh source");
        require(colour != inputs.end() && colour->location == 1 && colour->components == 4, "colour reflection differs from mesh source");
        require(compiled->vertex.outputs.size() == 1 && compiled->fragment.inputs.size() == 1 && compiled->fragment.outputs.size() == 1, "stage IO reflection missing");
        require(compiled->vertex.bindings.size() == 1, "built-in uniform block missing");
        auto const& builtins = compiled->vertex.bindings.front();
        require(builtins.set == 0 && builtins.binding == 0 && builtins.members.size() == 4 && builtins.size > 0, "built-in uniform binding differs");
        for (auto const* token : {"_mpp_u_viewPos_", "_mpp_u_model_", "_mpp_u_modelCameraProjection_", "_mpp_u_normal_"})
            require(std::any_of(builtins.members.begin(), builtins.members.end(), [&](auto const& m) { return m.name == token; }), "authoring token not retained in reflection");
        // Select the other backend's variant under the same logical name without
        // changing the authored resource schema or frozen resource-root workflow.
        mpp::resource_parsers::FileProgramStream legacy(nullptr, root + "mpp/Triangle.program.yaml");
        legacy.load();
        require(legacy.getVertexSource() != authored.getVertexSource(), "backend root did not select shader variant");
        auto vertex = read(root + "vpp/Triangle.vert");
        auto fragment = read(root + "vpp/Triangle.frag");
        auto compile = [&](std::string vs, std::string fs) {
            auto parser = std::make_shared<mpp::program::Parser>("Runtime.Edit");
            parser->setMeshSpecification(authored.getMeshSpecification());
            parser->setVertexSource(vs); parser->setFragmentSource(fs);
            mpp::ProgrammaticProgramStream stream(nullptr);
            stream.setParser(parser);
            return mpp::compileProgram(stream, "Runtime.Edit");
        };
        auto withBindings = std::string("@@Version\nlayout(std140, set=1, binding=3) uniform Material { vec4 tint; float scale; } material;\nlayout(set=1, binding=4) uniform sampler2D image;\nvoid main() { @Out(vec4 COLOUR) = @In(COLOUR) * material.tint * material.scale * texture(image, vec2(0)); }\n");
        auto reflected = compile(vertex, withBindings);
        auto const& descriptors = reflected->fragment.bindings;
        auto block = std::find_if(descriptors.begin(), descriptors.end(), [](auto const& b) { return b.set == 1 && b.binding == 3; });
        auto sampler = std::find_if(descriptors.begin(), descriptors.end(), [](auto const& b) { return b.set == 1 && b.binding == 4; });
        require(block != descriptors.end() && block->kind == mpp::ShaderBinding::Kind::UniformBlock && block->size == 32 && block->members.size() == 2, "authored uniform block reflection wrong");
        require(block->members[0].name == "tint" && block->members[0].offset == 0 && block->members[1].offset == 16, "uniform member offsets wrong");
        require(sampler != descriptors.end() && sampler->kind == mpp::ShaderBinding::Kind::CombinedImageSampler && sampler->count == 1, "authored descriptor binding wrong");
        auto generated = compile(vertex, "@@Version\n@@Uniform(vec4 TINT);\n@@Texture(sampler2D TEX0);\nvoid main() { @Out(vec4 COLOUR) = @In(COLOUR) * @Uniform(TINT) * texture(@Texture(TEX0), vec2(0)); }\n");
        require(generated->fragment.bindings.size() == 2, "generated uniform/sampler descriptors missing");
        // Unload/edit/recompile: runtime source changes must not reuse stale SPIR-V.
        auto runtimeParser = std::make_shared<mpp::program::Parser>("Runtime.Reload");
        runtimeParser->setMeshSpecification(authored.getMeshSpecification());
        runtimeParser->setVertexSource(vertex); runtimeParser->setFragmentSource(fragment);
        mpp::ProgrammaticProgramStream runtimeStream(nullptr);
        runtimeStream.setParser(runtimeParser);
        auto before = mpp::compileProgram(runtimeStream, "Runtime.Reload");
        runtimeStream.unload();
        runtimeParser->setFragmentSource("@@Version\nvoid main() { @Out(vec4 COLOUR) = vec4(1,0,0,1); }");
        auto after = mpp::compileProgram(runtimeStream, "Runtime.Reload");
        require(before->fragment.spirv != after->fragment.spirv, "unload/edit/reload returned stale code");
        require(mpp::compileProgram(runtimeStream, "Runtime.Reload")->fragment.spirv == after->fragment.spirv, "repeat compilation changed code");
        auto edited = compile(vertex, fragment + "\n// runtime edit\n");
        require(edited->vertex.spirv == compiled->vertex.spirv, "nonsemantic edit changed vertex");
        auto changed = fragment;
        changed.replace(changed.find("@In(COLOUR)"), 11, "vec4(1,0,0,1)");
        require(compile(vertex, changed)->fragment.spirv != compiled->fragment.spirv, "runtime edit reused stale fragment");
        auto rejects = [&](std::string vs, std::string fs, std::string const& diagnostic) {
            try { compile(vs, fs); } catch (std::exception const& e) {
                auto message = std::string(e.what());
                if (message.find("Runtime.Edit") == std::string::npos || message.find(diagnostic) == std::string::npos)
                    throw std::runtime_error("expected diagnostic '" + diagnostic + "', got: " + message);
                return;
            }
            throw std::runtime_error("invalid shader succeeded: " + diagnostic);
        };
        rejects("", fragment, "vertex");
        rejects(vertex, "", "fragment");
        rejects(vertex, "@@Version\nvoid main() { invalid; }", "fragment");
        rejects(vertex, "void main() { @Out(vec4 COLOUR) = vec4(1); }", "version");
        rejects(vertex, "@@Version\nlayout(set=0,binding=-1) uniform sampler2D image;\nvoid main() { @Out(vec4 COLOUR) = texture(image, vec2(0)); }", "cannot be negative");
        rejects(vertex, "@@Version\nlayout(set=0,binding=1) uniform sampler2D a;\nlayout(set=0,binding=1) uniform sampler2D b;\nvoid main() { @Out(vec4 COLOUR) = texture(a, vec2(0)) + texture(b, vec2(0)); }", "binding");
        rejects(vertex, "@@Version\nlayout(set=0,binding=2) buffer Data { vec4 colour; } data;\nvoid main() { @Out(vec4 COLOUR) = data.colour; }", "unsupported descriptor");
        rejects(vertex, "@@Version\nlayout(push_constant) uniform Data { vec4 colour; } data;\nvoid main() { @Out(vec4 COLOUR) = data.colour; }", "push constants");
        rejects(vertex, "@@Version\nlayout(std140,set=0,binding=0) uniform Wrong { vec4 value; } wrong;\nvoid main() { @Out(vec4 COLOUR) = wrong.value; }", "duplicate descriptor");
        rejects(vertex + "\nlayout(std140,set=2,binding=0) uniform Shared { vec4 value; } shaderData;\n", "@@Version\nlayout(std140,set=2,binding=0) uniform Shared { mat4 value; } shaderData;\nvoid main() { @Out(vec4 COLOUR) = shaderData.value[0]; }", "cross-stage descriptor");
        rejects(vertex, "@@Version\nlayout(location=1) in vec3 unmatched;\nvoid main() { @Out(vec4 COLOUR) = vec4(unmatched,1); }", "stage interface mismatch");
        runtimeStream.unload();
        runtimeParser->setGeometrySource("@@Version\nvoid main() {}\n");
        try { mpp::compileProgram(runtimeStream, "Runtime.Geometry"); throw std::runtime_error("geometry shader silently ignored"); }
        catch (std::exception const& error) {
            require(std::string(error.what()).find("geometry shaders are not delivered") != std::string::npos, "geometry did not fail explicitly");
        }
        return true;
    } catch (std::exception const& error) { *failure = error.what(); return false; }
}
