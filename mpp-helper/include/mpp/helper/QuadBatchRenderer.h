#pragma once

#include <mpp/BatchRenderer.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/QuadBatch.h>

#include <mpp/mesh/VertexTypeSpecification.h>

#include "Config.h"
#include "QuadBatchDataProvider.h"

namespace mpp
{
	namespace helper
	{

		class QuadBatchRendererParams
		{
			mpp::QuadBatchOptions::PrimitiveOptions mPrimitiveOptions;

			mpp::QuadBatchOptions::RotationOptions mRotationOptions;

			bool mFixedTextureData, mFixedColourData;

			bool mUseVertexColours, mUseDiffuse;

			bool mSameSize;

			size_t mWidth, mHeight;

			size_t mIndexWidth;

			mpp::ResourcePtr mTexture;

			mpp::TextureRendererPtr mTextureRenderer;

		public:

			QuadBatchRendererParams(
				mpp::QuadBatchOptions::PrimitiveOptions primitiveOptions,
				mpp::QuadBatchOptions::RotationOptions rotationOptions,
				bool fixedTextureData,
				bool fixedColourData,
				bool useVertexColours,
				bool useDiffuse,
				size_t width,
				size_t height,
				bool sameSize,
				size_t indexWidth)
				: mPrimitiveOptions(primitiveOptions)
				, mFixedTextureData(fixedTextureData)
				, mFixedColourData(fixedColourData)
				, mUseVertexColours(useVertexColours)
				, mUseDiffuse(useDiffuse)
				, mRotationOptions(rotationOptions)
				, mSameSize(sameSize)
				, mWidth(width)
				, mHeight(height)
				, mIndexWidth(indexWidth)
				, mTexture(nullptr)
				, mTextureRenderer(nullptr)
			{
			}

			QuadBatchRendererParams(
				mpp::QuadBatchOptions::PrimitiveOptions primitiveOptions,
				mpp::QuadBatchOptions::RotationOptions rotationOptions, 
				bool fixedTextureData,
				bool fixedColourData,
				bool useVertexColours,
				bool useDiffuse,
				size_t width,
				size_t height,
				bool sameSize,
				size_t indexWidth,
				mpp::ResourcePtr texture)
				: mPrimitiveOptions(primitiveOptions)
				, mFixedTextureData(fixedTextureData)
				, mFixedColourData(fixedColourData)
				, mUseVertexColours(useVertexColours)
				, mUseDiffuse(useDiffuse)
				, mRotationOptions(rotationOptions)
				, mSameSize(sameSize)
				, mWidth(width)
				, mHeight(height)
				, mIndexWidth(indexWidth)
				, mTexture(texture)
				, mTextureRenderer(nullptr)
			{
			}

			QuadBatchRendererParams(
				mpp::QuadBatchOptions::PrimitiveOptions primitiveOptions,
				mpp::QuadBatchOptions::RotationOptions rotationOptions,
				bool fixedTextureData,
				bool fixedColourData,
				bool useVertexColours,
				bool useDiffuse,
				size_t width,
				size_t height,
				bool sameSize,
				size_t indexWidth,
				mpp::TextureRendererPtr textureRenderer)
				: mPrimitiveOptions(primitiveOptions)
				, mFixedTextureData(fixedTextureData)
				, mFixedColourData(fixedColourData)
				, mUseVertexColours(useVertexColours)
				, mUseDiffuse(useDiffuse)
				, mRotationOptions(rotationOptions)
				, mSameSize(sameSize)
				, mWidth(width)
				, mHeight(height)
				, mIndexWidth(indexWidth)
				, mTexture(nullptr)
				, mTextureRenderer(textureRenderer)
			{
			}

			mpp::QuadBatchOptions::PrimitiveOptions getPrimitiveOptions() const
			{
				return mPrimitiveOptions;
			}

			mpp::QuadBatchOptions::RotationOptions getRotationOptions() const
			{
				return mRotationOptions;
			}

			bool fixedTextureData() const
			{
				return mFixedTextureData;
			}

			bool fixedColourData() const
			{
				return mFixedColourData;
			}

			bool useVertexColours() const
			{
				return mUseVertexColours;
			}

			bool useDiffuse() const
			{
				return mUseDiffuse;
			}

			size_t getWidth() const
			{
				return mWidth;
			}

			size_t getHeight() const
			{
				return mHeight;
			}

