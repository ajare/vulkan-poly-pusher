#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <glm/geometric.hpp>
#include "mpp/SceneDocument.h"

using namespace std;

namespace mpp
{
	namespace
	{
		bool finite(glm::vec3 const& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	}

	uint64_t scenePrimitiveTriangleCount(SceneModelDocument const& model)
	{
		if (!model.visible) return 0;
		switch (model.source)
		{
		case SceneModelSource::Box: return 12;
		case SceneModelSource::Sphere:
		{
			uint64_t triangles = 20;
			for (uint32_t level = 1; level < model.primitive.resolution; ++level) triangles *= 4;
			return triangles;
		}
		case SceneModelSource::Cylinder: return uint64_t(model.primitive.resolution) * 4;
		case SceneModelSource::Grid: return uint64_t(model.primitive.segmentsX) * model.primitive.segmentsZ * 2;
		default: return 0;
		}
	}

	uint64_t SceneDocument::getKnownTriangleCount() const
	{
		uint64_t result = 0;
		for (auto const& model : models) result += scenePrimitiveTriangleCount(model);
		return result;
	}

	size_t SceneDocument::getUnknownTriangleModelCount() const
	{
		return count_if(models.begin(), models.end(), [](auto const& model) { return model.visible && model.source == SceneModelSource::MppModel; });
	}

	optional<glm::vec3> SceneDocument::getShadowLightDirection() const
	{
		for (auto const& light : lights) if (light.castsShadows && light.type == SceneLightType::Directional) return light.direction;
		return nullopt;
	}

	optional<size_t> SceneDocument::getShadowLightIndex() const
	{
		for (size_t index = 0; index < lights.size(); ++index)
			if (lights[index].castsShadows) return index;
		return nullopt;
	}

