#include "PipelineStore.h"
#include "GeometryStore.h"
#include "VertexInputTranslation.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace mpp::detail {
namespace {
void check(VkResult r, char const* operation) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(r) + ")");
}
// The parser-generated loose `_mpp_u_*` uniforms are lifted into this shared
// std140 block by the compiler (#93); that slot is the native uniform path.
constexpr uint32_t kUniformSet = 0;
constexpr uint32_t kUniformBinding = 0;
}

PipelineStore::PipelineStore(VkPhysicalDevice physical, VkDevice device, GeometryStore& buffers, uint32_t frameContexts)
    : mPhysical(physical), mDevice(device), mBuffers(&buffers), mFrameContexts(frameContexts) {
    if (!frameContexts) throw std::runtime_error("Pipeline realization requires at least one frame context");
}

PipelineStore::~PipelineStore() { shutdown(); }

void PipelineStore::shutdown() noexcept {
    if (!mDevice) return;
    // Teardown runs after device idle: all submissions are complete.
    for (auto& retired : mRetired) {
        Entry entry;
        entry.vertex = retired.vertex; entry.fragment = retired.fragment;
        entry.setLayout = retired.setLayout; entry.pipelineLayout = retired.pipelineLayout;
        entry.pipeline = retired.pipeline; entry.pool = retired.pool;
        destroyEntryObjects(entry);
    }
    mRetired.clear();
    for (auto& [id, entry] : mEntries) {
        for (auto buffer : entry.uniformBuffers) mBuffers->release(buffer);
        destroyEntryObjects(entry);
    }
    mEntries.clear();
    for (auto& [format, pass] : mRenderPasses) vkDestroyRenderPass(mDevice, pass, nullptr);
    mRenderPasses.clear();
    mSelected = 0;
    mDevice = {};
}

void PipelineStore::setColourFormat(VkFormat format) noexcept { mColourFormat = format; }

