// VPP geometry-upload scenario: authored meshes through the public
// Mesh/VertexBuffer APIs into native Vulkan buffers. Verifies staged upload
// contents by explicit diagnostic readback, replacement/destruction deferred
// across the two presentation frame contexts, truthful vertex-input limits,
// and explicit failure of every not-yet-delivered geometry path.
#include "mpp/RenderSystem.h"
#include "mpp/Mesh.h"
#include "mpp/GeometryDiagnostics.h"
#include "mpp/app/WindowSDL.h"
#include "mpp/app/InputManagerSDL.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace mpp;
namespace {
void require(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }

struct Vertex { float position[3]; float colour[4]; };
std::vector<Vertex> triangle() {
    return {
        {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{ 0.0f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
}
template <typename T>
std::shared_ptr<const int8_t> sharedCopy(std::vector<T> const& values) {
    auto* bytes = new int8_t[values.size() * sizeof(T)];
    std::memcpy(bytes, values.data(), values.size() * sizeof(T));
    return {bytes, [](int8_t const* p) { delete[] p; }};
}
template <typename T>
std::vector<uint8_t> raw(std::vector<T> const& values) {
    std::vector<uint8_t> bytes(values.size() * sizeof(T));
    std::memcpy(bytes.data(), values.data(), bytes.size());
    return bytes;
}
template <typename T>
std::vector<int8_t> cpuBytes(std::vector<T> const& values) {
    auto bytes = raw(values);
    return {bytes.begin(), bytes.end()};
}
void requireUpload(std::string const& label, std::vector<uint8_t> const& expected) {
    auto actual = readbackUploadedGeometry(label);
    if (actual != expected)
        throw std::runtime_error("uploaded contents mismatch for '" + label + "': expected " +
            std::to_string(expected.size()) + " bytes, read back " + std::to_string(actual.size()) +
            (actual.size() == expected.size() ? " (contents differ)" : ""));
}
template <typename F>
void requireFails(std::string const& diagnostic, std::string const& needle, F operation) {
    try { operation(); }
    catch (std::exception const& error) {
        if (std::string(error.what()).find(needle) == std::string::npos)
            throw std::runtime_error("diagnostic '" + diagnostic + "' lacks '" + needle + "': " + error.what());
        return;
    }
    throw std::runtime_error("expected failure did not occur: " + diagnostic);
}
}
int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    int result = 0;
    try {
        auto start = std::chrono::steady_clock::now();
        InputManagerSDL input;
        WindowSDL window("VPP geometry upload");
        window.create(320, 240, false, false);
        window.processEvents(&input);
        RenderSystem renderer(320, 240, nullptr);

        // Truthful native vertex-input limits; every other capability stays off.
        auto const& caps = renderer.getCaps();
        require(caps.maxVertexAttributes >= 16 && caps.maxVertexAttributeStride > 0, "native vertex-input limits not reported");
        require(caps.glVersionMajor == 0 && !caps.streamingGeometry, "untruthful geometry capabilities");

        auto frame = [&](int count) {
            for (int i = 0; i < count; ++i) {
                window.processEvents(&input);
                renderer.clearScreen(Colour(0.1f, 0.1f, 0.2f, 1));
                window.show();
                SDL_Delay(1);
            }
        };

        // Authored indexed triangle: position3 float + colour4 float, stride 28.
        auto vertices = triangle();
        std::vector<uint32_t> indices{0, 1, 2};
        Mesh mesh(&renderer, "SharedTriangle", nullptr, mesh::Primitive::Type::Triangles, 1,
            32, raw(indices), mesh::VertexBufferStorageType::Static);
        auto* buffer = mesh.createVertexBuffer(vertices.size(), sizeof(Vertex), false, true, sharedCopy(vertices));
        buffer->setAttribute(0, mesh::Vertex::DataType::Float, 3, 0, false);
        buffer->setAttribute(1, mesh::Vertex::DataType::Float, 4, 12, false);
        requireFails("map before load", "not loaded", [&] { buffer->mapBufferData(3); });
        mesh.load();
        require(mesh.getIdCount() == 2 && mesh.getLiveIdCount() == 2, "engine ID counts wrong after load");
        require(mesh.getVertexSize() == sizeof(Vertex), "vertex size wrong");
        requireUpload("Mesh 'SharedTriangle' vertex buffer 0", raw(vertices));
        requireUpload("Mesh 'SharedTriangle' index buffer", raw(indices));
        auto labels = uploadedGeometryBuffers();
        require(labels.size() == 2, "diagnostic labels missing");

        // Full staged re-upload with modified contents.
        vertices[1].colour[0] = 0.25f;
        buffer->getBufferData() = cpuBytes(vertices);
        buffer->mapBufferData(vertices.size());
        requireUpload("Mesh 'SharedTriangle' vertex buffer 0", raw(vertices));

        // Range staged re-upload of a single vertex.
        vertices[2].position[0] = 0.125f;
        auto bytes = raw(vertices);
        auto& cpu = buffer->getBufferData();
        std::memcpy(cpu.data() + 2 * sizeof(Vertex), bytes.data() + 2 * sizeof(Vertex), sizeof(Vertex));
        buffer->mapBufferData(2, 1);
        requireUpload("Mesh 'SharedTriangle' vertex buffer 0", bytes);

        // Full and ranged index re-uploads (reverse winding). The vector
        // overload exercises the intended byte-width conversion (BD-4).
        indices = {2, 1, 0};
        mesh.setIndexData(raw(indices), 32);
        mesh.mapIndexData(1);
        requireUpload("Mesh 'SharedTriangle' index buffer", raw(indices));
        indices = {0, 2, 1};
        mesh.setIndexData(reinterpret_cast<int8_t const*>(indices.data()), 3, 32);
        mesh.mapIndexData(0, 1);
        requireUpload("Mesh 'SharedTriangle' index buffer", raw(indices));

        // In-flight replacement and destruction across the frame contexts:
        // uploads, growth replacements, unload/load cycles and whole-mesh
        // destruction interleave with live presentation; validation must stay
        // silent and readbacks must observe the newest contents.
        auto* dynamicMesh = new Mesh(&renderer, "DynamicFan", nullptr, mesh::Primitive::Type::Triangles, 1,
            16, raw(std::vector<uint16_t>{0, 1, 2}), mesh::VertexBufferStorageType::Dynamic);
        auto* dynamicBuffer = dynamicMesh->createVertexBuffer(vertices.size(), sizeof(Vertex), true, false, sharedCopy(vertices));
        dynamicBuffer->setAttribute(0, mesh::Vertex::DataType::Float, 3, 0, false);
        dynamicBuffer->setAttribute(1, mesh::Vertex::DataType::UnsignedByte, 4, 12, true);
        dynamicMesh->load();
        require(!dynamicMesh->usesPersistentIndexMapping() && !dynamicBuffer->usesPersistentMapping(),
            "persistent mapping must not be claimed");
        frame(3);
        for (int step = 0; step < 12; ++step) {
            vertices[0].colour[1] = float(step) / 16;
            dynamicBuffer->getBufferData() = cpuBytes(vertices);
            dynamicBuffer->mapBufferData(vertices.size());
            if (step == 4) { // grow beyond capacity: native storage is replaced
                vertices.push_back({{1.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}});
                dynamicBuffer->getBufferData() = cpuBytes(vertices);
                dynamicBuffer->mapBufferData(vertices.size());
                dynamicMesh->getIndexData() = raw(std::vector<uint16_t>{0, 1, 2, 0, 2, 3});
                dynamicMesh->mapIndexData(2);
                dynamicMesh->setNumPrimitives(2);
            }
            if (step == 7) dynamicMesh->setStorageType(mesh::VertexBufferStorageType::Static); // unload+load
            frame(2);
        }
        requireUpload("Mesh 'DynamicFan' vertex buffer 0", raw(vertices));
        requireUpload("Mesh 'DynamicFan' index buffer", raw(std::vector<uint16_t>{0, 1, 2, 0, 2, 3}));
        require(dynamicMesh->getLiveIdCount() == 2, "live IDs wrong after replacements");
        delete dynamicMesh; // destruction defers past in-flight frames
        frame(4);
        requireFails("readback of destroyed buffer", "No live uploaded geometry",
            [] { readbackUploadedGeometry("Mesh 'DynamicFan' index buffer"); });

        // Attribute layouts the reference accepts are validated at load.
        Mesh halfMesh(&renderer, "HalfLayout", nullptr, mesh::Primitive::Type::Points, 1,
            mesh::VertexBufferStorageType::Static);
        std::vector<uint8_t> halfData(3 * 16, 0xA5);
        auto* halfBuffer = halfMesh.createVertexBuffer(3, 16, false, true,
            std::shared_ptr<const int8_t>(new int8_t[halfData.size()], [](int8_t const* p) { delete[] p; }));
        std::memcpy(halfBuffer->getBufferData().data(), halfData.data(), halfData.size());
        halfBuffer->setAttribute(0, mesh::Vertex::DataType::HalfFloat, 2, 0, false);
        halfBuffer->setAttribute(1, mesh::Vertex::DataType::UnsignedByte, 4, 4, true);
        halfBuffer->setAttribute(2, mesh::Vertex::DataType::Float, 2, 8, false);
        halfMesh.load();
        requireUpload("Mesh 'HalfLayout' vertex buffer 0", halfData);

        // Unsupported and mismatched layouts fail actionably.
        requireFails("negative attribute offset", "negative",
            [&] { halfBuffer->setAttribute(3, mesh::Vertex::DataType::Float, 1, -1, false); });
        requireFails("attribute beyond stride", "beyond the vertex stride",
            [&] { halfBuffer->setAttribute(3, mesh::Vertex::DataType::Float, 4, 4, false); });
        Mesh badMesh(&renderer, "BadLayout", nullptr, mesh::Primitive::Type::Points, 1,
            mesh::VertexBufferStorageType::Static);
        auto* badBuffer = badMesh.createVertexBuffer(1, 12, false, true, sharedCopy(std::vector<uint8_t>(12, 0)));
        badBuffer->setAttribute(0, mesh::Vertex::DataType::Int, 3, 0, true);
        requireFails("normalized 32-bit attribute at load", "Normalized 32-bit", [&] { badMesh.load(); });
        Mesh doubleMesh(&renderer, "DoubleLayout", nullptr, mesh::Primitive::Type::Points, 1,
            mesh::VertexBufferStorageType::Static);
        auto* doubleBuffer = doubleMesh.createVertexBuffer(1, 24, false, true, sharedCopy(std::vector<uint8_t>(24, 0)));
        doubleBuffer->setAttribute(0, mesh::Vertex::DataType::Double, 3, 0, false);
        requireFails("double attribute at load", "Double-precision", [&] { doubleMesh.load(); });
        Mesh badIndex(&renderer, "BadIndexWidth", nullptr, mesh::Primitive::Type::Triangles, 1,
            24, std::vector<uint8_t>(9, 0), mesh::VertexBufferStorageType::Static);
        requireFails("unsupported index width", "24 bits", [&] { badIndex.load(); });

        // Every not-yet-delivered geometry path fails explicitly.
        requireFails("mesh drawing without a program", "no program selected", [&] { mesh.render(1); });
        requireFails("buffer bind", "not delivered", [&] { buffer->bind(); });
        requireFails("buffer unbind", "not delivered", [&] { buffer->unbind(); });
        requireFails("GL attribute state", "not delivered", [&] { buffer->enableAttribute(0, true); });
        requireFails("unknown readback label", "No live uploaded geometry",
            [] { readbackUploadedGeometry("Mesh 'Missing' index buffer"); });

        // Mesh destruction while frames are in flight; renderer and window
        // teardown retires everything without validation diagnostics.
        mesh.unload();
        require(mesh.getLiveIdCount() == 0, "live IDs after unload");
        mesh.load();
        require(mesh.getLiveIdCount() == 2, "live IDs after reload");
        frame(6);
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "PASS staged uploads verified by readback, range/full/growth updates, "
                     "deferred replacement/destruction across frame contexts, explicit subset failures; elapsed_ms=" << ms << '\n';
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; result = 1; }
    SDL_Quit();
    return result;
}
