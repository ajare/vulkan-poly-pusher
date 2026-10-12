#include <iostream>
#include <stdexcept>
#include "mpp/Diagnostic.h"
#include "mpp/resource-parsers/FileTextureStream.h"
#include "mpp/resource-parsers/FileSamplerStream.h"
#include "mpp/resource-parsers/SceneParser.h"
#include "mpp/resource-parsers/SceneSerializer.h"
#include "mpp/resource-parsers/LegacyPipelineParser.h"
#include "mpp/resource-parsers/LegacyPipelineSerializer.h"
#include "mpp/resource-parsers/LegacyPipelineResourceValidator.h"
#include "mpp/resource-parsers/PbrPipelineParser.h"
#include "mpp/resource-parsers/PbrPipelineSerializer.h"
#include "mpp/resource-parsers/PbrPipelineResourceValidator.h"
#include "mpp/resource-parsers/ParticleEffectParser.h"
#include "mpp/resource-parsers/ParticleEffectSerializer.h"

// Shared CPU comparison client: the same source builds against frozen MPP.
// No window, context, device, engine resource manager or runtime is created.
int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: VppAssetValidate scene|legacy|pbr|particle|texture|sampler input.xml|yaml [roundtrip.xml|yaml]\n";
        return 2;
    }
    using namespace mpp::resource_parsers;
    try {
        mpp::DiagnosticBag diagnostics;
        std::string type = argv[1];
        if (type == "scene") {
            auto document = SceneParser::fromFile(argv[2]);
            diagnostics = document.validate();
            if (argc == 4 && !diagnostics.hasErrors()) SceneSerializer::toFile(document, argv[3]);
        } else if (type == "legacy") {
            auto document = LegacyPipelineParser::fromFile(argv[2]);
            diagnostics = document.validate();
            diagnostics.append(validateLegacyPipelineResourceDefinitions(document));
            if (argc == 4 && !diagnostics.hasErrors()) LegacyPipelineSerializer::toFile(document, argv[3]);
        } else if (type == "pbr") {
            auto document = PbrPipelineParser::fromFile(argv[2]);
            diagnostics = document.validate();
            diagnostics.append(validatePbrPipelineResourceDefinitions(document));
            if (argc == 4 && !diagnostics.hasErrors()) PbrPipelineSerializer::toFile(document, argv[3]);
        } else if (type == "particle") {
            auto result = ParticleEffectParser::fromFile(argv[2]);
            diagnostics = result.diagnostics;
            if (argc == 4 && !diagnostics.hasErrors()) ParticleEffectSerializer::toFile(result.specification, argv[3]);
        } else if (type == "texture" || type == "sampler") {
            if (argc == 4) { std::cerr << "Texture/sampler round-trip is not supported by this client\n"; return 2; }
            if (type == "texture") {
                FileTextureStream stream(nullptr, argv[2]); stream.load();
                auto const& params = stream.getParams();
                std::cout << stream.getInternalFormat() << ' ' << stream.getTarget() << ' '
                          << params.minFilter << ' ' << params.magFilter << ' ' << params.wrap << '\n';
            } else {
                FileSamplerStream stream(nullptr, argv[2]); stream.load();
                auto const& params = stream.getParams();
                std::cout << params.minFilter << ' ' << params.magFilter << ' ' << params.wrap << '\n';
            }
        } else {
            std::cerr << "Unsupported document operation: " << type << '\n'; return 2;
        }
        for (auto const& diagnostic : diagnostics.getDiagnostics())
            std::cout << diagnostic.code << ' ' << mpp::diagnosticSeverityName(diagnostic.severity)
                      << ' ' << diagnostic.objectId << ": " << diagnostic.message << '\n';
        return diagnostics.hasErrors() ? 1 : 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
