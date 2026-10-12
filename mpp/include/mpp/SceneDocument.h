#pragma once
#include <string>
#include <vector>
#include <optional>
#include <glm/vec3.hpp>
#include "mpp/Config.h"
#include "mpp/Diagnostic.h"
namespace mpp {
enum class SceneModelSource { MppModel, Box, Sphere, Cylinder, Grid };
struct _MPPAPI ScenePrimitiveDocument { float width{1}, height{1}, depth{1}; float radius{1}, topRadius{1}; uint32_t resolution{3}, segmentsX{10}, segmentsZ{10}; float textureRepeatU{1}, textureRepeatV{1}; };
struct _MPPAPI SceneModelDocument { std::string id; SceneModelSource source{SceneModelSource::Sphere}; std::string file; ScenePrimitiveDocument primitive; glm::vec3 translation{0}, rotationDegrees{0}, scale{1}; std::vector<std::string> layers; std::string materialBinding; bool visible{true}; bool shadowCaster{true}; };
enum class SceneLightType { Directional, Point };
struct _MPPAPI SceneLightDocument { std::string id; SceneLightType type{SceneLightType::Directional}; glm::vec3 colour{1}; float intensity{1}; glm::vec3 position{0}; glm::vec3 direction{0,-1,0}; float range{0}; bool castsShadows{false}; };
struct _MPPAPI SceneParticleEffectDocument { std::string id; std::string effect; glm::vec3 translation{0}, rotationDegrees{0}, scale{1}; bool visible{true}; };
_MPPAPI uint64_t scenePrimitiveTriangleCount(SceneModelDocument const& model);
struct _MPPAPI SceneCameraDocument { glm::vec3 position{0,2,8}; glm::vec3 target{0}; float fov{60}; float nearPlane{0.1f}; float farPlane{2000}; };
class _MPPAPI SceneDocument { public: static constexpr uint32_t CurrentVersion=1; uint32_t version{1}; std::string sourcePath; std::string name; std::string environmentBinding; SceneCameraDocument camera; std::vector<std::string> layers; std::vector<SceneModelDocument> models; std::vector<SceneLightDocument> lights; std::vector<SceneParticleEffectDocument> particleEffects; uint64_t getKnownTriangleCount() const; size_t getUnknownTriangleModelCount() const; std::optional<glm::vec3> getShadowLightDirection() const; std::optional<size_t> getShadowLightIndex() const; DiagnosticBag validate() const; };
}
