#pragma once
#include <memory>
#include <string>
#include "mpp/Config.h"
#include "mpp/Colour.h"
#include "mpp/BackendCapabilities.h"
struct SDL_Window;
namespace mpp::detail {
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
};
_MPPAPI void registerWindow(SDL_Window*, bool);
_MPPAPI void unregisterWindow(SDL_Window*) noexcept;
_MPPAPI void presentWindow(SDL_Window*);
_MPPAPI std::shared_ptr<VulkanWindow> associatedWindow();
}
