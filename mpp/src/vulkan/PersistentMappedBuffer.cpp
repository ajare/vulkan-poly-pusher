// Link support for the frozen private PersistentMappedBuffer type: the native
// geometry-upload subset never instantiates it (all uploads are staged), so the
// streaming persistent-mapped path fails explicitly rather than silently
// pretending to map. The destructor and destroy() stay safe no-ops.
#include "../PersistentMappedBuffer.h"
#include <stdexcept>

namespace mpp::detail
{
	PersistentMappedBuffer::~PersistentMappedBuffer() = default;

	void PersistentMappedBuffer::create(uint32_t target, size_t capacity, size_t alignment, bool persistent,
		void const* data, size_t size, std::string label)
	{
		(void)target; (void)capacity; (void)alignment; (void)persistent; (void)data; (void)size; (void)label;
		throw std::runtime_error("Persistent-mapped streaming geometry is not delivered in the Vulkan geometry-upload subset; uploads are staged");
	}

	void PersistentMappedBuffer::destroy() noexcept {}

	void PersistentMappedBuffer::bind() const
	{
		throw std::runtime_error("Persistent-mapped streaming geometry is not delivered in the Vulkan geometry-upload subset");
	}

	void PersistentMappedBuffer::upload(void const* completeData, size_t completeSize, size_t changedOffset, size_t changedSize)
	{
		(void)completeData; (void)completeSize; (void)changedOffset; (void)changedSize;
		throw std::runtime_error("Persistent-mapped streaming geometry is not delivered in the Vulkan geometry-upload subset; uploads are staged");
	}

	void PersistentMappedBuffer::markUsed() {}
}
