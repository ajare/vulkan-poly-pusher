#pragma once
// Private native program/pipeline store for the Vulkan program/material
// realization subset (#95).
//
// Realizes an authored program's compiled shader variant (#93) into native
// execution objects: shader modules, a descriptor set layout and pipeline
// layout derived from the reflected bindings, and a graphics pipeline whose
// vertex input state matches the authored geometry layouts (#94). The
// generated `_mpp_Uniforms` block is backed by one uniform buffer per
// presentation frame context, reached through descriptor sets — there is no
// global uniform state. Uniform buffers live in the geometry store so staged
// uploads and completion-based deferred destruction behave identically;
// pipeline objects retire against the same frame-completion watermark.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include "mpp/CompiledProgram.h"
#include "mpp/mesh/MeshSpecification.h"

namespace mpp::detail {

class GeometryStore;

class PipelineStore {
public:
    PipelineStore(VkPhysicalDevice physical, VkDevice device, GeometryStore& buffers, uint32_t frameContexts);
    ~PipelineStore();
    PipelineStore(PipelineStore const&) = delete;
    PipelineStore& operator=(PipelineStore const&) = delete;

    // Frees everything. Requires the device to be idle (teardown path only).
    void shutdown() noexcept;

    // The window reports its swapchain colour format after every recreation;
    // pipelines are created against a compatible colour-only render pass.
    void setColourFormat(VkFormat format) noexcept;

    // Compile products + authored mesh layout -> native pipeline and uniform
    // storage. Transactional: a failure leaves no partial native state.
    // Returns an opaque engine ID; zero is never a live ID.
    uint32_t realizeProgram(std::shared_ptr<CompiledProgram const> compiled,
        mesh::MeshSpecification const& spec, std::string const& label);

    // Deferred retirement: native objects are destroyed once every submission
    // recorded so far has completed. Never throws (destructor path).
    void releaseProgram(uint32_t id) noexcept;

    bool isLive(uint32_t id) const;

    uint32_t frameContextCount() const { return mFrameContexts; }

    // Generated `_mpp_Uniforms` block access for the native uniform path.
    // The table is empty and the image zero-sized when the program declares
    // no generated uniforms.
    std::vector<UniformMember> const& uniformMemberTable(uint32_t id) const;
    std::vector<uint8_t>& uniformImage(uint32_t id);
    size_t uniformBlockSize(uint32_t id) const;

    // Stage the CPU uniform image into every frame context's uniform buffer.
    void flushUniforms(uint32_t id);

    // Explicit diagnostic readback of one frame context's uniform storage.
    // May wait for its own submission; uniform uploads never wait device-wide.
    std::vector<uint8_t> readbackUniforms(uint32_t id, size_t frameContext);

    // Native meaning of Program::bind(): select the realized pipeline as input
    // for subsequent renderer operations (consumed by the drawing slice).
    void select(uint32_t id);
    uint32_t selected() const { return mSelected; }

    // Frame-context hooks driven by the window's presentation loop.
    void noteFrameSubmit() { ++mSerial; }
    void noteFrameComplete();

private:
    struct Entry {
        std::shared_ptr<CompiledProgram const> compiled;
        VkShaderModule vertex{};
        VkShaderModule fragment{};
        VkDescriptorSetLayout setLayout{};
        VkPipelineLayout pipelineLayout{};
        VkPipeline pipeline{};
        VkDescriptorPool pool{};
        std::vector<VkDescriptorSet> sets;
        std::vector<uint32_t> uniformBuffers; // GeometryStore IDs, per frame context
        std::vector<UniformMember> members;
        std::vector<uint8_t> uniformImage;
        std::string label;
    };
    struct Retired {
        VkShaderModule vertex{}, fragment{};
        VkDescriptorSetLayout setLayout{};
        VkPipelineLayout pipelineLayout{};
        VkPipeline pipeline{};
        VkDescriptorPool pool{};
        uint64_t safeAfter{};
    };

    VkRenderPass renderPass(VkFormat format);
    void destroyEntryObjects(Entry& entry) const noexcept;
    Entry& require(uint32_t id, char const* operation);
    Entry const& require(uint32_t id, char const* operation) const;
    void reap() noexcept;

    VkPhysicalDevice mPhysical{};    VkDevice mDevice{};
    GeometryStore* mBuffers{};
    uint32_t mFrameContexts{};
    VkFormat mColourFormat{ VK_FORMAT_UNDEFINED };
    uint32_t mNextId{ 1 };
    uint64_t mSerial{ 0 };
    uint64_t mCompleted{ 0 };
    uint32_t mSelected{ 0 };
    std::map<uint32_t, Entry> mEntries;
    std::map<VkFormat, VkRenderPass> mRenderPasses;
    std::vector<Retired> mRetired;
};

}
