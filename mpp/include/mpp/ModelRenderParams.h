#pragma once

#include <memory>
#include <string>
#include <map>
#include <optional>

#include "mpp/Config.h"
#include "mpp/UniformCollection.h"
#include "mpp/Resource.h"
#include "mpp/VertexBufferRenderCommand.h"

namespace mpp
{
	class ModelRenderParams
	{
	public:

		static const uint32_t Flag_Visible		= 0x01;
		static const uint32_t Flag_Wireframe	= 0x02;
		static const uint32_t Flag_CastShadows	= 0x04;
		static const uint32_t Flag_CullBackFaces	= 0x08;

	public:

		struct MeshRenderParams
		{
			uint32_t flags{ Flag_Visible | Flag_CastShadows };
			size_t instanceCount{ 1 };
			std::vector<VertexBufferRenderCommand> renderCommands;
			float pointSize{ 1.0f };
			std::shared_ptr<UniformCollection> uniforms;
			std::optional<bool> blend;
			// Overrides automatic opaque/blended participation in a depth prepass.
			// Useful for diagnostic blend materials drawn over opaque geometry.
			std::optional<bool> depthPrepass;
			ResourcePtr material{ nullptr };
			std::vector<ResourcePtr> textures;
		};

	private:

		std::map<std::string, MeshRenderParams> mMeshParams;

		MeshRenderParams mModelParams;
		uint64_t mProgramSetRevision{ 1 };
		// Changes that can alter depth-only shadow output. Kept separate from the
		// visible-program cache because instance and draw-range changes do not
		// change a material program.
		uint64_t mShadowRevision{ 1 };

	public:

		ModelRenderParams()
		{
		}

		virtual ~ModelRenderParams() = default;

		uint32_t getModelFlags() const
		{
			auto it = mMeshParams.find("");

			if (it != mMeshParams.end())
			{
				return it->second.flags;
			}
			else
			{
				return Flag_Visible | Flag_CastShadows;
			}
		}

		void setModelFlags(uint32_t flags)
		{
			auto [it, inserted] = mMeshParams.insert(std::make_pair("", MeshRenderParams()));
			if (inserted || it->second.flags != flags) { ++mProgramSetRevision; ++mShadowRevision; }
			it->second.flags = flags;
		}

		void setModelInstanceCount(size_t count)
		{
			auto it = mMeshParams.insert(std::make_pair("", MeshRenderParams())).first;
			if (it->second.instanceCount != count) ++mShadowRevision;
			it->second.instanceCount = count;
		}

		void setModelPrimitiveCount(size_t count)
		{
			auto it = mMeshParams.insert(std::make_pair("", MeshRenderParams())).first;
			++mShadowRevision;
			it->second.renderCommands.clear();
			it->second.renderCommands.push_back({ 0, (uint32_t)count });
		}

		void setModelPointSize(float size)
		{
			auto it = mMeshParams.insert(std::make_pair("", MeshRenderParams())).first;
			
			it->second.pointSize = size;
		}

		void setModelUniforms(std::shared_ptr<UniformCollection> uniforms)
		{
			auto it = mMeshParams.insert(std::make_pair("", MeshRenderParams())).first;
			
			it->second.uniforms = uniforms;
		}

		void setModelBlend(bool blend)
		{
			auto it = mMeshParams.insert(std::make_pair("", MeshRenderParams())).first;

			it->second.blend = blend;
		}

		void setModelDepthPrepass(std::optional<bool> enabled)
		{
			auto it = mMeshParams.insert(std::make_pair("", MeshRenderParams())).first;
			it->second.depthPrepass = enabled;
		}

		void setModelMaterial(ResourcePtr material)
		{
			auto [it, inserted] = mMeshParams.insert(std::make_pair("", MeshRenderParams()));
			if (inserted || it->second.material != material) { ++mProgramSetRevision; ++mShadowRevision; }
			it->second.material = material;
		}

		void setModelTexture(uint32_t index, ResourcePtr texture)
		{
			auto it = mMeshParams.insert(std::make_pair("", MeshRenderParams())).first;
			auto& mrp = it->second;
			
			if (index >= mrp.textures.size())
			{
				mrp.textures.resize(index + 1);
			}

			mrp.textures[index] = texture;
		}

		void addModelRenderCommand(VertexBufferRenderCommand const& cmd)
		{
			auto it = mMeshParams.insert(std::make_pair("", MeshRenderParams())).first;
			++mShadowRevision;
			it->second.renderCommands.push_back(cmd);
		}

		void setMeshFlags(std::string const& mesh, uint32_t flags)
		{
			auto [it, inserted] = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams()));
			if (inserted || it->second.flags != flags) { ++mProgramSetRevision; ++mShadowRevision; }
			it->second.flags = flags;
		}

		void setMeshInstanceCount(std::string const& mesh, uint32_t count)
		{
			auto it = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams())).first;
			if (it->second.instanceCount != count) ++mShadowRevision;
			it->second.instanceCount = count;
		}

		void setMeshPrimitiveCount(std::string const& mesh, size_t count)
		{
			auto it = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams())).first;
			++mShadowRevision;
			it->second.renderCommands = { { 0, (uint32_t)count} };
		}

		void setMeshPointSize(std::string const& mesh, float size)
		{
			auto it = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams())).first;

			it->second.pointSize = size;
		}

		void setMeshUniforms(std::string const& mesh, std::shared_ptr<UniformCollection> uniforms)
		{
			auto it = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams())).first;

			it->second.uniforms = uniforms;
		}

		void setMeshBlend(std::string const& mesh, bool blend)
		{
			auto it = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams())).first;

			it->second.blend = blend;
		}

		void setMeshDepthPrepass(std::string const& mesh, std::optional<bool> enabled)
		{
			auto it = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams())).first;
			it->second.depthPrepass = enabled;
		}

		void setMeshMaterial(std::string const& mesh, ResourcePtr material)
		{
			auto [it, inserted] = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams()));
			if (inserted || it->second.material != material) { ++mProgramSetRevision; ++mShadowRevision; }
			it->second.material = material;
		}

		void setMeshTexture(std::string const& mesh, uint32_t index, ResourcePtr texture)
		{
			auto it = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams())).first;
			auto& mrp = it->second;

			if (index >= mrp.textures.size())
			{
				mrp.textures.resize(index + 1);
			}

			mrp.textures[index] = texture;
		}

		void addMeshRenderCommand(std::string const& mesh, VertexBufferRenderCommand const& cmd)
		{
			auto it = mMeshParams.insert(std::make_pair(mesh, MeshRenderParams())).first;
			++mShadowRevision;
			it->second.renderCommands.push_back(cmd);
		}

		uint64_t getProgramSetRevision() const
		{
			return mProgramSetRevision;
		}

		uint64_t getShadowRevision() const
		{
			return mShadowRevision;
		}

		std::map<std::string, MeshRenderParams> const& getMeshParams() const
		{
			return mMeshParams;
		}
	};
}

