#pragma once

#include <vector>

#include "mpp/Resource.h"
#include "mpp/Mesh.h"
#include "mpp/Program.h"

#include "mpp/mesh/MeshDefinition.h"

namespace mpp
{
	class _MPPAPI Model : public Resource
	{
		glm::vec3 mBounds[2];
		// A stream can decline the load-time position scan; a
		// ProgrammaticModelStream whose vertices are rewritten every frame is the
		// usual case. mBounds is then a placeholder rather than a measurement, so
		// record which of the two it holds.
		bool mBoundsCalculated{ false };
		uint64_t mMaterialRevision{ 1 };

	protected:

		std::vector<Mesh*> mMeshes;

	private:

		glm::vec3 readPositionFromStream(int8_t const* stream, mesh::VertexBufferAttributeLayout::Attribute const& attrib);

		void calculateBounds(mesh::VertexBufferAttributeLayout::Attribute const& posAttr, mesh::VertexBufferDefinition const* bufferDef);

	protected:

		void createImpl();

		void destroyImpl();

		void loadImpl();

		void unloadImpl();

		bool checkVertexAttributeMapping(ResourcePtr material, mesh::MeshDefinition* meshDef);

	public:

		Model(std::string const& name, RenderSystem* renderSystem, ResourceManager* resourceMgr, ResourceStreamPtr resourceStream);

		~Model();

		int getIdCount() const override;

		int getLiveIdCount() const override;

		int getNumTriangles() const;

		int getNumMeshes() const;

		uint64_t getMaterialRevision() const;

		Mesh const* getMesh(int index) const;

		Mesh* getMesh(int index);

		void getBounds(glm::vec3& bMin, glm::vec3& bMax) const;

		// Whether getBounds() describes this model's geometry. False when the
		// stream declined the calculation, and when it ran over no vertices and
		// left the inverted initial extent. A caller that culls by bounds must
		// read false as unbounded, never as the box getBounds() reports.
		bool hasBounds() const;

		void setMeshesDynamic();

		void setMeshesStatic();
	};

}