#include <cmath>

#include "mpp/QuadBatch.h"
#include "mpp/DefaultShaders.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/ResourceManager.h"

using namespace std;

namespace mpp
{
	using namespace mesh;

	/*
	 * Constructor.
	 *
	 */
	QuadBatch::QuadBatch(string const& name,
		QuadBatchOptions const& options, 
		bool sameSize,
		ResourcePtr texture,
		size_t initialCapacity,
		string const& defaultVertexShader,
		string const& defaultFragmentShader,
		RenderSystem* renderSystem,
		ResourceManager* resourceMgr)
		: Batch(name, initialCapacity, defaultVertexShader, defaultFragmentShader, "quad", options.colourAttrib, options.useDiffuse, renderSystem, resourceMgr)
		, mOptions(options)
		, mSameSize(sameSize)
		, mTexture(texture)
		, mTextureRenderer(nullptr)
		, mPointSize((float)options.maxSizeX)
	{
		setPrimitiveOptions();

		if (mTexture)
		{
			mTexture->acquire(this);
		}
	}

	/*
	 * Constructor.
	 *
	 */
	QuadBatch::QuadBatch(string const& name,
		QuadBatchOptions const& options,
		bool sameSize,
		ResourcePtr texture,
		size_t initialCapacity,
		RenderSystem* renderSystem,
		ResourceManager* resourceMgr)
		: QuadBatch(name, options, sameSize, texture, initialCapacity, VertexShader2dTemplate, FragmentShader2dTemplate, renderSystem, resourceMgr)
	{
	}

	/*
	 * Constructor.
	 *
	 */
	QuadBatch::QuadBatch(string const& name,
		QuadBatchOptions const& options,
		bool sameSize,
		TextureRendererPtr textureRenderer,
		size_t initialCapacity,
		string const& defaultVertexShader,
		string const& defaultFragmentShader,
		RenderSystem* renderSystem,
		ResourceManager* resourceMgr)
		: Batch(name, initialCapacity, defaultVertexShader, defaultFragmentShader, "quad", options.colourAttrib, options.useDiffuse, renderSystem, resourceMgr)
		, mOptions(options)
		, mSameSize(sameSize)
		, mTexture(nullptr)
		, mTextureRenderer(textureRenderer)
		, mPointSize((float)options.maxSizeX)
	{
		setPrimitiveOptions();
	}

	/*
	 * Constructor.
	 *
	 */
	QuadBatch::QuadBatch(string const& name,
		QuadBatchOptions const& options,
		bool sameSize,
		TextureRendererPtr textureRenderer,
		size_t initialCapacity,
		RenderSystem* renderSystem,
		ResourceManager* resourceMgr)
		: QuadBatch(name, options, sameSize, textureRenderer, initialCapacity, VertexShader2dTemplate, FragmentShader2dTemplate, renderSystem, resourceMgr)
	{
	}

	/*
	 * Constructor.
	 *
	 */
	QuadBatch::QuadBatch(string const& name,
		QuadBatchOptions const& options,
		bool sameSize,
		size_t initialCapacity,
		string const& defaultVertexShader,
		string const& defaultFragmentShader,
		RenderSystem* renderSystem,
		ResourceManager* resourceMgr)
		: Batch(name, initialCapacity, defaultVertexShader, defaultFragmentShader, "quad", options.colourAttrib, options.useDiffuse, renderSystem, resourceMgr)
		, mOptions(options)
		, mSameSize(sameSize)
		, mTexture(nullptr)
		, mTextureRenderer(nullptr)
		, mPointSize((float)options.maxSizeX)
	{
		setPrimitiveOptions();
	}

	/*
	 * Constructor.
	 *
	 */
	QuadBatch::QuadBatch(string const& name,
		QuadBatchOptions const& options,
		bool sameSize,
		size_t initialCapacity,
		RenderSystem* renderSystem,
		ResourceManager* resourceMgr)
		: QuadBatch(name, options, sameSize, initialCapacity, VertexShader2dTemplate, FragmentShader2dTemplate, renderSystem, resourceMgr)
	{
	}

	QuadBatch::~QuadBatch()
	{
		if (mTexture)
		{
			mTexture->release(this);
		}

		if (mTextureRenderer && mTexture && !mTexture->getRefCount())
		{
			mResourceMgr->deleteResource(mTexture->getName());
		}
	}

