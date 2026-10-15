// VPP program/material realization scenario (#95): authored programs and
// logical materials become native pipelines, descriptor set/pipeline layouts
// and descriptor-backed per-frame-context uniform storage through the public
// Program/BasicMaterial/ResourceManager APIs. Verifies reflected metadata,
// uniform values reaching native storage per frame context (diagnostic
// readback), runtime shader authoring (edit/reload rebuilds the pipeline),
// and explicit failure of every not-yet-delivered program/material path.
#include "mpp/RenderSystem.h"
#include "mpp/ResourceManager.h"
#include "mpp/Program.h"
#include "mpp/BasicMaterial.h"
#include "mpp/ProgrammaticProgramStream.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/ProgrammaticSamplerStream.h"
#include "mpp/resource-parsers/FileProgramStream.h"
#include "mpp/PipelineDiagnostics.h"
#include "mpp/app/WindowSDL.h"
#include "mpp/app/InputManagerSDL.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace mpp;
namespace {
void require(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }
template <typename F>
void requireFails(std::string const& diagnostic, std::string const& needle, F operation) {
    try { operation(); }
    catch (std::exception const& error) {
        if (std::string(error.what()).find(needle) == std::string::npos)
            throw std::runtime_error("diagnostic '" + diagnostic + "' lacks '" + needle + "': " + error.what());
        return;
    }
    throw std::runtime_error("expected failure did not occur: " + diagnostic);
}
std::shared_ptr<program::Parser> makeParser(mesh::MeshSpecification const& spec,
    std::string const& name, std::string const& vertex, std::string const& fragment) {
    auto parser = std::make_shared<program::Parser>(name);
    parser->setMeshSpecification(spec);
    parser->setVertexSource(vertex);
    parser->setFragmentSource(fragment);
    return parser;
}
}
int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    int result = 0;
    try {
        auto start = std::chrono::steady_clock::now();
        InputManagerSDL input;
        WindowSDL window("VPP program pipeline");
        window.create(320, 240, false, false);
        window.processEvents(&input);
        RenderSystem renderer(320, 240, nullptr);
        ResourceManager resources(&renderer, nullptr);

        auto frame = [&](int count) {
            for (int i = 0; i < count; ++i) {
                window.processEvents(&input);
                renderer.clearScreen(Colour(0.1f, 0.1f, 0.2f, 1));
                window.show();
                SDL_Delay(1);
            }
        };

        // Authored program resource: the shared Basics triangle variant.
        std::string const root = std::string(VPP_SOURCE_DIR) + "/docs/comparison/m01-basics/shaders/";
        auto [triangleResource, triangleCreated] = resources.declareResource("Basics.Triangle",
            std::make_shared<resource_parsers::FileProgramStream>(&resources, root + "vpp/Triangle.program.yaml"));
        require(triangleCreated, "program was not declared as a new resource");
        auto* triangle = static_cast<Program*>(triangleResource.get());

        // Identical authored source aliases to the same program resource.
        auto [aliasResource, aliasCreated] = resources.declareResource("Basics.Triangle.Copy",
            std::make_shared<resource_parsers::FileProgramStream>(&resources, root + "vpp/Triangle.program.yaml"));
        require(!aliasCreated && aliasResource == triangleResource && resources.isResourceAlias("Basics.Triangle.Copy"),
            "identical program source did not alias");
        requireFails("duplicate declaration", "already exists",
            [&] { resources.declareResource("Basics.Triangle", std::make_shared<resource_parsers::FileProgramStream>(
                &resources, root + "vpp/Triangle.program.yaml")); });

        requireFails("binding an unrealized program", "not loaded", [&] { triangle->bind(); });

        triangle->load();
        require(triangle->getId() != 0 && triangle->getIdCount() == 1 && triangle->getLiveIdCount() == 1,
            "realized pipeline ID accounting wrong");
        // Reflected metadata through the retained accessors.
        auto const& attributes = triangle->getVertexAttributes();
        require(attributes.size() == 2 && attributes[0].name == "POSITION" && attributes[0].numComponents == 3 &&
            attributes[1].name == "COLOUR" && attributes[1].numComponents == 4 &&
            attributes[1].streamOffset == 3, "vertex attribute reflection wrong");
        require(triangle->getViewPosId() >= 0 && triangle->getModelMatrixId() >= 0 &&
            triangle->getModelCameraProjectionMatrixId() >= 0 && triangle->getNormalMatrixId() >= 0,
            "authoring-token uniform IDs missing");
        require(triangle->getHalfWindowSizeId() == -1 && triangle->getPointSizeId() == -1, "absent tokens must report -1");
        require(triangle->getUniformId("model") == triangle->getModelMatrixId(), "marked-up uniform lookup wrong");
        require(triangle->getUniformId("MISSING") == -1, "undeclared uniform lookup wrong");
        std::string outputDiagnostic;
        require(triangle->validateFragmentOutputLocations(1, outputDiagnostic), "fragment output location 0 missing");
        require(!triangle->validateFragmentOutputLocations(2, outputDiagnostic) && !outputDiagnostic.empty(),
            "missing fragment output location not reported");
        require(triangle->getNumSamplers() == 0, "triangle program must have no samplers");
        // Generated uniform block: zero-initialized storage per frame context.
        auto const contexts = programFrameContextCount();
        require(contexts == 2, "frame context count mismatch");
        auto const blockSize = programUniformBlockSize(*triangle);
        require(blockSize >= 4 * 16, "generated uniform block too small for the token matrices");
        for (size_t context = 0; context < contexts; ++context)
            require(readbackProgramUniforms(*triangle, context) == std::vector<uint8_t>(blockSize, 0),
                "uniform block not zero-initialized");
        triangle->bind(); // selects the realized pipeline for subsequent operations

        // Runtime shader authoring through a programmatic stream: material
        // colour flows through the generated uniform block.
        auto triangleStreamPtr = triangle->getResourceStream();
        auto* triangleStream = static_cast<ProgramStream*>(triangleStreamPtr.get());
        auto const& spec = triangleStream->getMeshSpecification();
        std::string const vertex =
            "@@Version\n"
            "void main() { @Out(vec4 COLOUR) = @In(COLOUR); gl_Position = @MCPMatrix * vec4(@In(POSITION), 1); }\n";
        std::string const fragment =
            "@@Version\n@@Uniform(vec4 TINT);\n"
            "void main() { @Out(vec4 COLOUR) = @In(COLOUR) * @Uniform(TINT); }\n";
        auto tintedParser = makeParser(spec, "Runtime.Tinted", vertex, fragment);
        auto tintedStream = std::make_shared<ProgrammaticProgramStream>(&resources);
        tintedStream->setParser(tintedParser);
        auto [tintedResource, tintedCreated] = resources.declareResource("Runtime.Tinted", tintedStream);
        require(tintedCreated, "tinted program was not declared");

        // Logical material over the authored program via the public stream API.
        auto materialStream = std::make_shared<ProgrammaticBasicMaterialStream>(&resources);
        materialStream->setProgram("Runtime.Tinted");
        materialStream->setUniform("TINT", glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
        auto [materialResource, materialCreated] = resources.declareResource("Tinted", materialStream);
        require(materialCreated, "material was not declared");
        auto* material = static_cast<BasicMaterial*>(materialResource.get());
        material->load();
        require(material->getProgram() == tintedResource, "material did not acquire its program");
        require(material->getNumTextures() == 0, "tinted material must have no textures");
        auto* tinted = static_cast<Program*>(tintedResource.get());
        require(tinted->isLoaded() && tinted->getLiveIdCount() == 1, "material load did not realize the program pipeline");
        auto const tintId = tinted->getUniformId("TINT");
        require(tintId >= 0, "material uniform missing from reflected block");
        require(tinted->getUniformGlType("TINT") == 0x8B52 /* GL_FLOAT_VEC4 */, "uniform type vocabulary wrong");

        // Material values reach descriptor-backed storage in every frame context.
        material->setUniforms();
        frame(2);
        auto expectTint = [&](glm::vec4 const& value) {
            for (size_t context = 0; context < contexts; ++context) {
                auto bytes = readbackProgramUniforms(*tinted, context);
                glm::vec4 actual;
                std::memcpy(&actual, bytes.data() + tintId, sizeof(actual));
                if (actual != value) {
                    auto bits = [](float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; };
                    throw std::runtime_error("uniform value did not reach frame context " + std::to_string(context) +
                        " (offset " + std::to_string(tintId) + ", got " + std::to_string(actual.x) + "," +
                        std::to_string(actual.y) + "," + std::to_string(actual.z) + "," + std::to_string(actual.w) +
                        " expected " + std::to_string(value.x) + "," + std::to_string(value.y) + "," +
                        std::to_string(value.z) + "," + std::to_string(value.w) +
                        " bits " + std::to_string(bits(actual.x)) + " vs " + std::to_string(bits(value.x)) +
                        " block " + std::to_string(bytes.size()) + "B)");
                }
            }
        };
        expectTint(glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
        // Runtime uniform edits flow through the retained stream/recreate seam.
        // UniformCollection::setUniform emplaces (never overwrites), so an edit
        // replaces the whole collection on the stream.
        auto recreateMaterial = [&] { material->destroy(); material->load(); };
        auto setMaterialUniforms = [&](UniformCollection uniforms) {
            materialStream->setUniforms(uniforms);
            recreateMaterial();
            material->setUniforms();
        };
        {
            UniformCollection edited;
            edited.setUniform("TINT", glm::vec4(1.0f, 0.0f, 0.5f, 1.0f));
            setMaterialUniforms(edited);
        }
        expectTint(glm::vec4(1.0f, 0.0f, 0.5f, 1.0f));
        // Sparse overrides: undeclared uniforms are no-ops; unwritten members
        // keep their last uploaded values.
        {
            UniformCollection sparse;
            sparse.setUniform("MISSING", 1.0f);
            setMaterialUniforms(sparse);
        }
        expectTint(glm::vec4(1.0f, 0.0f, 0.5f, 1.0f));
        // Type mismatches fail naming the program and uniform.
        {
            UniformCollection wrong;
            wrong.setUniform("TINT", int32_t(1));
            materialStream->setUniforms(wrong);
            recreateMaterial();
        }
        requireFails("int into vec4 material uniform", "_mpp_u_TINT_", [&] { material->setUniforms(); });
        {
            UniformCollection restored;
            restored.setUniform("TINT", glm::vec4(0.0f, 1.0f, 0.25f, 1.0f));
            setMaterialUniforms(restored);
        }
        expectTint(glm::vec4(0.0f, 1.0f, 0.25f, 1.0f));

        // Runtime shader authoring: edit source, unload/reload, and the
        // affected pipeline is rebuilt without restarting the renderer.
        auto const originalId = tinted->getId();
        auto const originalRevision = tinted->getFragmentOutputRevision();
        frame(2); // leave the old pipeline potentially referenced by in-flight frames
        tinted->unload();
        require(tinted->getId() == 0 && tinted->getLiveIdCount() == 0, "unloaded program still realized");
        tintedParser->setFragmentSource(
            "@@Version\n@@Uniform(vec4 TINT);\n"
            "void main() { @Out(vec4 COLOUR) = vec4(1) - @In(COLOUR) * @Uniform(TINT); }\n");
        tintedStream->unload();
        tinted->load();
        require(tinted->getId() != 0 && tinted->getId() != originalId, "edited program did not rebuild its pipeline");
        require(tinted->getFragmentOutputRevision() != originalRevision, "reflection revision did not advance");
        require(tinted->getUniformId("TINT") >= 0, "edited program lost its material uniform");
        material->setUniforms();
        expectTint(glm::vec4(0.0f, 1.0f, 0.25f, 1.0f));
        frame(2);

        // A failing rebuild leaves the program unloaded and reports stage,
        // program name and source diagnostic; fixing the source recovers.
        tinted->unload();
        tintedParser->setFragmentSource("@@Version\nvoid main() { broken; }\n");
        tintedStream->unload();
        requireFails("invalid edited shader", "Runtime.Tinted", [&] { tinted->load(); });
        require(tinted->getId() == 0 && !tinted->isLoaded(), "failed rebuild left partial realization");
        tintedParser->setFragmentSource(
            "@@Version\n@@Uniform(vec4 TINT);\n"
            "void main() { @Out(vec4 COLOUR) = @In(COLOUR) * @Uniform(TINT); }\n");
        tintedStream->unload();
        tinted->load();
        require(tinted->getLiveIdCount() == 1, "recovered program did not realize");

        // Invalid binding/source combinations fail naming resource and binding.
        {
            auto samplerStream = std::make_shared<ProgrammaticProgramStream>(&resources);
            samplerStream->setParser(makeParser(spec, "Runtime.Sampler", vertex,
                "@@Version\n@@Texture(sampler2D TEX0);\n"
                "void main() { @Out(vec4 COLOUR) = texture(@Texture(TEX0), vec2(0)); }\n"));
            Program samplerProgram("Runtime.Sampler", &renderer, &resources, samplerStream);
            requireFails("sampler realization", "not delivered", [&] { samplerProgram.load(); });
            requireFails("sampler diagnostic names the program", "Runtime.Sampler", [&] { samplerProgram.load(); });
        }
        {
            auto blockStream = std::make_shared<ProgrammaticProgramStream>(&resources);
            blockStream->setParser(makeParser(spec, "Runtime.AuthoredBlock", vertex,
                "@@Version\nlayout(std140, set=1, binding=3) uniform Material { vec4 tint; } material;\n"
                "void main() { @Out(vec4 COLOUR) = @In(COLOUR) * material.tint; }\n"));
            Program blockProgram("Runtime.AuthoredBlock", &renderer, &resources, blockStream);
            requireFails("authored-block realization names the binding", "set 1 binding 3", [&] { blockProgram.load(); });
        }
        {
            // Shader declares an input the authored mesh layout does not supply.
            auto mismatchedStream = std::make_shared<ProgrammaticProgramStream>(&resources);
            mismatchedStream->setParser(makeParser(spec, "Runtime.Mismatched",
                "@@Version\nlayout(location=2) in vec2 _mpp_i_TEXCOORDS;\n"
                "void main() { @Out(vec4 COLOUR) = @In(COLOUR); gl_Position = vec4(@In(POSITION) + vec3(_mpp_i_TEXCOORDS, 0), 1); }\n",
                "@@Version\nvoid main() { @Out(vec4 COLOUR) = @In(COLOUR); }\n"));
            Program mismatchedProgram("Runtime.Mismatched", &renderer, &resources, mismatchedStream);
            requireFails("mesh/shader layout mismatch names the program", "Runtime.Mismatched", [&] { mismatchedProgram.load(); });
            requireFails("mesh/shader layout mismatch explains the inputs", "vertex input mismatch", [&] { mismatchedProgram.load(); });
        }
        {
            // A program without generated uniforms owns no uniform block.
            auto plainStream = std::make_shared<ProgrammaticProgramStream>(&resources);
            plainStream->setParser(makeParser(spec, "Runtime.NoUniforms",
                "@@Version\nvoid main() { @Out(vec4 COLOUR) = @In(COLOUR); gl_Position = vec4(@In(POSITION), 1); }\n",
                "@@Version\nvoid main() { @Out(vec4 COLOUR) = @In(COLOUR); }\n"));
            Program plainProgram("Runtime.NoUniforms", &renderer, &resources, plainStream);
            plainProgram.load();
            require(plainProgram.getLiveIdCount() == 1 && programUniformBlockSize(plainProgram) == 0,
                "uniform-free program must realize without a uniform block");
            requireFails("uniform readback without a block", "no generated uniform block", [&] {
                readbackProgramUniforms(plainProgram, 0); });
            plainProgram.bind();
        }
        {
            // A material cannot resolve an undeclared program resource.
            auto orphanStream = std::make_shared<ProgrammaticBasicMaterialStream>(&resources);
            orphanStream->setProgram("No.Such.Program");
            requireFails("material with a missing program", "No.Such.Program", [&] {
                BasicMaterial orphan("Orphan", &renderer, &resources, orphanStream);
                orphan.load();
            });
        }
        // Undelivered resource types fail explicitly at declaration.
        requireFails("undelivered resource type", "not delivered", [&] {
            resources.declareResource("Sampler.Nope", std::make_shared<ProgrammaticSamplerStream>(&resources));
        });
        requireFails("missing resource lookup", "not found", [&] { resources.getResource("Missing.Resource"); });

        // Drawing remains undelivered here (#96) and fails explicitly.
        tinted->bind();

        // Unload/reload while frames are in flight; teardown retires every
        // native object without validation diagnostics.
        frame(3);
        tinted->unload();
        frame(2);
        require(tinted->getLiveIdCount() == 0, "retired pipeline still live");
        tinted->load();
        frame(3);
        requireFails("uniform readback of an unrealized program", "not realized", [&] {
            Program empty("Empty", &renderer, &resources, nullptr);
            readbackProgramUniforms(empty, 0); });
        requireFails("uniform readback beyond the frame contexts", "frame context", [&] {
            readbackProgramUniforms(*tinted, contexts); });

        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "PASS programs/materials realized as native pipelines and descriptors, "
                     "uniform values verified per frame context, runtime rebuilds, explicit subset failures; elapsed_ms=" << ms << '\n';
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; result = 1; }
    SDL_Quit();
    return result;
}
