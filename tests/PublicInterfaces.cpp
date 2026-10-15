// Representative existing client declarations from every retained module.
#include <cstddef>
#include <cstdint>
#include "mpp/data/StructuredData.h"
#include "mpp/mesh/MeshSpecification.h"
#ifndef MPP_FROZEN_REFERENCE
#include "mpp/CompiledProgram.h"
#include "mpp/PipelineDiagnostics.h"
#endif
#include "mpp/Mesh.h"
#include "mpp/Program.h"
#include "mpp/BasicMaterial.h"
#include "mpp/program/Parser.h"
#include "mpp/SceneDocument.h"
#include "mpp/RenderSystem.h"
#include "mpp/ResourceManager.h"
#include "mpp/resource-parsers/SceneParser.h"
#include "mpp/resource-parsers/FileTextureStream.h"
#include "mpp/mesh-specification-parser/SpecificationParser.h"
#include "mpp/helper/FpsCamera.h"
#include "mpp/app/DocumentFile.h"
#include <type_traits>
static_assert(std::is_same_v<decltype(mpp::resource_parsers::SceneParser::fromFile("")), mpp::SceneDocument>);
static_assert(std::is_same_v<decltype(mpp::resource_parsers::FileTextureStream::parseDefinition(mpp::data::StructuredData{"Texture"}, nullptr, "").second.internalFormat), uint32_t>);
static_assert(std::is_same_v<decltype(std::declval<mpp::ResourceManager&>().getResource("", true)), mpp::ResourcePtr>);
// Shared public rendering surface: these declarations compile against both backends.
void triangleInterface(mpp::RenderSystem& renderer, mpp::Mesh& mesh, mpp::Program& program,
    mpp::BasicMaterial& material) {
    renderer.resetTransform(); renderer.pushModelMatrix();
    renderer.translateTransform3d(glm::vec3(0)); renderer.popModelMatrix();
    program.bind(); material.setUniforms(); mesh.render(1); mesh.render(1, 0, 1);
}