	void QuadBatch::setPrimitiveOptions()
	{
		// Set vertex options
		float size = (float)max(mOptions.maxSizeX, mOptions.maxSizeY);
		bool square = mSameSize && mOptions.maxSizeX == mOptions.maxSizeY;

		Caps const& caps = mRenderSystem->getCaps();
		if (mOptions.primitiveOptions == QuadBatchOptions::PrimitiveOptions::Points)
		{
			if (mOptions.rotation != QuadBatchOptions::RotationOptions::None && !usingTexture())
			{
				THROW_MPP("Cannot use point sprites when rotating with no texture.", __LINE__, __FILE__, __func__);
			}
			if (!square)
			{
				THROW_MPP("Cannot use point sprites for non-square quads.", __LINE__, __FILE__, __func__);
			}
			if (caps.pointSizeRange[0] > size || caps.pointSizeRange[1] < size)
			{
				THROW_MPP("Cannot use point sprites for theses sizes of quad.", __LINE__, __FILE__, __func__);
			}
		}

		if (mOptions.primitiveOptions != QuadBatchOptions::PrimitiveOptions::Triangles)
		{
			mOptions.primitiveOptions = QuadBatchOptions::PrimitiveOptions::Points;
		}

		if (usingTexture() && mOptions.texcoordAttrib.dataType == mesh::Vertex::DataType::None)
		{
			THROW_MPP("Must specify a texcoord type when using a texture.", __LINE__, __FILE__, __func__);
		}
	}

	mesh::Primitive::Type QuadBatch::getPrimitiveType() const
	{
		return usingPointSprites() ? mesh::Primitive::Type::Points : mesh::Primitive::Type::Triangles;
	}

	bool QuadBatch::indexedVertices() const
	{
		return !usingPointSprites();
	}

	/*
	 * Create indices for a primitive.
	 *
	 */
	void QuadBatch::createIndexData(vector<uint8_t>& data, uint32_t start, size_t count)
	{
		if (count == 0)
		{
			return;
		}

		size_t vertexSize{ 6 * (mOptions.indexWidth / 8) };
		data.resize(count * vertexSize);

		uint32_t* ptr = (uint32_t*)&data[start * vertexSize]; // Indices will be 16 or 32-bit, so use 32 to cover both
		int indexBytes = (int)mOptions.indexWidth / 8;

		for (uint32_t i = start; i < count; ++i)
		{
			auto x = i * 4;

			if (indexBytes == 2)
			{
				*ptr++ = (x + 0) + ((x + 1) << 16);
				*ptr++ = (x + 2) + ((x + 2) << 16);
				*ptr++ = (x + 3) + ((x + 0) << 16);
			}
			else if (indexBytes == 4)
			{
				*ptr++ = x + 0;
				*ptr++ = x + 1;
				*ptr++ = x + 2;
				*ptr++ = x + 2;
				*ptr++ = x + 3;
				*ptr++ = x + 0;
			}
		}
	}

	/*
	 * Get the number of vertices required, given the number of primitives.
	 *
	 */
	size_t QuadBatch::getVertexCount(size_t primitiveCount) const
	{
		size_t mult;

		if (usingPointSprites())
		{
			mult = 1;
		}
		else if (indexedVertices())
		{
			mult = 4;
		}
		else
		{
			mult = 6;
		}
		
		return primitiveCount * mult;
	}

	mesh::MeshSpecification QuadBatch::createMeshSpecification(mesh::Primitive::Type primitiveType)
	{
		/*
		Position holds position (xy) and centroid (zw) for when we're rotating and
		using triangles.
		
		User-defined Rotation holds rotation (xy) for when we're rotating

		Texcoords hold texcoords when we have a single texture (xy) and full xyzw
		when we're using point sprites and an atlas

		Colour is optional, but always xyzw
		*/

		auto meshSpec = mesh::MeshSpecification(primitiveType);
		meshSpec.setIndexedVertices(indexedVertices());

		auto dynamicLayout = meshSpec.createVertexBufferAttributeLayout(false);
		mesh::VertexBufferAttributeLayout* staticLayout{ nullptr };

		// For position, if we're rotating with triangles, then we need to store the
		// angle in .zw, as it needs the same type as position.
		if (rotating() && usingTriangles())
		{
			dynamicLayout->createAttribute(mesh::Vertex::Component::Position4, mOptions.positionType, false);
		}
		else
		{
			dynamicLayout->createAttribute(mesh::Vertex::Component::Position2, mOptions.positionType, false);
		}

		// If we're not rotating, store rotation as static data
		mesh::VertexBufferAttributeLayout* rotationLayout{ nullptr };
		if (!rotating())
		{
			rotationLayout = staticLayout;
		}
		else
		{
			rotationLayout = dynamicLayout;
		}

		if (rotationLayout)
		{
			rotationLayout->createAttribute(mesh::Vertex::Component::UserDefined2, "ROTATION", mesh::Vertex::DataType::Float, false);
		}

		// Texture coords
		mesh::VertexBufferAttributeLayout* texcoordLayout{ nullptr };
		if ((usingPointSprites() && usingTextureAtlas()) || (!usingPointSprites() && usingTexture()))
		{
			if (mOptions.texcoordAttrib.fixedValues)
			{
				if (!staticLayout)
				{
					staticLayout = meshSpec.createVertexBufferAttributeLayout(true);
				}

				texcoordLayout = staticLayout;
			}
			else
			{
				texcoordLayout = dynamicLayout;
			}
		}

		if (usingPointSprites())
		{
			// Only need texcoords if we're using a texture atlas (and implicitly, a texture), otherwise
			// gl_PointCoord is used
			if (usingTextureAtlas())
			{
				texcoordLayout->createAttribute(mesh::Vertex::Component::TexCoord4, mOptions.texcoordAttrib.dataType, false);
			}
		}
		else
		{
			if (usingTexture())
			{
				texcoordLayout->createAttribute(mesh::Vertex::Component::TexCoord2, mOptions.texcoordAttrib.dataType, false);
			}
		}
		
		// Colour
		if (mOptions.colourAttrib.dataType != mesh::Vertex::DataType::None)
		{
			mesh::VertexBufferAttributeLayout* colourLayout{ nullptr };
			if (mOptions.colourAttrib.fixedValues)
			{
				if (!staticLayout)
				{
					staticLayout = meshSpec.createVertexBufferAttributeLayout(true);
				}

				colourLayout = staticLayout;
			}
			else
			{
				colourLayout = dynamicLayout;
			}

			colourLayout->createAttribute(mesh::Vertex::Component::Colour4, mOptions.colourAttrib.dataType, mesh::Vertex::isDataTypeNormalisable(mOptions.colourAttrib.dataType));
		}

		return meshSpec;
	}