			bool sameSize() const
			{
				return mSameSize;
			}

			size_t getIndexWidth() const
			{
				return mIndexWidth;
			}

			mpp::ResourcePtr getTexture() const
			{
				return mTexture;
			}

			mpp::TextureRendererPtr getTextureRenderer() const
			{
				return mTextureRenderer;
			}
		};

		template<typename PosType, typename TexType, typename ColType = mpp::mesh::DataTypeNone>
		class QuadBatchRenderer : public BatchRenderer
		{
			mpp::RenderSystem* mRenderSystem{ nullptr };

			mpp::ResourceManager* mResourceMgr{ nullptr };

			mpp::QuadBatch* mBatch{ nullptr };

			std::shared_ptr<QuadBatchDataProvider<PosType, TexType, ColType>> mDataProvider{ nullptr };

		public:

			QuadBatchRenderer(std::string const& name,
				QuadBatchRendererParams const& params,
				std::shared_ptr<QuadBatchDataProvider<PosType, TexType, ColType>> dataProvider,
				mpp::RenderSystem* renderSystem,
				mpp::ResourceManager* resourceMgr)
				: BatchRenderer()
				, mRenderSystem(renderSystem)
				, mResourceMgr(resourceMgr)
				, mDataProvider(dataProvider)
			{
				if (params.getTexture())
				{
					mBatch = new mpp::QuadBatch(
						name,
						{
							params.getPrimitiveOptions(),
							params.getRotationOptions(),
							PosType::vertexDataType(),
							{ TexType::vertexDataType(), params.fixedTextureData() },
							{ params.useVertexColours() ? ColType::vertexDataType() : mpp::mesh::Vertex::DataType::None, params.fixedColourData() },
							params.useDiffuse(),
							params.getWidth(),
							params.getHeight(),
							params.getIndexWidth()
						},
						params.sameSize(),
						params.getTexture(),
						mDataProvider->getNumPrimitives(),
						renderSystem,
						resourceMgr);
				}
				else if (params.getTextureRenderer())
				{
					mBatch = new mpp::QuadBatch(
						name,
						{
							params.getPrimitiveOptions(),
							params.getRotationOptions(),
							PosType::vertexDataType(),
							{ TexType::vertexDataType(), params.fixedTextureData() },
							{ params.useVertexColours() ? ColType::vertexDataType() : mpp::mesh::Vertex::DataType::None, params.fixedColourData() },
							params.useDiffuse(),
							params.getWidth(),
							params.getHeight(),
							params.getIndexWidth()
						},
						params.sameSize(),
						params.getTextureRenderer(),
						mDataProvider->getNumPrimitives(),
						renderSystem,
						resourceMgr);
				}
				else
				{
					mBatch = new mpp::QuadBatch(
						name,
						{
							params.getPrimitiveOptions(),
							params.getRotationOptions(),
							PosType::vertexDataType(),
							{ TexType::vertexDataType(), params.fixedTextureData() },
							{ params.useVertexColours() ? ColType::vertexDataType() : mpp::mesh::Vertex::DataType::None, params.fixedColourData() },
							params.useDiffuse(),
							params.getWidth(),
							params.getHeight(),
							params.getIndexWidth()
						},
						params.sameSize(),
						mDataProvider->getNumPrimitives(),
						renderSystem,
						resourceMgr);
				}

				mUniforms->setUniform("DIFFUSE", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
				mParams->setModelPointSize((float)params.getWidth());
			}

			virtual ~QuadBatchRenderer()
			{
				delete mBatch;
			}

			mpp::QuadBatch* getBatch()
			{
				return mBatch;
			}

			void create() override
			{
				mBatch->create();
				update();
			}

			size_t update() override
			{
				size_t count = mDataProvider->getNumPrimitives();

				size_t initStart{ ~0u }, batchSize = mBatch->getCount(0);
				bool newVertices{ false };
				if (count > batchSize)
				{
					initStart = mBatch->getVertexCount(mBatch->getPrimitiveCount(batchSize));
					newVertices = true;
				}

				size_t vertexCount = mBatch->getVertexCount(mBatch->getPrimitiveCount(count));

				mBatch->startUpdate(count, vertexCount);

				typedef typename PosType::builtin_type PosTypeBuiltin;
				typedef typename TexType::builtin_type TexTypeBuiltin;
				typedef typename ColType::builtin_type ColTypeBuiltin;

				auto posBuffer = (PosTypeBuiltin*)mBatch->getAttributeData(0, "POSITION").first;
				auto posStride = mBatch->getAttributeData(0, "POSITION").second / sizeof(PosTypeBuiltin);

				PosTypeBuiltin* rotBuffer{ nullptr };
				size_t rotStride{ 0 };

				if (mBatch->rotating())
				{
					rotBuffer = (PosTypeBuiltin*)mBatch->getAttributeData(0, "ROTATION").first;
					rotStride = mBatch->getAttributeData(0, "ROTATION").second / sizeof(PosTypeBuiltin);
				}

				TexTypeBuiltin* texBuffer{ nullptr };
				size_t texStride{ 0 };

				if (mBatch->usingTexture())
				{
					texBuffer = (TexTypeBuiltin*)mBatch->getAttributeData(0, "TEXCOORDS").first;
					texStride = mBatch->getAttributeData(0, "TEXCOORDS").second / sizeof(TexTypeBuiltin);
				}

				auto colBuffer = (ColTypeBuiltin*)mBatch->getAttributeData(0, "COLOUR").first;
				auto colStride = mBatch->getAttributeData(0, "COLOUR").second / sizeof(ColTypeBuiltin);

				for (size_t pOffset = 0, rOffset = 0, tOffset = 0, cOffset = 0, i = 0; i < vertexCount; ++i)
				{
					uint32_t primitiveIndex = (uint32_t)(mBatch->usingPointSprites() ? i : i / 4);
					bool newVertex = i >= initStart;

					//
					// Position data
					//
					if (!mBatch->positionFixed() || newVertex)
					{
						PosTypeBuiltin x, y;
						mDataProvider->position(primitiveIndex, x, y);

						if (mBatch->usingPointSprites())
						{
							// One vertex per quad
							posBuffer[pOffset + 0] = x;
							posBuffer[pOffset + 1] = y;
						}
						else
						{
							float halfWidth, halfHeight;
							mDataProvider->dimensions(primitiveIndex, halfWidth, halfHeight);

							// Indexed, four vertices per quad
							int vertexIndex = i % 4;

							switch (vertexIndex)
							{
							case 0:
								posBuffer[pOffset + 0] = x - halfWidth;
								posBuffer[pOffset + 1] = y - halfHeight;
								break;
							case 1:
								posBuffer[pOffset + 0] = x + halfWidth;
								posBuffer[pOffset + 1] = y - halfHeight;
								break;
							case 2:
								posBuffer[pOffset + 0] = x + halfWidth;
								posBuffer[pOffset + 1] = y + halfHeight;
								break;
							case 3:
								posBuffer[pOffset + 0] = x - halfWidth;
								posBuffer[pOffset + 1] = y + halfHeight;
								break;
							}
						}

						if (mBatch->rotating() && mBatch->usingTriangles())
						{
							// Centroid for rotation in vertex shader
							posBuffer[pOffset + 2] = x;
							posBuffer[pOffset + 3] = y;
						}
					}

					//
					// Rotation data
					//
					if (mBatch->rotating() && (!mBatch->rotationFixed() || newVertex))
					{
						switch (mBatch->getRotationType())
						{
						case QuadBatchOptions::RotationOptions::Angle:
						{
							PosTypeBuiltin angle;
							mDataProvider->angle(primitiveIndex, angle);

							auto rads = glm::radians(angle);
							rotBuffer[rOffset + 0] = sinf(rads);
							rotBuffer[rOffset + 1] = cosf(rads);
							break;
						}
						case QuadBatchOptions::RotationOptions::Direction:
						{
							float x, y;
							mDataProvider->direction(primitiveIndex, x, y);

							rotBuffer[rOffset + 0] = x;
							rotBuffer[rOffset + 1] = y;
							break;
						}
						}
					}

					//
					// Texture data
					//
					if (!mBatch->texcoordsFixed() || newVertex)
					{
						if (mBatch->usingPointSprites())
						{
							if (mBatch->usingTextureAtlas())
							{
								TexTypeBuiltin u0, v0, u1, v1;
								mDataProvider->textureAtlasTexcoords(primitiveIndex, u0, v0, u1, v1);

								texBuffer[tOffset + 0] = u0;
								texBuffer[tOffset + 1] = v0;
								texBuffer[tOffset + 2] = u1;
								texBuffer[tOffset + 3] = v1;
							}
						}
						else
						{
							if (mBatch->usingTexture())
							{
								int vertexIndex = i % 4;

								TexTypeBuiltin u0, v0, u1, v1;
								mDataProvider->textureAtlasTexcoords(primitiveIndex, u0, v0, u1, v1);

								// Indexed, four vertices per quad
								if (mBatch->usingTextureAtlas())
								{
									switch (vertexIndex)
									{
									case 0:
										texBuffer[tOffset + 0] = u0;
										texBuffer[tOffset + 1] = v0;
										break;
									case 1:
										texBuffer[tOffset + 0] = u1;
										texBuffer[tOffset + 1] = v0;
										break;
									case 2:
										texBuffer[tOffset + 0] = u1;
										texBuffer[tOffset + 1] = v1;
										break;
									case 3:
										texBuffer[tOffset + 0] = u0;
										texBuffer[tOffset + 1] = v1;
										break;
									}
								}
								else
								{
									switch (vertexIndex)
									{
									case 0:
										texBuffer[tOffset + 0] = 0.0f;
										texBuffer[tOffset + 1] = 0.0f;
										break;
									case 1:
										texBuffer[tOffset + 0] = 1.0f;
										texBuffer[tOffset + 1] = 0.0f;
										break;
									case 2:
										texBuffer[tOffset + 0] = 1.0f;
										texBuffer[tOffset + 1] = 1.0f;
										break;
									case 3:
										texBuffer[tOffset + 0] = 0.0f;
										texBuffer[tOffset + 1] = 1.0f;
										break;
									}
								}
							}
						}
					}

					//
					// Colour data
					//
					if (!mBatch->colourFixed() || newVertex)
					{
						ColTypeBuiltin red, green, blue, alpha;
						mDataProvider->colour(primitiveIndex, red, green, blue, alpha);

						colBuffer[cOffset + 0] = red;
						colBuffer[cOffset + 1] = green;
						colBuffer[cOffset + 2] = blue;
						colBuffer[cOffset + 3] = alpha;
					}

					// Next vertex
					pOffset += posStride;
					rOffset += rotStride;
					tOffset += texStride;
					cOffset += colStride;
				}

				mBatch->finishUpdate(count, vertexCount, newVertices);
				return mBatch->getCount(0);
			}

			void render() override
			{
				if (mBatch->usingDiffuse())
				{
					auto colour = mDataProvider->diffuse();
					mUniforms->updateUniform("DIFFUSE", glm::vec4(colour.red, colour.green, colour.blue, colour.alpha));
				}

				auto const& model = static_cast<Model const&>(*mBatch->getModel().get());
				mRenderSystem->renderModelImmediate(model, true, mParams);

			}
		};

