#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mpp::detail
{
	// Dynamic GPU buffer with a persistently mapped triple-buffered path and a
	// glBufferSubData fallback. The caller supplies complete CPU contents when a
	// persistent segment must rotate; partial fallback uploads remain supported.
	class PersistentMappedBuffer
	{
		static constexpr uint32_t SegmentCount = 3;

		uint32_t mTarget{ 0 };
		uint32_t mBuffer{ 0 };
		bool mPersistent{ false };
		std::byte* mMapping{ nullptr };
		size_t mCapacity{ 0 };
		size_t mSegmentStride{ 0 };
		size_t mAlignment{ 1 };
		uint32_t mActiveSegment{ 0 };
		bool mActiveUsed{ false };
		std::array<void*, SegmentCount> mFences{};
		std::string mLabel;

		static size_t alignedSize(size_t value, size_t alignment);
		void waitForSegment(uint32_t segment);
		void releaseStorage() noexcept;
		void swap(PersistentMappedBuffer& other) noexcept;
		void createStorage(size_t capacity, void const* data, size_t size);
		void ensureCapacity(size_t required, void const* data, size_t size);

	public:
		PersistentMappedBuffer() = default;
		~PersistentMappedBuffer();
		PersistentMappedBuffer(PersistentMappedBuffer const&) = delete;
		PersistentMappedBuffer& operator =(PersistentMappedBuffer const&) = delete;

		void create(uint32_t target, size_t capacity, size_t alignment, bool persistent,
			void const* data, size_t size, std::string label);
		void destroy() noexcept;
		void bind() const;
		void upload(void const* completeData, size_t completeSize, size_t changedOffset, size_t changedSize);
		void markUsed();

		uint32_t getBuffer() const { return mBuffer; }
		size_t getActiveOffset() const { return mPersistent ? mActiveSegment * mSegmentStride : 0; }
		bool isPersistent() const { return mPersistent; }
	};
}