	void QuadBatch::addIndexedPrimitives(shared_ptr<ProgrammaticModelStream> ms, int meshIndex)
	{
		for (size_t i = 0; i < mInitialCapacity; ++i)
		{
			auto x = (uint32_t)i * 4;
			ms->addTriangle(meshIndex, x + 0, x + 1, x + 2);
			ms->addTriangle(meshIndex, x + 2, x + 3, x + 0);
		}
	}

	uint32_t QuadBatch::getProgramFlags() const
	{
		uint32_t flags = 0
			| (usingPointSprites() ? MPP_PROGRAM_TAGS_PRIM_POINTS : MPP_PROGRAM_TAGS_PRIM_TRIANGLES)
			| (usingTexture() ? MPP_PROGRAM_TAGS_TEXTURE : 0)
			| (usingTextureAtlas() ? MPP_PROGRAM_TAGS_ATLAS : 0)
			| (rotating() ? MPP_PROGRAM_TAGS_ROTATION : 0)
			| (usingDiffuse() ? MPP_PROGRAM_TAGS_DIFFUSE : 0);

		return flags;
	}

	ResourcePtr QuadBatch::getTexture()
	{
		if (!mTexture && mTextureRenderer)
		{
			mTexture = mTextureRenderer->createRenderTexture(mOptions.maxSizeX, mOptions.maxSizeY);
			mTexture->acquire(this);
		}

		return mTexture;
	}

	int QuadBatch::getIndexWidth() const
	{
		return (int)mOptions.indexWidth;
	}

	float QuadBatch::getPointSize() const
	{
		return mPointSize;
	}

	size_t QuadBatch::getPrimitiveCount(size_t objectCount) const
	{
		return objectCount * (usingPointSprites() ? 1 : 2);
	}

	int QuadBatch::getMaxDimX() const
	{
		return (int)mOptions.maxSizeX;
	}

	int QuadBatch::getMaxDimY() const
	{
		return (int)mOptions.maxSizeY;
	}

	QuadBatchOptions::RotationOptions QuadBatch::getRotationType() const
	{
		return mOptions.rotation;
	}

	bool QuadBatch::usingPointSprites() const
	{
		return mOptions.primitiveOptions == QuadBatchOptions::PrimitiveOptions::Points;
	}

	bool QuadBatch::usingTriangles() const
	{
		return mOptions.primitiveOptions == QuadBatchOptions::PrimitiveOptions::Triangles;
	}

	bool QuadBatch::rotating() const
	{
		return mOptions.rotation != QuadBatchOptions::RotationOptions::None;
	}

	bool QuadBatch::usingTexture() const
	{
		return mTexture || mTextureRenderer;
	}

	bool QuadBatch::usingTextureAtlas() const
	{
		return mTexture && static_cast<Texture*>(mTexture.get())->isAtlas();
	}

	bool QuadBatch::positionFixed() const
	{
		return false;
	}

	bool QuadBatch::rotationFixed() const
	{
		return mOptions.rotation == QuadBatchOptions::RotationOptions::None;
	}

	bool QuadBatch::texcoordsFixed() const
	{
		return mOptions.texcoordAttrib.fixedValues;
	}

	bool QuadBatch::colourFixed() const
	{
		return mOptions.colourAttrib.fixedValues;
	}

}