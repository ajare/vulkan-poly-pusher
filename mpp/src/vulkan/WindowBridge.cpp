#include "WindowBridge.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <array>
#include <algorithm>
#include <fstream>
#include <map>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdlib>

namespace mpp::detail {
namespace {
void check(VkResult r, char const* operation) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(r) + ")");
}
std::map<SDL_Window*, std::shared_ptr<VulkanWindow>> windows;
}
struct VulkanWindow::Impl {
    SDL_Window* window;
    bool vsync;
    bool closed{};
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceProperties properties{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    VkSwapchainKHR swapchain{};
    VkFormat format{};
    VkExtent2D extent{};
    std::vector<VkImage> images;
    // Presentation waits are not covered by submission fences. Reuse a present
    // semaphore only after reacquiring its image, not when a frame fence signals.
    std::vector<VkSemaphore> presented;
    struct Frame { VkCommandPool pool{}; VkCommandBuffer command{}; VkSemaphore acquired{}; VkFence fence{}; };
    std::array<Frame, 2> frames{};
    size_t frameIndex{};
    Colour colour{0, 0, 0, 1};
    std::string capturePath;
    VkBuffer readback{};
    VkDeviceMemory readbackMemory{};
    bool live() const { return device && !closed; }
    void require() const { if (!live()) throw std::runtime_error("Vulkan renderer is not initialized or its window was destroyed"); }
    void freeReadback() noexcept {
        if (readback) vkDestroyBuffer(device, readback, nullptr);
        if (readbackMemory) vkFreeMemory(device, readbackMemory, nullptr);
        readback = {}; readbackMemory = {};
    }
    void freeSwapchain() noexcept {
        for (auto s : presented) vkDestroySemaphore(device, s, nullptr);
        presented.clear(); images.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = {};
    }
    void shutdown() noexcept {
        closed = true;
        if (device) {
            vkDeviceWaitIdle(device); // teardown only
            freeReadback(); freeSwapchain();
            for (auto& f : frames) {
                if (f.fence) vkDestroyFence(device, f.fence, nullptr);
                if (f.acquired) vkDestroySemaphore(device, f.acquired, nullptr);
                if (f.pool) vkDestroyCommandPool(device, f.pool, nullptr);
                f = {};
            }
            vkDestroyDevice(device, nullptr); device = {};
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = {};
        if (instance) vkDestroyInstance(instance, nullptr);
        instance = {};
    }
    bool recreate() {
        int w{}, h{};
        if (!SDL_GetWindowSizeInPixels(window, &w, &h)) throw std::runtime_error(SDL_GetError());
        if (w <= 0 || h <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return false;
        VkSurfaceCapabilitiesKHR caps;
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "query surface capabilities");
        auto usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if ((caps.supportedUsageFlags & usage) != usage)
            throw std::runtime_error("Vulkan surface requires transfer-destination clear and transfer-source capture support");
        uint32_t count{};
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr), "query surface formats");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()), "query surface formats");
        auto chosen = std::find_if(formats.begin(), formats.end(), [](auto f) {
            return (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (chosen == formats.end()) throw std::runtime_error("Vulkan clear subset requires RGBA8/BGRA8 UNORM with SRGB_NONLINEAR presentation");
        VkExtent2D next = caps.currentExtent;
        if (next.width == UINT32_MAX) next = {std::clamp(uint32_t(w), caps.minImageExtent.width, caps.maxImageExtent.width), std::clamp(uint32_t(h), caps.minImageExtent.height, caps.maxImageExtent.height)};
        if (!next.width || !next.height) return false;
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr), "query present modes");
        std::vector<VkPresentModeKHR> modes(count);
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data()), "query present modes");
        if (!vsync) for (auto candidate : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
            if (std::find(modes.begin(), modes.end(), candidate) != modes.end()) { mode = candidate; break; }
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & a) { alpha = a; break; }
        uint32_t imageCount = std::max(2u, caps.minImageCount);
        if (caps.maxImageCount) imageCount = std::min(imageCount, caps.maxImageCount);
        // Resize/out-of-date recovery is exceptional, not the per-frame path.
        check(vkDeviceWaitIdle(device), "wait for swapchain retirement");
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface; info.minImageCount = imageCount;
        info.imageFormat = chosen->format; info.imageColorSpace = chosen->colorSpace;
        info.imageExtent = next; info.imageArrayLayers = 1; info.imageUsage = usage;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; info.preTransform = caps.currentTransform;
        info.compositeAlpha = alpha; info.presentMode = mode; info.clipped = VK_TRUE; info.oldSwapchain = swapchain;
        VkSwapchainKHR replacement{};
        check(vkCreateSwapchainKHR(device, &info, nullptr, &replacement), "create swapchain");
        freeSwapchain(); swapchain = replacement; format = chosen->format; extent = next;
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "query swapchain images");
        images.resize(count);
        check(vkGetSwapchainImagesKHR(device, swapchain, &count, images.data()), "query swapchain images");
        presented.resize(count);
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto& s : presented) check(vkCreateSemaphore(device, &sem, nullptr, &s), "create present semaphore");
        return true;
    }
    void initialise() {
        if (closed) throw std::runtime_error("Cannot initialize a destroyed Vulkan window");
        if (device) return;
        uint32_t count{};
        auto extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        if (!extensions) throw std::runtime_error(SDL_GetError());
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Vulkan Poly Pusher"; app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app; ci.enabledExtensionCount = count; ci.ppEnabledExtensionNames = extensions;
        char const* validation = "VK_LAYER_KHRONOS_validation";
        if (auto required = std::getenv("VPP_REQUIRE_VALIDATION"); required && std::string(required) == "1") {
            ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = &validation;
        }
        check(vkCreateInstance(&ci, nullptr, &instance), "create Vulkan 1.3 instance");
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) throw std::runtime_error(SDL_GetError());
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate devices");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate devices");
        std::string rejected;
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(candidate, &p);
            if (p.apiVersion < VK_API_VERSION_1_3) { rejected += std::string(p.deviceName) + ": Vulkan 1.3 unavailable; "; continue; }
            uint32_t n{}; check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n, nullptr), "enumerate device extensions");
            std::vector<VkExtensionProperties> exts(n);
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &n, exts.data()), "enumerate device extensions");
            if (std::none_of(exts.begin(), exts.end(), [](auto const& e) { return !std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME); })) {
                rejected += std::string(p.deviceName) + ": VK_KHR_swapchain unavailable; "; continue;
            }
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
            std::vector<VkQueueFamilyProperties> families(n); vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, families.data());
            for (uint32_t i = 0; i < n; ++i) {
                VkBool32 supported{}; check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &supported), "query present queue");
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supported) { physical = candidate; family = i; properties = p; break; }
            }
            if (physical) break;
            rejected += std::string(p.deviceName) + ": no combined graphics/presentation queue; ";
        }
        if (!physical) throw std::runtime_error("No device supports the Vulkan clear subset: " + rejected);
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
        char const* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = &extension;
        // No optional device features are needed or enabled for transfer clears.
        check(vkCreateDevice(physical, &di, nullptr, &device), "create device");
        vkGetDeviceQueue(device, family, 0, &queue);
        for (auto& f : frames) {
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = family;
            check(vkCreateCommandPool(device, &pool, nullptr, &f.pool), "create command pool");
            VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; alloc.commandPool = f.pool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device, &alloc, &f.command), "allocate frame command");
            VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; check(vkCreateSemaphore(device, &sem, nullptr, &f.acquired), "create acquire semaphore");
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            check(vkCreateFence(device, &fence, nullptr, &f.fence), "create frame fence");
        }
        recreate();
    }
    void allocateReadback() {
        freeReadback();
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; b.size = VkDeviceSize(extent.width) * extent.height * 4; b.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        check(vkCreateBuffer(device, &b, nullptr, &readback), "create capture buffer");
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, readback, &req);
        VkPhysicalDeviceMemoryProperties mem; vkGetPhysicalDeviceMemoryProperties(physical, &mem);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) if ((req.memoryTypeBits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { type = i; break; }
        if (type == UINT32_MAX) throw std::runtime_error("Diagnostic capture requires host-visible coherent memory");
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize = req.size; a.memoryTypeIndex = type;
        check(vkAllocateMemory(device, &a, nullptr, &readbackMemory), "allocate capture memory");
        check(vkBindBufferMemory(device, readback, readbackMemory, 0), "bind capture memory");
    }
    void saveCapture() {
        void* mapped{}; check(vkMapMemory(device, readbackMemory, 0, VK_WHOLE_SIZE, 0, &mapped), "map capture");
        std::ofstream file(capturePath, std::ios::binary);
        file << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
        auto bytes = static_cast<unsigned char*>(mapped);
        for (size_t i = 0; i < size_t(extent.width) * extent.height; ++i) {
            char pixel[3] = {char(bytes[i*4 + (format == VK_FORMAT_B8G8R8A8_UNORM ? 2 : 0)]), char(bytes[i*4+1]), char(bytes[i*4 + (format == VK_FORMAT_B8G8R8A8_UNORM ? 0 : 2)])};
            file.write(pixel, 3);
        }
        vkUnmapMemory(device, readbackMemory);
        if (!file) throw std::runtime_error("Could not write diagnostic capture: " + capturePath);
        capturePath.clear(); freeReadback();
    }
    void present() {
        require();
        int w{}, h{}; SDL_GetWindowSizeInPixels(window, &w, &h);
        if (w <= 0 || h <= 0 || (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) return;
        if (!swapchain || uint32_t(w) != extent.width || uint32_t(h) != extent.height) if (!recreate()) return;
        auto& f = frames[frameIndex];
        check(vkWaitForFences(device, 1, &f.fence, VK_TRUE, UINT64_MAX), "wait for frame reuse");
        uint32_t image{};
        auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, f.acquired, {}, &image);
        for (int retry = 0; result == VK_ERROR_OUT_OF_DATE_KHR && retry < 3; ++retry) {
            if (!recreate()) return;
            result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, f.acquired, {}, &image);
        }
        bool rebuild = result == VK_SUBOPTIMAL_KHR;
        if (!rebuild) check(result, "acquire image");
        bool capturing = !capturePath.empty();
        if (capturing) allocateReadback();
        check(vkResetCommandPool(device, f.pool, 0), "reset frame command pool");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(f.command, &begin), "begin clear command");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[image]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        // Chain the acquire semaphore's TRANSFER wait into the transition.
        vkCmdPipelineBarrier(f.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkClearColorValue value{{colour.red, colour.green, colour.blue, colour.alpha}};
        vkCmdClearColorImage(f.command, images[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1, &barrier.subresourceRange);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        if (capturing) {
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(f.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {extent.width, extent.height, 1};
            vkCmdCopyImageToBuffer(f.command, images[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
            VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; host.buffer = readback; host.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(f.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 0, nullptr);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        }
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(f.command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        check(vkEndCommandBuffer(f.command), "end clear command");
        check(vkResetFences(device, 1, &f.fence), "reset frame fence");
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &f.acquired; submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &f.command; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &presented[image];
        check(vkQueueSubmit(queue, 1, &submit, f.fence), "submit clear");
        VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; p.waitSemaphoreCount = 1; p.pWaitSemaphores = &presented[image]; p.swapchainCount = 1; p.pSwapchains = &swapchain; p.pImageIndices = &image;
        result = vkQueuePresentKHR(queue, &p);
        if (capturing) { check(vkWaitForFences(device, 1, &f.fence, VK_TRUE, UINT64_MAX), "wait diagnostic capture"); saveCapture(); }
        frameIndex = (frameIndex + 1) % frames.size();
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || rebuild) recreate();
        else check(result, "present clear");
    }
};
VulkanWindow::VulkanWindow(SDL_Window* w, bool vsync) : impl(std::make_unique<Impl>()) { impl->window = w; impl->vsync = vsync; }
VulkanWindow::~VulkanWindow() { shutdown(); }
void VulkanWindow::initialise() { try { impl->initialise(); } catch (...) { impl->shutdown(); throw; } }
void VulkanWindow::shutdown() noexcept { impl->shutdown(); }
void VulkanWindow::clear(Colour const& c) { impl->require(); impl->colour = c; }
void VulkanWindow::present() {
    impl->require(); // rejected pre-initialization calls do not poison startup
    try { impl->present(); }
    catch (...) { impl->shutdown(); throw; } // never reuse a half-submitted frame
}
void VulkanWindow::capture(std::string const& p) { impl->require(); if (p.empty()) throw std::runtime_error("Empty capture path"); impl->capturePath = p; }
BackendCapabilities VulkanWindow::capabilities() const {
    impl->require(); auto version = impl->properties.apiVersion;
    return {"Vulkan", impl->properties.deviceName, VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), VK_API_VERSION_PATCH(version), true, false, "not delivered"};
}
void registerWindow(SDL_Window* w, bool vsync) { windows.emplace(w, std::make_shared<VulkanWindow>(w, vsync)); }
void unregisterWindow(SDL_Window* w) noexcept { auto it = windows.find(w); if (it != windows.end()) { it->second->shutdown(); windows.erase(it); } }
void presentWindow(SDL_Window* w) { auto it = windows.find(w); if (it == windows.end()) throw std::runtime_error("No Vulkan window association"); it->second->present(); }
std::shared_ptr<VulkanWindow> associatedWindow() {
    if (windows.size() != 1) throw std::runtime_error("Standard Vulkan startup requires exactly one live WindowSDL; create the window before RenderSystem");
    return windows.begin()->second;
}
}
