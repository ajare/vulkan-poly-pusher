#pragma once

#include <cstdlib>
#include <memory>
#include <array>

#include <glm/gtc/matrix_transform.hpp>

#include "mpp/Config.h"
#include "mpp/ResourceWrangler.h"
#include "mpp/Material.h"
#include "mpp/Mesh.h"
#include "mpp/UniformCollection.h"
#include "mpp/VertexBufferRenderCommand.h"

namespace mpp
{
	class SceneModel3d;

	class _MPPAPI alignas(16) MeshInstance : public ResourceWrangler
	{
		friend class RenderSystem;
		friend class ModelInstance;

	private:

		Mesh const* mwMesh;
		SceneModel3d const* mSourceSceneObject;

		bool mRender;

		bool mWireframe;

		bool mCullBackFaces;

		bool mBlend;

		// Only PBR Blend materials need depth ordering. Legacy callers retain
		// their existing batching/sort behaviour.
		bool mSortTransparent;

		float mPointSize;

		size_t mInstanceCount;

		std::vector<VertexBufferRenderCommand> mRenderCommands;

		ResourcePtr mMaterial;

		float mGamma;

		std::vector<ResourcePtr> mTextureOverrides;

		std::shared_ptr<UniformCollection> mUniforms;

#pragma warning(push)
#pragma warning(disable: 4324)
		alignas(16) glm::vec3 mViewPos;
		alignas(16) glm::mat4 mModelMatrix;
		alignas(16) glm::mat4 mModelCameraProjectionMatrix;
		alignas(16) glm::mat4 mLocalTransform;
		alignas(16) glm::mat3 mNormalMatrix;
		alignas(16) glm::vec2 mHalfWindowSize;
#pragma warning(pop)

	private:

		void commonSetup(Mesh const* mesh, float gamma);

		void teardown();

	public:

		MeshInstance();

		virtual ~MeshInstance();

		bool operator <(MeshInstance const* other);

		void setup(Mesh const* mesh, glm::vec3 const& viewPos, glm::mat4 const& modelMatrix, glm::mat4 const& modelCameraProjMatrix, glm::mat3 const& normalMatrix, glm::vec2 const& halfWindowSize, float pointSize, float gamma);

		void setup(Mesh const* mesh, glm::vec3 const& viewPos, glm::mat4 const& modelMatrix, glm::mat4 const& modelCameraProjMatrix, glm::mat3 const& normalMatrix, float pointSize, float gamma);

		void setup(Mesh const* mesh, glm::vec3 const& viewPos, glm::mat4 const& modelMatrix, glm::mat4 const& modelCameraProjMatrix, glm::vec2 const& halfWindowSize, float pointSize, float gamma);

		void release();

		void render(bool render);

		bool render() const;

		void wireframe(bool wireframe);

		bool wireframe() const;

		void cullBackFaces(bool cullBackFaces);

		bool cullBackFaces() const;

		void blend(bool blend);

		bool blend() const;

		void sortTransparent(bool sortTransparent);

		bool sortTransparent() const;

		void setPointSize(float pointSize);

		float getPointSize() const;

		void setInstanceCount(size_t instanceCount);

		size_t getInstanceCount() const;

		void setGamma(float gamma);

		float getGamma() const;

		void setMaterial(ResourcePtr material);

		ResourcePtr getMaterial();

		void setTexture(int index, ResourcePtr texture);

		ResourcePtr getTexture(int texture);

		void setUniformCollection(std::shared_ptr<UniformCollection> uniforms);

		std::shared_ptr<UniformCollection> getUniformCollection();

		void setRenderCount(uint32_t count);

		void addRenderCommand(VertexBufferRenderCommand const& renderCmd);

		void translate(glm::vec3 const& translate);

		void scale(glm::vec3 const& scale);

		void rotate(glm::vec3 const& axis, float angle);

		void bindUniforms();
	};
}

