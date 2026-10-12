#pragma once

#include "mpp/Config.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace mpp
{
	class RenderSystem;
	namespace detail { class PersistentMappedBuffer; }

	class _MPPAPI UniformBuffer
	{
		std::uint32_t mUBO;

		RenderSystem* mwRenderSystem;

		std::vector<int8_t> mData;

		size_t mDataSize;
		
		uint32_t mBinding;

		std::unique_ptr<detail::PersistentMappedBuffer> mStreamBuffer;

	private:

		void allocate();

	public:

		UniformBuffer(RenderSystem* renderSystem, std::shared_ptr<const int8_t> data, size_t dataSize, uint32_t binding);

		virtual ~UniformBuffer();

		std::vector<int8_t>& getBufferData();

		bool usesPersistentMapping() const;

		void updateData(uint32_t offset, size_t size);

		void mapBufferData();

		void bind();

		void activate();

		void unbind();

		void load();

		void unload();
	};

}