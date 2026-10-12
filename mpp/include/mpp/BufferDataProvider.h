#pragma once

#include <memory>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec3.hpp>
#pragma warning(pop)

#include "mpp/Config.h"
#include "mpp/VertexBufferRenderCommand.h"

namespace mpp
{
	class _MPPAPI BufferDataProvider
	{
	public:

		virtual void getBounds(glm::vec3& bMin, glm::vec3& bMax) = 0;

		virtual uint32_t getNumCommands() = 0;

		virtual int8_t* const getVertexData(uint32_t command) = 0;

		virtual uint32_t getVertexStride(uint32_t command) = 0;

		virtual uint32_t getVertexCount(uint32_t command) = 0;

		virtual int8_t* const getIndexData(uint32_t command) = 0;

		virtual uint32_t getIndexWidth(uint32_t command) = 0;

		virtual uint32_t getIndexCount(uint32_t command) = 0;

		virtual std::vector<VertexBufferRenderCommand> getRenderCommands(uint32_t command) = 0;

		virtual bool update(float frameTime);
	};

	typedef std::shared_ptr<BufferDataProvider> BufferDataProviderPtr;
}