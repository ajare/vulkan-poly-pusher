#include <stdexcept>
#include "mpp/ResourceManager.h"

// These retained stream branches require engine resource ownership, not document
// parsing. Never claim that a CPU-only resource has been realized successfully.
namespace mpp {
ResourcePtr ResourceManager::getResource(std::string const&, bool) {
    throw std::runtime_error("Resource lookup requires the not-yet-delivered Vulkan resource manager.");
}
ImageLoadFunction ResourceManager::getImageLoadFunction() {
    throw std::runtime_error("Engine image loading requires the not-yet-delivered Vulkan resource manager.");
}
void ResourceManager::warnMessage(std::string const&) {
    throw std::runtime_error("Engine resource logging requires the not-yet-delivered Vulkan resource manager.");
}
}