		template<typename PosType, typename TexType>
		class QuadBatchRenderer<PosType, TexType, mpp::mesh::DataTypeNone>
			: public BatchRenderer
		{
			mpp::RenderSystem* mRenderSystem{ nullptr };

			mpp::ResourceManager* mResourceMgr{ nullptr };

			mpp::QuadBatch* mBatch{ nullptr };

			std::shared_ptr<QuadBatchDataProvider<PosType, TexType, mpp::mesh::DataTypeNone>> mDataProvider;

		public:

			QuadBatchRenderer(std::string const& name,
				QuadBatchRendererParams const& params,
				std::shared_ptr<QuadBatchDataProvider<PosType, TexType, mpp::mesh::DataTypeNone>> dataProvider,
				mpp::RenderSystem* renderSystem,
				mpp::ResourceManager* resourceMgr)
				: BatchRenderer()
				, mRenderSystem(renderSystem)
				, mResourceMgr(resourceMgr)
				, mDataProvider(dataProvider)
			{
				if (params.getTexture())
				{
					mBatch = new mpp::QuadBatch(
						name,
						{
							params.getPrimitiveOptions(),
							params.getRotationOptions(),
							PosType::vertexDataType(),
							{ TexType::vertexDataType(), params.fixedTextureData() },
							{ mpp::mesh::Vertex::DataType::None, true },
							params.useDiffuse(),
							params.getWidth(),
							params.getHeight(),
							params.getIndexWidth()
						},
						params.sameSize(),
						params.getTexture(),
						mDataProvider->getNumPrimitives(),
						renderSystem,
						resourceMgr);
				}
				else if (params.getTextureRenderer())
				{
					mBatch = new mpp::QuadBatch(
						name,
						{
							params.getPrimitiveOptions(),
							params.getRotationOptions(),
							PosType::vertexDataType(),
							{ TexType::vertexDataType(), params.fixedTextureData() },
							{ mpp::mesh::Vertex::DataType::None, true },
							params.useDiffuse(),
							params.getWidth(),
							params.getHeight(),
							params.getIndexWidth()
						},
						params.sameSize(),
						params.getTextureRenderer(),
						mDataProvider->getNumPrimitives(),
						renderSystem,
						resourceMgr);
				}
				else
				{
					mBatch = new mpp::QuadBatch(
						name,
						{
							params.getPrimitiveOptions(),
							params.getRotationOptions(),
							PosType::vertexDataType(),
							{ TexType::vertexDataType(), params.fixedTextureData() },
							{ mpp::mesh::Vertex::DataType::None, true },
							params.useDiffuse(),
							params.getWidth(),
							params.getHeight(),
							params.getIndexWidth()
						},
						params.sameSize(),
						mDataProvider->getNumPrimitives(),
						renderSystem,
						resourceMgr);
				}

				mUniforms->setUniform("DIFFUSE", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
				mParams->setModelPointSize((float)params.getWidth());
			}

			virtual ~QuadBatchRenderer()
			{
				delete mBatch;
			}

			mpp::QuadBatch* getBatch()
			{
				return mBatch;
			}

			void create()
			{
				mBatch->create();
				update();
			}

			size_t update() override
			{
				size_t count = mDataProvider->getNumPrimitives();
				size_t initStart{ ~0u }, batchSize = mBatch->getCount(0);
				bool newVertices{ false };
				if (count > batchSize)
				{
					initStart = mBatch->getVertexCount(mBatch->getPrimitiveCount(batchSize));
					newVertices = true;
				}

				size_t vertexCount = mBatch->getVertexCount(mBatch->getPrimitiveCount(count));

				mBatch->startUpdate(count, vertexCount);

				typedef typename PosType::builtin_type PosTypeBuiltin;
				typedef typename TexType::builtin_type TexTypeBuiltin;

				auto posBuffer = (PosTypeBuiltin*)mBatch->getAttributeData(0, "POSITION").first;
				auto posStride = mBatch->getAttributeData(0, "POSITION").second / sizeof(PosTypeBuiltin);

				PosTypeBuiltin* rotBuffer{ nullptr };
				size_t rotStride{ 0 };

				if (mBatch->rotating())
				{
					rotBuffer = (PosTypeBuiltin*)mBatch->getAttributeData(0, "ROTATION").first;
					rotStride = mBatch->getAttributeData(0, "ROTATION").second / sizeof(PosTypeBuiltin);
				}

				TexTypeBuiltin* texBuffer{ nullptr };
				size_t texStride{ 0 };

				if (mBatch->usingTexture())
				{
					texBuffer = (TexTypeBuiltin*)mBatch->getAttributeData(0, "TEXCOORDS").first;
					texStride = mBatch->getAttributeData(0, "TEXCOORDS").second / sizeof(TexTypeBuiltin);
				}

				for (size_t pOffset = 0, rOffset = 0, tOffset = 0, i = 0; i < vertexCount; ++i)
				{
					uint32_t primitiveIndex = (uint32_t)(mBatch->usingPointSprites() ? i : i / 4);
					bool newVertex = i >= initStart;

					//
					// Position data
					//
					if (!mBatch->positionFixed() || newVertex)
					{
						PosTypeBuiltin x, y;
						mDataProvider->position(primitiveIndex, x, y);

						if (mBatch->usingPointSprites())
						{
							// One vertex per quad
							posBuffer[pOffset + 0] = x;
							posBuffer[pOffset + 1] = y;
						}
						else
						{
							float halfWidth, halfHeight;
							mDataProvider->dimensions(primitiveIndex, halfWidth, halfHeight);

							// Indexed, four vertices per quad
							int vertexIndex = i % 4;

							switch (vertexIndex)
							{
							case 0:
								posBuffer[pOffset + 0] = x - halfWidth;
								posBuffer[pOffset + 1] = y - halfHeight;
								break;
							case 1:
								posBuffer[pOffset + 0] = x + halfWidth;
								posBuffer[pOffset + 1] = y - halfHeight;
								break;
							case 2:
								posBuffer[pOffset + 0] = x + halfWidth;
								posBuffer[pOffset + 1] = y + halfHeight;
								break;
							case 3:
								posBuffer[pOffset + 0] = x - halfWidth;
								posBuffer[pOffset + 1] = y + halfHeight;
								break;
							}
						}

						if (mBatch->rotating() && mBatch->usingTriangles())
						{
							// Centroid for rotation in vertex shader
							posBuffer[pOffset + 2] = x;
							posBuffer[pOffset + 3] = y;
						}
					}

					//
					// Rotation data
					//
					if (rotBuffer && mBatch->rotating() && (!mBatch->rotationFixed() || newVertex))
					{
						switch (mBatch->getRotationType())
						{
						case QuadBatchOptions::RotationOptions::Angle:
						{
							PosTypeBuiltin angle;
							mDataProvider->angle(primitiveIndex, angle);

							auto rads = glm::radians(angle);
							rotBuffer[rOffset + 0] = sinf(rads);
							rotBuffer[rOffset + 1] = cosf(rads);
							break;
						}
						case QuadBatchOptions::RotationOptions::Direction:
						{
							float x, y;
							mDataProvider->direction(primitiveIndex, x, y);

							rotBuffer[rOffset + 0] = x;
							rotBuffer[rOffset + 1] = y;
							break;
						}
						}
					}

					//
					// Texture data
					//
					if (texBuffer && !mBatch->texcoordsFixed() || newVertex)
					{
						if (mBatch->usingPointSprites())
						{
							if (mBatch->usingTextureAtlas())
							{
								TexTypeBuiltin u0, v0, u1, v1;
								mDataProvider->textureAtlasTexcoords(primitiveIndex, u0, v0, u1, v1);

								texBuffer[tOffset + 0] = u0;
								texBuffer[tOffset + 1] = v0;
								texBuffer[tOffset + 2] = u1;
								texBuffer[tOffset + 3] = v1;
							}
						}
						else
						{
							if (texBuffer && mBatch->usingTexture())
							{
								int vertexIndex = i % 4;

								TexTypeBuiltin u0, v0, u1, v1;
								mDataProvider->textureAtlasTexcoords(primitiveIndex, u0, v0, u1, v1);

								// Indexed, four vertices per quad
								if (mBatch->usingTextureAtlas())
								{
									switch (vertexIndex)
									{
									case 0:
										texBuffer[tOffset + 0] = u0;
										texBuffer[tOffset + 1] = v0;
										break;
									case 1:
										texBuffer[tOffset + 0] = u1;
										texBuffer[tOffset + 1] = v0;
										break;
									case 2:
										texBuffer[tOffset + 0] = u1;
										texBuffer[tOffset + 1] = v1;
										break;
									case 3:
										texBuffer[tOffset + 0] = u0;
										texBuffer[tOffset + 1] = v1;
										break;
									}
								}
								else
								{
									switch (vertexIndex)
									{
									case 0:
										texBuffer[tOffset + 0] = 0.0f;
										texBuffer[tOffset + 1] = 0.0f;
										break;
									case 1:
										texBuffer[tOffset + 0] = 1.0f;
										texBuffer[tOffset + 1] = 0.0f;
										break;
									case 2:
										texBuffer[tOffset + 0] = 1.0f;
										texBuffer[tOffset + 1] = 1.0f;
										break;
									case 3:
										texBuffer[tOffset + 0] = 0.0f;
										texBuffer[tOffset + 1] = 1.0f;
										break;
									}
								}
							}
						}
					}

					// Next vertex
					pOffset += posStride;
					rOffset += rotStride;
					tOffset += texStride;
				}

				mBatch->finishUpdate(count, vertexCount, newVertices);
				return mBatch->getCount(0);
			}

			void render()
			{
				if (mBatch->usingDiffuse())
				{
					auto colour = mDataProvider->diffuse();
					mUniforms->updateUniform("DIFFUSE", glm::vec4(colour.red, colour.green, colour.blue, colour.alpha));
				}

				auto const& model = static_cast<Model const&>(*mBatch->getModel().get());
				mRenderSystem->renderModelImmediate(model, true, mParams);
			}
		};

	}
}