	DiagnosticBag SceneDocument::validate() const
	{
		DiagnosticBag diagnostics;
		if (version != CurrentVersion) diagnostics.error("MPP-SCENE-001", "Unsupported Scene version.", { sourcePath });
		if (name.empty()) diagnostics.error("MPP-SCENE-002", "Scene name is required.", { sourcePath });

		set<string> declaredLayers;
		for (auto const& layer : layers)
		{
			if (layer.empty() || !declaredLayers.insert(layer).second)
				diagnostics.error("MPP-SCENE-019", "Declared render layers must be non-empty and unique.", { sourcePath }, layer);
		}

		bool const implicitLegacyLayers = layers.empty();
		set<string> ids;
		bool hasShadowCasters = false;
		for (auto const& model : models)
		{
			if (model.id.empty() || !ids.insert(model.id).second) diagnostics.error("MPP-SCENE-003", "Model IDs must be non-empty and unique.", { sourcePath }, model.id);
			if (model.source == SceneModelSource::MppModel && model.file.empty()) diagnostics.error("MPP-SCENE-004", "MPP model file is required.", { sourcePath }, model.id);
			else if (model.source == SceneModelSource::MppModel)
			{
				auto path = filesystem::path(model.file);
				if (path.is_absolute()) diagnostics.warning("MPP-SCENE-009", "Absolute model path is not portable.", { sourcePath }, model.id);
				auto resolved = path.is_absolute() ? path : filesystem::path(sourcePath).parent_path() / path;
				if (!filesystem::exists(resolved)) diagnostics.warning("MPP-SCENE-010", "Model file does not exist; runtime placeholder will be used: " + resolved.string(), { sourcePath }, model.id);
			}
			if (model.source == SceneModelSource::Box && (model.primitive.width <= 0 || model.primitive.height <= 0 || model.primitive.depth <= 0)) diagnostics.error("MPP-SCENE-015", "Box dimensions must be positive.", { sourcePath }, model.id);
			if (model.source == SceneModelSource::Sphere && (model.primitive.radius <= 0 || model.primitive.resolution > 8)) diagnostics.error("MPP-SCENE-016", "Sphere radius must be positive and subdivision resolution cannot exceed eight.", { sourcePath }, model.id);
			if (model.source == SceneModelSource::Cylinder && (model.primitive.height <= 0 || model.primitive.radius < 0 || model.primitive.topRadius < 0 || (model.primitive.radius == 0 && model.primitive.topRadius == 0) || model.primitive.resolution < 3)) diagnostics.error("MPP-SCENE-017", "Cylinder length/radii/resolution are invalid.", { sourcePath }, model.id);
			if (model.source == SceneModelSource::Grid && (model.primitive.width <= 0 || model.primitive.depth <= 0 || model.primitive.segmentsX == 0 || model.primitive.segmentsZ == 0)) diagnostics.error("MPP-SCENE-018", "Grid dimensions and segment counts must be positive.", { sourcePath }, model.id);
			if (!finite(model.translation) || !finite(model.rotationDegrees) || !finite(model.scale)) diagnostics.error("MPP-SCENE-026", "Model transform values must be finite.", { sourcePath }, model.id);
			if (model.scale.x == 0 || model.scale.y == 0 || model.scale.z == 0) diagnostics.error("MPP-SCENE-027", "Model scale components cannot be zero.", { sourcePath }, model.id);
			set<string> modelLayers;
			for (auto const& layer : model.layers)
			{
				if (layer.empty()) diagnostics.error("MPP-SCENE-011", "Layer names cannot be empty.", { sourcePath }, model.id);
				else if (!modelLayers.insert(layer).second) diagnostics.error("MPP-SCENE-020", "A model cannot reference the same render layer more than once: " + layer, { sourcePath }, model.id);
				else if (!implicitLegacyLayers && !declaredLayers.contains(layer)) diagnostics.error("MPP-SCENE-021", "Model references undeclared render layer '" + layer + "'.", { sourcePath }, model.id);
			}
			if (model.materialBinding.empty()) diagnostics.warning("MPP-SCENE-005", "Model has no logical material binding.", { sourcePath }, model.id);
			hasShadowCasters |= model.visible && model.shadowCaster;
		}

		if (implicitLegacyLayers && any_of(models.begin(), models.end(), [](auto const& model) { return !model.layers.empty(); })) diagnostics.warning("MPP-SCENE-030", "Legacy implicit render layers are accepted; declare them in Scene/Layers when saving.", { sourcePath }, "layers");

		size_t shadowLights = 0;
		for (auto const& light : lights)
		{
			if (light.id.empty() || !ids.insert(light.id).second) diagnostics.error("MPP-SCENE-006", "Light IDs must be non-empty and unique.", { sourcePath }, light.id);
			if (light.intensity < 0 || light.range < 0) diagnostics.error("MPP-SCENE-012", "Light intensity and range cannot be negative.", { sourcePath }, light.id);
			if (!finite(light.colour) || !finite(light.position) || !finite(light.direction) || !std::isfinite(light.intensity) || !std::isfinite(light.range)) diagnostics.error("MPP-SCENE-026", "Light values must be finite.", { sourcePath }, light.id);
			if (light.colour.x < 0 || light.colour.y < 0 || light.colour.z < 0) diagnostics.error("MPP-SCENE-025", "Light colour components cannot be negative.", { sourcePath }, light.id);
			if (light.type == SceneLightType::Directional && glm::dot(light.direction, light.direction) < 0.000001f) diagnostics.error("MPP-SCENE-013", "Directional light direction cannot be zero.", { sourcePath }, light.id);
			if (light.castsShadows)
			{
				++shadowLights;
				if (light.type == SceneLightType::Point && light.range <= 0)
					diagnostics.error("MPP-SCENE-023", "Shadow-casting point light range must be positive.", { sourcePath }, light.id);
			}
		}
		for (auto const& effect : particleEffects)
		{
			if (effect.id.empty() || !ids.insert(effect.id).second) diagnostics.error("MPP-SCENE-031", "Particle effect IDs must be non-empty and unique across scene objects.", { sourcePath }, effect.id);
			if (effect.effect.empty()) diagnostics.error("MPP-SCENE-032", "Particle effect resource is required.", { sourcePath }, effect.id);
			if (!finite(effect.translation) || !finite(effect.rotationDegrees) || !finite(effect.scale)) diagnostics.error("MPP-SCENE-033", "Particle effect transform values must be finite.", { sourcePath }, effect.id);
			if (effect.scale.x == 0 || effect.scale.y == 0 || effect.scale.z == 0) diagnostics.error("MPP-SCENE-034", "Particle effect scale components cannot be zero.", { sourcePath }, effect.id);
		}

		if (lights.size() > 8) diagnostics.error("MPP-SCENE-014", "Scene exceeds the renderer limit of eight PBR lights.", { sourcePath }, "lights");
		if (shadowLights > 1) diagnostics.error("MPP-SCENE-024", "Scenes support only one shadow-casting light.", { sourcePath }, "lights");
		if (hasShadowCasters && shadowLights == 0) diagnostics.warning("MPP-SCENE-028", "The scene has shadow-casting models but no shadow-casting light.", { sourcePath }, "lights");
		if (!hasShadowCasters && shadowLights != 0) diagnostics.warning("MPP-SCENE-029", "The shadow light has no visible shadow-casting models.", { sourcePath }, "lights");
		if (camera.nearPlane <= 0 || camera.farPlane <= camera.nearPlane || !finite(camera.position) || !finite(camera.target) || !std::isfinite(camera.fov)) diagnostics.error("MPP-SCENE-007", "Camera values or clip distances are invalid.", { sourcePath }, "camera");
		if (environmentBinding.empty()) diagnostics.warning("MPP-SCENE-008", "Scene has no logical environment binding.", { sourcePath }, "environment");
		return diagnostics;
	}
}
