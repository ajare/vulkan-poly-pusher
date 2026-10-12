#pragma once

#include <vector>

#include "mpp/ResourceWrangler.h"
#include "mpp/Model.h"
#include "mpp/ProgrammaticModelStream.h"

#include "mpp/mesh/Vertex.h"
#include "mpp/mesh/VertexData.h"
#include "mpp/mesh/MeshSpecification.h"

namespace mpp
{

	struct BatchVertexAttribute
	{
		mesh::Vertex::DataType dataType;
		bool fixedValues;
	};

	struct BatchMesh
	{
		std::map<std::string, std::pair<char*, size_t>> dataPointers;
		size_t curCount{ 0 };
		size_t maxCount{ 0 };
	};

	class _MPPAPI Batch : public ResourceWrangler
	{
		std::string mName;
		
		bool mUseDiffuse;

		static std::pair<char*, size_t> msNonExistentAttribute;

		ResourcePtr mModel, mMaterial;

		std::string mDefaultVertexShader, mDefaultFragmentShader;

		std::string mProgramDescriptor;

		mesh::MeshSpecification mSpecification;

		BatchVertexAttribute mColourAttrib;

	protected:

		size_t mInitialCapacity;

		std::vector<BatchMesh> mMeshes;
		
		RenderSystem* mRenderSystem;

		ResourceManager* mResourceMgr;

	private:

		void destroy();

		virtual bool indexedVertices() const = 0;

		virtual void createIndexData(std::vector<uint8_t>& data, uint32_t start, size_t count);

	protected:

		BatchVertexAttribute getColourAttribute() const;

		virtual std::shared_ptr<ModelStream> createModelStream();

		virtual mesh::Primitive::Type getPrimitiveType() const = 0;

		virtual uint32_t getProgramFlags() const = 0;

		virtual int getIndexWidth() const = 0;

		virtual float getPointSize() const;

		virtual ResourcePtr getTexture();

		virtual mesh::MeshSpecification createMeshSpecification(mesh::Primitive::Type primitiveType) = 0;

		virtual void addIndexedPrimitives(std::shared_ptr<ProgrammaticModelStream> ms, int meshIndex);
		
		void createVertexBuffer(uint32_t index, Mesh* mesh, size_t vertexCount, bool staticData);

		void setSpecificationPointers(uint32_t meshIndex, Mesh* mesh);

		virtual void setMinimumCount(size_t count, size_t vertexCount);

		virtual ResourcePtr createMaterial(std::string const& name, ResourcePtr texture, uint32_t programFlags, bool is2d = true);

		ResourcePtr createMaterial(std::string const& name, ResourcePtr program, ResourcePtr texture, uint32_t programFlags);

	public:

		Batch(std::string const& name,
			size_t initialCapacity,
			std::string const& defaultVertexShader,
			std::string const& defaultFragmentShader,
			std::string const& descriptor,
			BatchVertexAttribute colourAttrib,
			bool useDiffuse,
			RenderSystem* renderSystem,
			ResourceManager* resourceMgr);

		virtual ~Batch();

		std::string const& getName() const;

		ResourcePtr getModel();

		ResourcePtr getMaterial();

		mesh::MeshSpecification const& getSpecification() const;

		const std::pair<char*, size_t>& getAttributeData(uint32_t meshIndex, std::string const& name) const;

		size_t getCount(uint32_t meshIndex) const;

		size_t getCapacity(uint32_t meshIndex) const;

		virtual size_t getPrimitiveCount(size_t objectCount) const;

		virtual size_t getVertexCount(size_t primitiveCount) const = 0;

		void create();

		void startUpdate(size_t minimumCount, size_t vertexCount);

		virtual void finishUpdate(size_t primitiveCount, size_t vertexCount, bool updateFixedBuffers);

		bool usingDiffuse() const;

		bool usingColour() const;
	};
}
