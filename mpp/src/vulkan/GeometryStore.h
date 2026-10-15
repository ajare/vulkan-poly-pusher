#pragma once
// Private native geometry store for the Vulkan geometry-upload subset.
//
// Owns device-local vertex/index buffers addressed by opaque engine IDs (never
// native handles). Uploads are staged through host-visible memory and submitted
// with their own fences: routine uploads and destruction never wait device-wide.
// Replacement and destruction retire native storage until every submission that
// could reference it has completed, tracked against a single submission serial
// shared with the window's frame contexts. Explicit diagnostic readback may
// wait for its own submission only.
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace mpp::detail {

class GeometryStore {
public:
    GeometryStore(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t queueFamily);
    ~GeometryStore();
    GeometryStore(GeometryStore const&) = delete;
    GeometryStore& operator=(GeometryStore const&) = delete;

    // Frees everything. Requires the device to be idle (teardown path only).
    void shutdown() noexcept;

    // Create a device-local buffer and stage the initial contents into it.
    // Returns an opaque engine ID; zero is never a live ID.
    uint32_t create(VkBufferUsageFlags usage, void const* data, size_t size, std::string label);

    // Staged update of an already-created buffer. Offset+size must fit the
    // existing storage; uploads are ordered by submission on one queue.
    void upload(uint32_t id, void const* data, size_t size, size_t offset);

    // Replace storage (growing or shrinking) without breaking in-flight work:
    // the old buffer is retired until its last possible reader completes.
    void replace(uint32_t id, void const* data, size_t size);

    // Deferred destruction: native storage is freed once all submissions
    // recorded so far are complete. Never throws (destructor path).
    void release(uint32_t id) noexcept;

    bool isLive(uint32_t id) const;
    size_t size(uint32_t id);

    // Diagnostic labels are an authored convenience for readback; rebinding an
    // existing label points it at the newest buffer.
    void bindLabel(uint32_t id, std::string label);
    std::vector<std::string> labels() const;

    // Explicit diagnostic readback. May wait for its own submission; ordinary
    // upload paths never do. Copy size is the buffer's recorded size.
    std::vector<uint8_t> readback(uint32_t id);
    std::vector<uint8_t> readbackLabel(std::string const& label);

    // Validate a translated vertex format against this device's vertex input.
    void requireVertexFormat(VkFormat format, std::string const& context) const;

    // Native handle of a live buffer for descriptor writes. Backend-private;
    // never escapes into public resource identifiers.
    VkBuffer handle(uint32_t id);
    void validateIndices(uint32_t id, size_t first, size_t count, size_t width, size_t vertexLimit);

    // Frame-context hooks driven by the window's presentation loop.
    uint64_t noteFrameSubmit() { return ++mSerial; }
    void noteFrameComplete(uint64_t serial);

    // Hold native storage across the interval between public draw and submit.
    VkBuffer pin(uint32_t id);
    void unpin(VkBuffer buffer) noexcept;

    // Poll completed upload fences (zero timeout) and free finished staging
    // plus retired buffers. Called from presentation and every store operation.
    void reap() noexcept;

private:
    struct Entry {
        VkBuffer buffer{};
        VkDeviceMemory memory{};
        VkBufferUsageFlags usage{};
        size_t size{};
        std::string label;
        // Last uploaded contents, not the caller's mutable CPU array. Enables
        // queued-draw copy-on-write and safe indexed range validation.
        std::vector<uint8_t> contents;
    };
    struct Pending {
        uint64_t serial;
        VkFence fence;
        VkCommandBuffer command;
        VkBuffer staging;
        VkDeviceMemory stagingMemory;
    };
    struct Retired {
        VkBuffer buffer;
        VkDeviceMemory memory;
        uint64_t safeAfter;
    };

    uint32_t memoryType(uint32_t typeBits, VkMemoryPropertyFlags required, char const* context) const;
    Entry allocate(VkBufferUsageFlags usage, size_t size, std::string label) const;
    void freeEntry(Entry& entry) const noexcept;
    void stageInto(Entry& entry, void const* data, size_t size, size_t offset);
    Entry& require(uint32_t id, char const* operation);

    VkPhysicalDevice mPhysical{};
    VkDevice mDevice{};
    VkQueue mQueue{};
    VkCommandPool mPool{};
    uint32_t mNextId{ 1 };
    uint64_t mSerial{ 0 };
    uint64_t mCompleted{ 0 };
    std::map<uint32_t, Entry> mEntries;
    std::map<std::string, uint32_t> mLabels;
    std::deque<Pending> mPending;
    std::vector<Retired> mRetired;
    std::map<VkBuffer, size_t> mPins;
};

}
