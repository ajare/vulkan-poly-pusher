#pragma once

#include <vector>

#include "mpp/Batch.h"
#include "mpp/TextureRenderer.h"

namespace mpp
{
	struct QuadBatchOptions
	{
		enum class PrimitiveOptions
		{
			Auto,
			Points,
			Triangles
		};

		enum class RotationOptions
		{
			None,
			Angle,
			Direction
		};
		
		PrimitiveOptions primitiveOptions{ PrimitiveOptions::Auto };
		RotationOptions rotation{ RotationOptions::None };
		mpp::mesh::Vertex::DataType positionType{ mpp::mesh::Vertex::DataType::Float };
		BatchVertexAttribute texcoordAttrib{ mpp::mesh::Vertex::DataType::Float, false };
		BatchVertexAttribute colourAttrib{ mpp::mesh::Vertex::DataType::Float, false };
		bool useDiffuse{ false };
		size_t maxSizeX{ 1 }, maxSizeY{ 1 };
		size_t indexWidth{ 32 };
	};

	class _MPPAPI QuadBatch : public Batch
	{
		QuadBatchOptions mOptions;

		bool mSameSize;

		ResourcePtr mTexture;

		TextureRendererPtr mTextureRenderer;

		float mPointSize;

	private:

		void setPrimitiveOptions();

		bool indexedVertices() const;

		void createIndexData(std::vector<uint8_t>& data, uint32_t start, size_t count);

	protected:

		mesh::Primitive::Type getPrimitiveType() const;

		void createImpl();

		mesh::MeshSpecification createMeshSpecification(mesh::Primitive::Type primitiveType) override;

		void addIndexedPrimitives(std::shared_ptr<ProgrammaticModelStream> ms, int meshIndex) override;

		uint32_t getProgramFlags() const override;

		ResourcePtr getTexture() override;

		int getIndexWidth() const override;

		float getPointSize() const override;

	public:

		QuadBatch(std::string const& name,
			QuadBatchOptions const& options,
			bool sameSize,
			ResourcePtr texture,
			size_t initialCapacity,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		QuadBatch(std::string const& name,
			QuadBatchOptions const& options,
			bool sameSize,
			ResourcePtr texture,
			size_t initialCapacity,
			std::string const& defaultVertexShader,
			std::string const& defaultFragmentShader,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		QuadBatch(std::string const& name,
			QuadBatchOptions const& options,
			bool sameSize,
			TextureRendererPtr textureRenderer,
			size_t initialCapacity,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		QuadBatch(std::string const& name,
			QuadBatchOptions const& options,
			bool sameSize,
			TextureRendererPtr textureRenderer,
			size_t initialCapacity,
			std::string const& defaultVertexShader,
			std::string const& defaultFragmentShader,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		QuadBatch(std::string const& name,
			QuadBatchOptions const& options,
			bool sameSize,
			size_t initialCapacity,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		QuadBatch(std::string const& name,
			QuadBatchOptions const& options,
			bool sameSize,
			size_t initialCapacity,
			std::string const& defaultVertexShader,
			std::string const& defaultFragmentShader,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		~QuadBatch();

		size_t getPrimitiveCount(size_t objectCount) const;

		size_t getVertexCount(size_t primitiveCount) const;

		int getMaxDimX() const;

		int getMaxDimY() const;

		QuadBatchOptions::RotationOptions getRotationType() const;

		bool usingPointSprites() const;

		bool usingTriangles() const;

		bool rotating() const;

		bool usingTexture() const;

		bool usingTextureAtlas() const;

		bool positionFixed() const;

		bool rotationFixed() const;

		bool texcoordsFixed() const;

		bool colourFixed() const;
	};
}
