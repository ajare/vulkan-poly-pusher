#pragma once

#include <mpp/BatchRenderer.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/LineBatch.h>

#include <mpp/mesh/VertexTypeSpecification.h>

#include "Config.h"
#include "LineBatchDataProvider.h"

namespace mpp
{
	namespace helper
	{

		struct LineBatchRendererParams
		{
			bool fixedColourData;
			bool useVertexColours, useDiffuse;
		};

		template<typename PosType, typename ColType = mpp::mesh::DataTypeNone>
		class LineBatchRenderer : public BatchRenderer
		{
			mpp::RenderSystem* mRenderSystem{ nullptr };

			mpp::ResourceManager* mResourceMgr{ nullptr };

			mpp::LineBatch* mBatch{ nullptr };

			std::shared_ptr<LineBatchDataProvider<PosType, ColType>> mDataProvider{ nullptr };

		public:

			LineBatchRenderer(std::string const& name,
				LineBatchRendererParams const& params,
				std::shared_ptr<LineBatchDataProvider<PosType, ColType>> dataProvider,
				mpp::RenderSystem* renderSystem,
				mpp::ResourceManager* resourceMgr)
				: BatchRenderer()
				, mRenderSystem(renderSystem)
				, mResourceMgr(resourceMgr)
				, mDataProvider(dataProvider)
			{
				mBatch = new mpp::LineBatch(
					name,
					{
						PosType::vertexDataType(),
						{ params.useVertexColours ? ColType::vertexDataType() : mpp::mesh::Vertex::DataType::None, params.fixedColourData },
						params.useDiffuse,
					},
					mDataProvider->getNumPrimitives(),
					renderSystem,
					resourceMgr);

				mUniforms->setUniform("DIFFUSE", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
			}

			virtual ~LineBatchRenderer()
			{
				delete mBatch;
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
					initStart = mBatch->getPrimitiveCount(batchSize);
					newVertices = true;
				}

				mBatch->startUpdate(count, count * 2);

				typedef typename PosType::builtin_type PosTypeBuiltin;
				typedef typename ColType::builtin_type ColTypeBuiltin;

				auto posBuffer = (PosTypeBuiltin*)mBatch->getAttributeData(0, "POSITION").first;
				auto posStride = mBatch->getAttributeData(0, "POSITION").second / sizeof(PosTypeBuiltin);

				auto colBuffer = (ColTypeBuiltin*)mBatch->getAttributeData(0, "COLOUR").first;
				auto colStride = mBatch->getAttributeData(0, "COLOUR").second / sizeof(ColTypeBuiltin);

				size_t lineCount = mBatch->getPrimitiveCount(count);
				for (size_t pOffset = 0, cOffset = 0, i = 0; i < lineCount; ++i)
				{
					auto primitiveIndex = (uint32_t)i;
					bool newVertex = i >= initStart;

					//
					// Position data
					//
					if (!mBatch->positionFixed() || newVertex)
					{
						PosTypeBuiltin x0, y0, x1, y1;
						mDataProvider->position(primitiveIndex, x0, y0, x1, y1);

						posBuffer[pOffset + 0] = x0;
						posBuffer[pOffset + 1] = y0;
						pOffset += posStride;

						posBuffer[pOffset + 0] = x1;
						posBuffer[pOffset + 1] = y1;
						pOffset += posStride;
					}
					else
					{
						pOffset += posStride * 2;
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
						cOffset += colStride;

						colBuffer[cOffset + 0] = red;
						colBuffer[cOffset + 1] = green;
						colBuffer[cOffset + 2] = blue;
						colBuffer[cOffset + 3] = alpha;
						cOffset += colStride;
					}
					else
					{
						cOffset += colStride * 2;
					}
				}

				mBatch->finishUpdate(count, count * 2, newVertices);
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

	}
}