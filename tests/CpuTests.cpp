#include <iostream>
#include <filesystem>
#include <fstream>
#include "mpp/ResourceStreamSerializer.h"
#include "mpp/ProgrammaticSamplerStream.h"
#include "mpp/ProgrammaticStringStream.h"
#include <stdexcept>
#include "mpp/DiagnosticTests.h"
#include "mpp/RenderGraphTests.h"
#include "mpp/PbrMaterialTests.h"
#include "mpp/UniformCollection.h"
#include "mpp/RenderGraphBuiltInPasses.h"
#include "mpp/RenderGraphPassFactoryRegistry.h"
#include "mpp/RenderGraphScenePass.h"
#include "mpp/resource-parsers/RenderGraphResourceTests.h"
#include "mpp/resource-parsers/ParticleResourceTests.h"
#include "mpp/app/DocumentFoundationTests.h"

bool binaryRoundTrip(std::string* failure) {
    auto path = std::filesystem::temp_directory_path() / "vpp_cpu_resource_roundtrip.rse";
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{path};
    try {
        auto sampler = std::make_shared<mpp::ProgrammaticSamplerStream>(nullptr);
        sampler->setFiltering(mpp::SamplerParams::MinFilter::Linear, mpp::SamplerParams::MagFilter::Nearest);
        sampler->setLodBias(0.75f);
        auto text = std::make_shared<mpp::ProgrammaticStringStream>(nullptr);
        text->setString("CPU resource round trip");
        sampler->addChild("Text", text);
        sampler->load();
        mpp::ResourceStreamSerializer serializer(nullptr);
        serializer.serialize(sampler, path.string());
        auto bytes = [&] { std::ifstream file(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(file), {}); };
        auto original = bytes();
        auto restored = serializer.deserialize(path.string());
        restored->load();
        auto restoredSampler = std::dynamic_pointer_cast<mpp::SamplerStream>(restored);
        auto restoredText = std::dynamic_pointer_cast<mpp::StringStream>(restored->getChildren().at("Text"));
        if (!restoredSampler || !restoredText || restoredSampler->getParams().lodBias != 0.75f || restoredText->getString() != "CPU resource round trip")
            throw std::runtime_error("Binary stream lost sampler/child string values");
        serializer.serialize(restored, path.string());
        if (original.substr(0, 4) != "RSE5" || original != bytes())
            throw std::runtime_error("Binary stream did not retain RSE5 bytes");
        return true;
    } catch (std::exception const& error) { *failure = error.what(); return false; }
}

bool shaderCompilationTests(std::string* failure);
#ifdef VPP_GEOMETRY_TESTS
bool geometryTranslationTests(std::string* failure);
#endif

int main() {
    struct Test { char const* name; bool (*run)(std::string*); };
    Test tests[] = {
        {"Vulkan runtime shader compilation/reflection", shaderCompilationTests},
#ifdef VPP_GEOMETRY_TESTS
        {"Vulkan vertex-format boundary translation", geometryTranslationTests},
#endif
        {"RSE5 binary round trip", binaryRoundTrip},
        {"diagnostics", mpp::runDiagnosticTests},
        {"graph topology", mpp::runRenderGraphTopologyTests},
        {"PBR specialization", mpp::runPbrMaterialSpecializationTests},
        {"XML/YAML resource round trips", mpp::resource_parsers::runRenderGraphResourceTests},
        {"particle resources", mpp::resource_parsers::runParticleResourceTests},
        {"application document foundations", mpp::app::runDocumentFoundationTests}
    };
    for (auto const& test : tests) {
        std::string failure;
        if (!test.run(&failure)) { std::cerr << test.name << ": " << failure << '\n'; return 1; }
        std::cout << "PASS " << test.name << '\n';
    }
#ifdef VPP_CPU_ONLY
    auto rejects = [](auto operation) {
        try { operation(); }
        catch (std::exception const& error) { return std::string(error.what()).find("not-yet-delivered Vulkan") != std::string::npos; }
        return false;
    };
    mpp::RenderGraphPassFactoryRegistry registry;
    mpp::registerBuiltInRenderGraphPasses(registry);
    if (!rejects([] { mpp::UniformCollection{}.bindUniforms({}); }) ||
        !rejects([] { mpp::ProgrammaticStringStream(nullptr).createChildResources("Test"); }) ||
        !rejects([&] { registry.createScenePass("MPP.PbrScene"); })) {
        std::cerr << "An unsupported CPU-only operation did not fail explicitly\n"; return 1;
    }
    std::cout << "PASS uniform binding, resource realization and GPU pass creation rejected\n";
    return 0;
#else
    return 0;
#endif
}
