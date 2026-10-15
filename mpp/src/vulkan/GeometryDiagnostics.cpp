#include "mpp/GeometryDiagnostics.h"
#include "WindowBridge.h"
#include "GeometryStore.h"

namespace mpp {
std::vector<std::string> uploadedGeometryBuffers() {
    return detail::associatedWindow()->geometry().labels();
}
std::vector<uint8_t> readbackUploadedGeometry(std::string const& label) {
    return detail::associatedWindow()->geometry().readbackLabel(label);
}
}
