#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "mpp/Config.h"
#include "mpp/Colour.h"
#include "mpp/BackendCapabilities.h"
struct SDL_Window;
namespace mpp::detail {
class GeometryStore;
class _MPPAPI VulkanWindow {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    VulkanWindow(SDL_Window*, bool vsync);
    ~VulkanWindow();
    void initialise();
    void shutdown() noexcept;
    void clear(Colour const&);
    void present();
    void capture(std::string const&);
    BackendCapabilities capabilities() const;
    // Native geometry upload store (vertex/index buffers). Fails when the
    // renderer is not initialized or its window was destroyed.
    GeometryStore& geometry();
    // Truthful native vertex-input limits reported through Caps.
    uint32_t maxVertexInputAttributes() const;
    uint32_t maxVertexInputBindingStride() const;
};
_MPPAPI void registerWindow(SDL_Window*, bool);
_MPPAPI void unregisterWindow(SDL_Window*) noexcept;
_MPPAPI void presentWindow(SDL_Window*);
_MPPAPI std::shared_ptr<VulkanWindow> associatedWindow();
}
