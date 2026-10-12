#pragma once

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

#include <memory>
#include <vector>
#include <string>

#include "mpp/Config.h"
#include "mpp/MeshInstance.h"
#include "mpp/Resource.h"
#include "mpp/Model.h"
#include "mpp/ModelRenderParams.h"
#include "mpp/Pool.h"

namespace mpp
{
	class RenderSystem;
	class SceneModel3d;

	class _MPPAPI ModelInstance
	{
		std::map<std::string, MeshInstance*> mMeshInstances;

		std::vector<MeshInstance*> mOrderedMeshInstances;

	private:

		void teardown();

	public:

		~ModelInstance();

		void setup(Model const& model, glm::vec3 const& viewPos, glm::mat4 const& modelMatrix, glm::mat4 const& modelCameraProjMatrix, glm::mat3 const& normalMatrix, glm::vec2 const& halfWindowSize, float gamma, Pool<MeshInstance>* pool);

		void setup(Model const& model, glm::vec3 const& viewPos, glm::mat4 const& modelMatrix, glm::mat4 const& modelCameraProjMatrix, glm::mat3 const& normalMatrix, float gamma, Pool<MeshInstance>* pool);

		void setup(Model const& model, glm::vec3 const& viewPos, glm::mat4 const& modelMatrix, glm::mat4 const& modelCameraProjMatrix, glm::vec2 const& halfWindowSize, float gamma, Pool<MeshInstance>* pool);

		void release();

		std::vector<MeshInstance*>& getMeshInstances();

		std::vector<MeshInstance*> const& getMeshInstances() const;

		bool hasMeshInstance(std::string const& name) const;

		MeshInstance* getMeshInstance(std::string const& name);

		void setParams(std::shared_ptr<ModelRenderParams> params);

		void setSourceSceneObject(SceneModel3d const* source);

	};
}