VkRenderPass PipelineStore::renderPass(VkFormat format) {
    auto it = mRenderPasses.find(format);
    if (it != mRenderPasses.end()) return it->second;
    // The colour-only render pass the M01 draw slice (#96) records against.
    // Pipeline compatibility is structural (formats and samples), so the
    // recording pass may choose its own load/store ops for the same format.
    VkAttachmentDescription colour{};
    colour.format = format;
    colour.samples = VK_SAMPLE_COUNT_1_BIT;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colour.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colour.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colour.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colour.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference reference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkRenderPassCreateInfo info{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    info.attachmentCount = 1;
    info.pAttachments = &colour;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    VkRenderPass pass{};
    check(vkCreateRenderPass(mDevice, &info, nullptr, &pass), "create colour render pass");
    mRenderPasses.emplace(format, pass);
    return pass;
}

void PipelineStore::destroyEntryObjects(Entry& entry) const noexcept {
    if (entry.pipeline) vkDestroyPipeline(mDevice, entry.pipeline, nullptr);
    if (entry.pool) vkDestroyDescriptorPool(mDevice, entry.pool, nullptr); // frees its sets
    if (entry.pipelineLayout) vkDestroyPipelineLayout(mDevice, entry.pipelineLayout, nullptr);
    if (entry.setLayout) vkDestroyDescriptorSetLayout(mDevice, entry.setLayout, nullptr);
    if (entry.vertex) vkDestroyShaderModule(mDevice, entry.vertex, nullptr);
    if (entry.fragment) vkDestroyShaderModule(mDevice, entry.fragment, nullptr);
    entry.vertex = entry.fragment = {};
    entry.setLayout = {}; entry.pipelineLayout = {}; entry.pipeline = {}; entry.pool = {};
    entry.sets.clear();
}

uint32_t PipelineStore::realizeProgram(std::shared_ptr<CompiledProgram const> compiled,
    mesh::MeshSpecification const& spec, std::string const& label) {
    auto const fail = [&](std::string const& message) -> void {
        throw std::runtime_error("Program '" + label + "': " + message);
    };
    if (mColourFormat == VK_FORMAT_UNDEFINED)
        fail("the renderer window has no colour format; initialize the window before loading programs");
    reap();

    // Merge the stage binding lists; cross-stage collisions were already
    // proven identical by compilation (#93).
    std::vector<ShaderBinding> bindings = compiled->vertex.bindings;
    for (auto const& binding : compiled->fragment.bindings) {
        auto const duplicate = std::find_if(bindings.begin(), bindings.end(), [&](auto const& existing) {
            return existing.set == binding.set && existing.binding == binding.binding; });
        if (duplicate == bindings.end()) bindings.push_back(binding);
    }
    ShaderBinding const* uniforms = nullptr;
    VkShaderStageFlags uniformStages = 0;
    for (auto const& binding : bindings) {
        auto const where = "set " + std::to_string(binding.set) + " binding " + std::to_string(binding.binding) +
            " ('" + binding.name + "')";
        if (binding.kind == ShaderBinding::Kind::CombinedImageSampler)
            fail("sampler descriptor " + where + " requires texture resources, which are not delivered in the "
                "Vulkan program/material subset");
        if (binding.set != kUniformSet || binding.binding != kUniformBinding)
            fail("authored uniform block " + where + " is not delivered in the Vulkan program/material subset; "
                "author material values through @@Uniform markup");
        if (binding.count != 1)
            fail("uniform block arrays are not delivered: " + where);
        if (uniforms && uniforms->members != binding.members)
            fail("generated uniform blocks disagree across stages: " + where);
        if (!uniforms) uniforms = &binding; // stable: `bindings` outlives realization
    }
    std::vector<UniformMember> members;
    size_t uniformSize = 0;
    if (uniforms) {
        members = uniforms->members;
        uniformSize = uniforms->size;
        auto used = [&](std::vector<ShaderBinding> const& stage) {
            return std::any_of(stage.begin(), stage.end(), [](auto const& b) {
                return b.set == kUniformSet && b.binding == kUniformBinding; });
        };
        if (used(compiled->vertex.bindings)) uniformStages |= VK_SHADER_STAGE_VERTEX_BIT;
        if (used(compiled->fragment.bindings)) uniformStages |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    // Vertex input state from the authored mesh layout (#94) matched against
    // the reflected inputs (#93); mismatches fail naming the attribute.
    auto vertexInput = buildVertexInputState(spec, compiled->vertex.inputs, label);
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(mPhysical, &properties);
        for (auto const& input : compiled->vertex.inputs)
            if (input.location >= properties.limits.maxVertexInputAttributes)
                fail("vertex input '" + input.name + "' uses location " + std::to_string(input.location) +
                    " but the device supports " + std::to_string(properties.limits.maxVertexInputAttributes) + " vertex input attributes");
    }
    for (auto const& attribute : vertexInput.attributes)
        mBuffers->requireVertexFormat(attribute.format, "Program '" + label + "'");

    // Transactional assembly: every native object lives in a local entry and
    // is destroyed on the exception path before the error propagates.
    Entry entry;
    entry.compiled = std::move(compiled);
    entry.label = label;
    entry.members = std::move(members);
    try {
        VkShaderModuleCreateInfo module{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        module.codeSize = entry.compiled->vertex.spirv.size() * sizeof(uint32_t);
        module.pCode = entry.compiled->vertex.spirv.data();
        check(vkCreateShaderModule(mDevice, &module, nullptr, &entry.vertex), "create vertex shader module");
        module.codeSize = entry.compiled->fragment.spirv.size() * sizeof(uint32_t);
        module.pCode = entry.compiled->fragment.spirv.data();
        check(vkCreateShaderModule(mDevice, &module, nullptr, &entry.fragment), "create fragment shader module");

        if (uniforms) {
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = kUniformBinding;
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            layoutBinding.descriptorCount = 1;
            layoutBinding.stageFlags = uniformStages;
            VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            layoutInfo.bindingCount = 1;
            layoutInfo.pBindings = &layoutBinding;
            check(vkCreateDescriptorSetLayout(mDevice, &layoutInfo, nullptr, &entry.setLayout), "create descriptor set layout");

            entry.uniformImage.assign(uniformSize, 0);
            std::vector<uint8_t> zeros(uniformSize, 0);
            for (uint32_t context = 0; context < mFrameContexts; ++context)
                entry.uniformBuffers.push_back(mBuffers->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    zeros.data(), zeros.size(), "Program '" + label + "' uniform buffer (frame context " + std::to_string(context) + ")"));

            VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, mFrameContexts };
            VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            poolInfo.maxSets = mFrameContexts;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            check(vkCreateDescriptorPool(mDevice, &poolInfo, nullptr, &entry.pool), "create descriptor pool");
            std::vector<VkDescriptorSetLayout> layouts(mFrameContexts, entry.setLayout);
            VkDescriptorSetAllocateInfo allocate{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            allocate.descriptorPool = entry.pool;
            allocate.descriptorSetCount = mFrameContexts;
            allocate.pSetLayouts = layouts.data();
            entry.sets.resize(mFrameContexts);
            check(vkAllocateDescriptorSets(mDevice, &allocate, entry.sets.data()), "allocate descriptor sets");
            for (uint32_t context = 0; context < mFrameContexts; ++context) {
                VkDescriptorBufferInfo buffer{ mBuffers->handle(entry.uniformBuffers[context]), 0, uniformSize };
                VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                write.dstSet = entry.sets[context];
                write.dstBinding = kUniformBinding;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.pBufferInfo = &buffer;
                vkUpdateDescriptorSets(mDevice, 1, &write, 0, nullptr);
            }
        }

        VkPipelineLayoutCreateInfo pipelineLayout{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayout.setLayoutCount = entry.setLayout ? 1u : 0u;
        pipelineLayout.pSetLayouts = &entry.setLayout;
        check(vkCreatePipelineLayout(mDevice, &pipelineLayout, nullptr, &entry.pipelineLayout), "create pipeline layout");

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = entry.vertex;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = entry.fragment;
        stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vertexState{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexState.vertexBindingDescriptionCount = uint32_t(vertexInput.bindings.size());
        vertexState.pVertexBindingDescriptions = vertexInput.bindings.data();
        vertexState.vertexAttributeDescriptionCount = uint32_t(vertexInput.attributes.size());
        vertexState.pVertexAttributeDescriptions = vertexInput.attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        assembly.topology = vertexInput.topology;
        VkPipelineViewportStateCreateInfo viewport{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkDynamicState dynamicStates[]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;
        VkPipelineRasterizationStateCreateInfo rasterization{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        // The authored program/material carries no culling contract in this
        // subset; rasterize both faces rather than reconstructing GL state.
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_FALSE; // BasicMaterial is opaque in this subset
        VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        VkGraphicsPipelineCreateInfo pipeline{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline.stageCount = 2;
        pipeline.pStages = stages;
        pipeline.pVertexInputState = &vertexState;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &rasterization;
        pipeline.pMultisampleState = &multisample;
        pipeline.pDepthStencilState = &depthStencil;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = entry.pipelineLayout;
        pipeline.renderPass = renderPass(mColourFormat);
        check(vkCreateGraphicsPipelines(mDevice, VK_NULL_HANDLE, 1, &pipeline, nullptr, &entry.pipeline), "create graphics pipeline");
    } catch (...) {
        for (auto buffer : entry.uniformBuffers) mBuffers->release(buffer);
        destroyEntryObjects(entry);
        throw;
    }

    auto id = mNextId++;
    if (!id) id = mNextId++;
    mEntries.emplace(id, std::move(entry));
    return id;
}

void PipelineStore::releaseProgram(uint32_t id) noexcept {
    auto it = mEntries.find(id);
    if (it == mEntries.end()) return;
    Entry entry = std::move(it->second);
    mEntries.erase(it);
    if (mSelected == id) mSelected = 0;
    for (auto buffer : entry.uniformBuffers) mBuffers->release(buffer); // deferred there
    mRetired.push_back({ entry.vertex, entry.fragment, entry.setLayout, entry.pipelineLayout,
        entry.pipeline, entry.pool, mSerial });
    reap();
}

bool PipelineStore::isLive(uint32_t id) const { return mEntries.find(id) != mEntries.end(); }

PipelineStore::Entry& PipelineStore::require(uint32_t id, char const* operation) {
    auto it = mEntries.find(id);
    if (it == mEntries.end())
        throw std::runtime_error(std::string(operation) + ": program is not realized (call load() first)");
    return it->second;
}

PipelineStore::Entry const& PipelineStore::require(uint32_t id, char const* operation) const {
    return const_cast<PipelineStore*>(this)->require(id, operation);
}

std::vector<UniformMember> const& PipelineStore::uniformMemberTable(uint32_t id) const {
    return require(id, "Uniform member query").members;
}

std::vector<uint8_t>& PipelineStore::uniformImage(uint32_t id) {
    return require(id, "Uniform update").uniformImage;
}

size_t PipelineStore::uniformBlockSize(uint32_t id) const {
    return require(id, "Uniform block query").uniformImage.size();
}

void PipelineStore::flushUniforms(uint32_t id) {
    auto& entry = require(id, "Uniform upload");
    if (entry.uniformImage.empty()) return;
    for (auto buffer : entry.uniformBuffers)
        mBuffers->upload(buffer, entry.uniformImage.data(), entry.uniformImage.size(), 0);
}

std::vector<uint8_t> PipelineStore::readbackUniforms(uint32_t id, size_t frameContext) {
    auto& entry = require(id, "Diagnostic uniform readback");
    if (entry.uniformBuffers.empty())
        throw std::runtime_error("Diagnostic uniform readback of program '" + entry.label +
            "': the program declares no generated uniform block");
    if (frameContext >= entry.uniformBuffers.size())
        throw std::runtime_error("Diagnostic uniform readback of program '" + entry.label + "': frame context " +
            std::to_string(frameContext) + " does not exist (" + std::to_string(entry.uniformBuffers.size()) + " contexts)");
    return mBuffers->readback(entry.uniformBuffers[frameContext]);
}

void PipelineStore::select(uint32_t id) {
    require(id, "Program binding");
    mSelected = id;
}

void PipelineStore::noteFrameComplete() {
    // The frame fence wait retires every earlier submission on this queue.
    mCompleted = std::max(mCompleted, mSerial);
    reap();
}

void PipelineStore::reap() noexcept {
    if (!mDevice) return;
    auto remaining = std::remove_if(mRetired.begin(), mRetired.end(), [&](Retired const& retired) {
        if (retired.safeAfter > mCompleted) return false;
        Entry entry;
        entry.vertex = retired.vertex; entry.fragment = retired.fragment;
        entry.setLayout = retired.setLayout; entry.pipelineLayout = retired.pipelineLayout;
        entry.pipeline = retired.pipeline; entry.pool = retired.pool;
        destroyEntryObjects(entry);
        return true;
    });
    mRetired.erase(remaining, mRetired.end());
}

}
