#include "GeometryStore.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace mpp::detail {
namespace {
void check(VkResult r, char const* operation) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(r) + ")");
}
}

GeometryStore::GeometryStore(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t queueFamily)
    : mPhysical(physical), mDevice(device), mQueue(queue) {
    VkCommandPoolCreateInfo pool{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queueFamily;
    check(vkCreateCommandPool(device, &pool, nullptr, &mPool), "create geometry upload command pool");
}

GeometryStore::~GeometryStore() { shutdown(); }

void GeometryStore::shutdown() noexcept {
    if (!mDevice) return;
    // Teardown runs after device idle: pending and retired work is complete.
    for (auto& pending : mPending) {
        vkDestroyFence(mDevice, pending.fence, nullptr);
        vkFreeCommandBuffers(mDevice, mPool, 1, &pending.command);
        vkDestroyBuffer(mDevice, pending.staging, nullptr);
        vkFreeMemory(mDevice, pending.stagingMemory, nullptr);
    }
    mPending.clear();
    for (auto& retired : mRetired) {
        vkDestroyBuffer(mDevice, retired.buffer, nullptr);
        vkFreeMemory(mDevice, retired.memory, nullptr);
    }
    mRetired.clear();
    for (auto& [id, entry] : mEntries) freeEntry(entry);
    mEntries.clear();
    mLabels.clear();
    if (mPool) vkDestroyCommandPool(mDevice, mPool, nullptr);
    mPool = {};
    mDevice = {};
}

uint32_t GeometryStore::memoryType(uint32_t typeBits, VkMemoryPropertyFlags required, char const* context) const {
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(mPhysical, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & required) == required) return i;
    throw std::runtime_error(std::string("No suitable memory type for ") + context);
}

GeometryStore::Entry GeometryStore::allocate(VkBufferUsageFlags usage, size_t size, std::string label) const {
    Entry entry;
    entry.size = size;
    entry.usage = usage;
    entry.label = std::move(label);
    VkBufferCreateInfo buffer{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    // Zero-sized MPP buffers still get native storage so IDs stay meaningful.
    buffer.size = std::max<size_t>(1, size);
    buffer.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(mDevice, &buffer, nullptr, &entry.buffer), "create geometry buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(mDevice, entry.buffer, &requirements);
    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "device-local geometry storage");
    check(vkAllocateMemory(mDevice, &allocate, nullptr, &entry.memory), "allocate geometry memory");
    check(vkBindBufferMemory(mDevice, entry.buffer, entry.memory, 0), "bind geometry memory");
    return entry;
}

void GeometryStore::freeEntry(Entry& entry) const noexcept {
    if (entry.buffer) vkDestroyBuffer(mDevice, entry.buffer, nullptr);
    if (entry.memory) vkFreeMemory(mDevice, entry.memory, nullptr);
    entry = {};
}

GeometryStore::Entry& GeometryStore::require(uint32_t id, char const* operation) {
    auto it = mEntries.find(id);
    if (it == mEntries.end())
        throw std::runtime_error(std::string(operation) + ": geometry buffer is not loaded (call load() first)");
    return it->second;
}

