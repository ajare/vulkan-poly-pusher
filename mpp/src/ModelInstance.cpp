#include "mpp/ModelInstance.h"
#include "mpp/Model.h"

using namespace std;

namespace mpp
{

	ModelInstance::~ModelInstance()
	{
		teardown();
	}

	void ModelInstance::setup(Model const& model, glm::vec3 const& viewPos, glm::mat4 const& modelMatrix, glm::mat4 const& modelCameraProjMatrix, glm::mat3 const& normalMatrix, glm::vec2 const& halfWindowSize, float gamma, Pool<MeshInstance>* pool)
	{
		teardown();

		// Iterate over model and create MeshInstances
		for (int i = 0; i < model.getNumMeshes(); ++i)
		{
			Mesh const* mesh = model.getMesh(i);

			//auto mi = new MeshInstance();
			auto mi = pool->acquireObject();

			mi->setup(mesh, viewPos, modelMatrix, modelCameraProjMatrix, normalMatrix, halfWindowSize, mesh->getPointSize(), gamma);

			string meshName = mesh->getName();
			mMeshInstances[meshName] = mi;
			mOrderedMeshInstances.push_back(mi);
		}
	}

	void ModelInstance::setup(Model const& model, glm::vec3 const& viewPos, glm::mat4 const& modelMatrix, glm::mat4 const& modelCameraProjMatrix, glm::mat3 const& normalMatrix, float gamma, Pool<MeshInstance>* pool)
	{
		teardown();

		// Iterate over model and create MeshInstances
		for (int i = 0; i < model.getNumMeshes(); ++i)
		{
			Mesh const* mesh = model.getMesh(i);

			//auto mi = new MeshInstance();
			auto mi = pool->acquireObject();

			mi->setup(mesh, viewPos, modelMatrix, modelCameraProjMatrix, normalMatrix, mesh->getPointSize(), gamma);

			mMeshInstances[mesh->getName()] = mi;
			mOrderedMeshInstances.push_back(mi);
		}
	}

	void ModelInstance::setup(Model const& model, glm::vec3 const& viewPos, glm::mat4 const& modelMatrix, glm::mat4 const& modelCameraProjMatrix, glm::vec2 const& halfWindowSize, float gamma, Pool<MeshInstance>* pool)
	{
		teardown();

		// Iterate over model and create MeshInstances
		for (int i = 0; i < model.getNumMeshes(); ++i)
		{
			Mesh const* mesh = model.getMesh(i);

			//auto mi = new MeshInstance();
			auto mi = pool->acquireObject();

			mi->setup(mesh, viewPos, modelMatrix, modelCameraProjMatrix, halfWindowSize, mesh->getPointSize(), gamma);

			mMeshInstances[mesh->getName()] = mi;
			mOrderedMeshInstances.push_back(mi);
		}
	}

	void ModelInstance::teardown()
	{
		release();

		mMeshInstances.clear();
		mOrderedMeshInstances.clear();
	}

	void ModelInstance::release()
	{
	}

	/*
	 * Get all mesh instances.
	 *
	 */
	vector<MeshInstance*>& ModelInstance::getMeshInstances()
	{
		return mOrderedMeshInstances;
	}

	/*
	 * Get all mesh instances.
	 *
	 */
	vector<MeshInstance*> const& ModelInstance::getMeshInstances() const
	{
		return mOrderedMeshInstances;
	}

	/*
	 * Check whether the named instance exists.
	 *
	 */
	bool ModelInstance::hasMeshInstance(string const& name) const
	{
		return mMeshInstances.find(name) != mMeshInstances.end();
	}

	/*
	 * Get the named mesh instance.
	 *
	 */
	MeshInstance* ModelInstance::getMeshInstance(string const& name)
	{
		return mMeshInstances[name];
	}

	void ModelInstance::setSourceSceneObject(SceneModel3d const* source)
	{
		for (auto meshInstance : mOrderedMeshInstances) meshInstance->mSourceSceneObject = source;
	}

	void ModelInstance::setParams(shared_ptr<ModelRenderParams> params)
	{
		// Set MeshInstances, etc
		auto const& p = params->getMeshParams();
		auto defaultIt = p.find("");
		
		for (auto meshInstance: mMeshInstances)
		{
			auto& mi = meshInstance.second;
			auto it = p.find(meshInstance.first);

			auto const* rp = it != p.end() ? &it->second : (defaultIt != p.end() ? &defaultIt->second : nullptr);

			if (rp)
			{
				mi->render((rp->flags & ModelRenderParams::Flag_Visible) != 0);
				mi->wireframe((rp->flags & ModelRenderParams::Flag_Wireframe) != 0);
				mi->cullBackFaces((rp->flags & ModelRenderParams::Flag_CullBackFaces) != 0);
				mi->setInstanceCount(rp->instanceCount);
				mi->setPointSize(rp->pointSize);
				mi->setUniformCollection(rp->uniforms);
				if (rp->blend.has_value())
				{
					mi->blend(*rp->blend);
				}

				if (rp->material)
				{
					mi->setMaterial(rp->material);
				}

				for (size_t i = 0; i < rp->textures.size(); ++i)
				{
					if (rp->textures[i])
					{
						mi->setTexture((int)i, rp->textures[i]);
					}
				}

				for (auto const& renderCmd : rp->renderCommands)
				{
					mi->addRenderCommand(renderCmd);
				}
			}
		}
	}
}