void GeometryStore::stageInto(Entry const& entry, void const* data, size_t size, size_t offset) {
    if (!size) return;
    if (offset > entry.size || size > entry.size - offset)
        throw std::runtime_error("Staged geometry upload of " + std::to_string(size) + " bytes at offset " +
            std::to_string(offset) + " exceeds its " + std::to_string(entry.size) + "-byte buffer");
    VkBufferCreateInfo stagingInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging{};
    check(vkCreateBuffer(mDevice, &stagingInfo, nullptr, &staging), "create staging buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(mDevice, staging, &requirements);
    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "host-visible geometry staging");
    VkDeviceMemory stagingMemory{};
    check(vkAllocateMemory(mDevice, &allocate, nullptr, &stagingMemory), "allocate staging memory");
    check(vkBindBufferMemory(mDevice, staging, stagingMemory, 0), "bind staging memory");
    void* mapped{};
    check(vkMapMemory(mDevice, stagingMemory, 0, size, 0, &mapped), "map staging memory");
    std::memcpy(mapped, data, size);
    vkUnmapMemory(mDevice, stagingMemory);

    VkCommandBufferAllocateInfo command{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    command.commandPool = mPool;
    command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command.commandBufferCount = 1;
    VkCommandBuffer transfer{};
    check(vkAllocateCommandBuffers(mDevice, &command, &transfer), "allocate upload command");
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(transfer, &begin), "begin upload command");
    VkBufferCopy copy{ 0, offset, size };
    vkCmdCopyBuffer(transfer, staging, entry.buffer, 1, &copy);
    // Make the transfer writes visible to later transfer reads (diagnostic
    // readback) and to the vertex-input stage once drawing is delivered.
    VkBufferMemoryBarrier visible{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    visible.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    visible.srcQueueFamilyIndex = visible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    visible.buffer = entry.buffer;
    visible.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(transfer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &visible, 0, nullptr);
    check(vkEndCommandBuffer(transfer), "end upload command");
    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence{};
    check(vkCreateFence(mDevice, &fenceInfo, nullptr, &fence), "create upload fence");
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &transfer;
    check(vkQueueSubmit(mQueue, 1, &submit, fence), "submit geometry upload");
    mPending.push_back({ ++mSerial, fence, transfer, staging, stagingMemory });
}

uint32_t GeometryStore::create(VkBufferUsageFlags usage, void const* data, size_t size, std::string label) {
    reap();
    auto id = mNextId++;
    if (!id) id = mNextId++;
    mEntries.emplace(id, allocate(usage, size, std::move(label)));
    try {
        stageInto(mEntries.at(id), data, size, 0);
    } catch (...) {
        Entry entry = std::move(mEntries.at(id));
        mEntries.erase(id);
        freeEntry(entry); // never submitted: safe to free immediately
        throw;
    }
    return id;
}

void GeometryStore::upload(uint32_t id, void const* data, size_t size, size_t offset) {
    reap();
    stageInto(require(id, "Geometry upload"), data, size, offset);
}

void GeometryStore::replace(uint32_t id, void const* data, size_t size) {
    reap();
    auto& current = require(id, "Geometry buffer replacement");
    auto const usage = current.usage;
    Entry retiredEntry = std::move(current);
    current = Entry{}; // scalar handles copy on move; zero them to avoid double frees
    try {
        current = allocate(usage, size, retiredEntry.label);
        stageInto(current, data, size, 0);
    } catch (...) {
        freeEntry(current); // a partially replaced buffer is freed; the old one stays live
        current = std::move(retiredEntry);
        throw;
    }
    mRetired.push_back({ retiredEntry.buffer, retiredEntry.memory, mSerial });
    reap();
}

void GeometryStore::release(uint32_t id) noexcept {
    auto it = mEntries.find(id);
    if (it == mEntries.end()) return;
    Entry entry = std::move(it->second);
    mEntries.erase(it);
    mRetired.push_back({ entry.buffer, entry.memory, mSerial });
    reap();
}

bool GeometryStore::isLive(uint32_t id) const { return mEntries.find(id) != mEntries.end(); }

size_t GeometryStore::size(uint32_t id) { return require(id, "Geometry size query").size; }

void GeometryStore::bindLabel(uint32_t id, std::string label) {
    require(id, "Geometry label binding");
    mLabels[std::move(label)] = id;
}

std::vector<std::string> GeometryStore::labels() const {
    std::vector<std::string> result;
    for (auto const& [label, id] : mLabels)
        if (mEntries.find(id) != mEntries.end()) result.push_back(label);
    return result;
}

std::vector<uint8_t> GeometryStore::readback(uint32_t id) {
    reap();
    auto& entry = require(id, "Diagnostic geometry readback");
    std::vector<uint8_t> bytes(entry.size);
    if (!entry.size) return bytes;
    VkBufferCreateInfo stagingInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = entry.size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging{};
    check(vkCreateBuffer(mDevice, &stagingInfo, nullptr, &staging), "create readback buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(mDevice, staging, &requirements);
    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "host-visible geometry readback");
    VkDeviceMemory stagingMemory{};
    check(vkAllocateMemory(mDevice, &allocate, nullptr, &stagingMemory), "allocate readback memory");
    check(vkBindBufferMemory(mDevice, staging, stagingMemory, 0), "bind readback memory");
    VkCommandBufferAllocateInfo command{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    command.commandPool = mPool;
    command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command.commandBufferCount = 1;
    VkCommandBuffer transfer{};
    check(vkAllocateCommandBuffers(mDevice, &command, &transfer), "allocate readback command");
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(transfer, &begin), "begin readback command");
    VkBufferCopy copy{ 0, 0, entry.size };
    vkCmdCopyBuffer(transfer, entry.buffer, staging, 1, &copy);
    VkBufferMemoryBarrier host{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = staging;
    host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(transfer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
    check(vkEndCommandBuffer(transfer), "end readback command");
    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence{};
    check(vkCreateFence(mDevice, &fenceInfo, nullptr, &fence), "create readback fence");
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &transfer;
    check(vkQueueSubmit(mQueue, 1, &submit, fence), "submit geometry readback");
    auto const serial = ++mSerial;
    // Explicit diagnostic capture may wait; routine uploads never take this path.
    check(vkWaitForFences(mDevice, 1, &fence, VK_TRUE, UINT64_MAX), "wait geometry readback");
    mCompleted = std::max(mCompleted, serial);
    void* mapped{};
    check(vkMapMemory(mDevice, stagingMemory, 0, entry.size, 0, &mapped), "map readback memory");
    std::memcpy(bytes.data(), mapped, entry.size);
    vkUnmapMemory(mDevice, stagingMemory);
    vkDestroyFence(mDevice, fence, nullptr);
    vkFreeCommandBuffers(mDevice, mPool, 1, &transfer);
    vkDestroyBuffer(mDevice, staging, nullptr);
    vkFreeMemory(mDevice, stagingMemory, nullptr);
    reap();
    return bytes;
}

std::vector<uint8_t> GeometryStore::readbackLabel(std::string const& label) {
    auto it = mLabels.find(label);
    if (it == mLabels.end() || mEntries.find(it->second) == mEntries.end())
        throw std::runtime_error("No live uploaded geometry buffer labelled '" + label +
            "'; labels follow \"Mesh '<name>' vertex buffer <i>\" / \"Mesh '<name>' index buffer\"");
    return readback(it->second);
}

VkBuffer GeometryStore::handle(uint32_t id) { return require(id, "Geometry handle query").buffer; }

void GeometryStore::requireVertexFormat(VkFormat format, std::string const& context) const {
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(mPhysical, format, &properties);
    if (!(properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
        throw std::runtime_error(context + ": vertex format " + std::to_string(unsigned(format)) +
            " is not supported for vertex input by device " + [&] {
                VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(mPhysical, &p);
                return std::string(p.deviceName); }() +
            "; use a float or 32-bit attribute layout");
}

void GeometryStore::noteFrameComplete() {
    // The frame fence wait implies every earlier submission on this queue.
    mCompleted = std::max(mCompleted, mSerial);
    reap();
}

void GeometryStore::reap() noexcept {
    if (!mDevice) return;
    while (!mPending.empty()) {
        auto& pending = mPending.front();
        if (vkGetFenceStatus(mDevice, pending.fence) != VK_SUCCESS) break;
        mCompleted = std::max(mCompleted, pending.serial);
        vkDestroyFence(mDevice, pending.fence, nullptr);
        vkFreeCommandBuffers(mDevice, mPool, 1, &pending.command);
        vkDestroyBuffer(mDevice, pending.staging, nullptr);
        vkFreeMemory(mDevice, pending.stagingMemory, nullptr);
        mPending.pop_front();
    }
    auto remaining = std::remove_if(mRetired.begin(), mRetired.end(), [&](Retired const& retired) {
        if (retired.safeAfter > mCompleted) return false;
        vkDestroyBuffer(mDevice, retired.buffer, nullptr);
        vkFreeMemory(mDevice, retired.memory, nullptr);
        return true;
    });
    mRetired.erase(remaining, mRetired.end());
}

}
