// Shared authored triangle through public Program/Material/Mesh APIs.
// The frozen GL client needs a VAO binding adapter because Mesh::bind is
// private. VPP has no such adapter: Mesh::render records native inputs.
#include "mpp/RenderSystem.h"
#include "mpp/ResourceManager.h"
#include "mpp/Program.h"
#include "mpp/BasicMaterial.h"
#include "mpp/ProgrammaticBasicMaterialStream.h"
#include "mpp/resource-parsers/FileProgramStream.h"
#include "mpp/Mesh.h"
#include "mpp/app/WindowSDL.h"
#include "mpp/app/InputManagerSDL.h"
#ifdef VPP_VULKAN
#include "mpp/BackendCapabilities.h"
#else
#include <GL/glew.h>
#endif
#include <SDL3/SDL.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace mpp;
namespace {
void require(bool value, char const* message) { if (!value) throw std::runtime_error(message); }
template<class F> void fails(char const* message, F operation) {
    try { operation(); }
    catch (std::exception const& e) {
        if (std::string(e.what()).find(message) != std::string::npos) return;
        throw;
    }
    throw std::runtime_error(std::string("Expected diagnostic: ") + message);
}
struct Vertex { float position[3], colour[4]; };
std::vector<Vertex> loadVertices(std::string const& root) {
    std::ifstream file(root + "/assets/Triangle.vertices");
    std::vector<Vertex> vertices(3);
    for (auto& v : vertices) {
        for (auto& f : v.position) file >> f;
        for (auto& f : v.colour) file >> f;
    }
    require(bool(file), "Cannot read authored Triangle.vertices");
    return vertices;
}
VertexBuffer* fillMesh(Mesh& mesh, std::vector<Vertex> const& vertices) {
    auto bytes = new int8_t[vertices.size() * sizeof(Vertex)];
    std::memcpy(bytes, vertices.data(), vertices.size() * sizeof(Vertex));
    auto buffer = mesh.createVertexBuffer(vertices.size(), sizeof(Vertex), false, false,
        std::shared_ptr<int8_t const>(bytes, [](int8_t const* p) { delete[] p; }));
    buffer->setAttribute(0, mesh::Vertex::DataType::Float, 3, 0, false);
    buffer->setAttribute(1, mesh::Vertex::DataType::Float, 4, 12, false);
    return buffer;
}
void capture(RenderSystem& renderer, WindowSDL& window, std::string const& path) {
    std::filesystem::remove(path);
#ifdef VPP_VULKAN
    captureNextPresent(renderer, path);
#else
    int w{}, h{}; SDL_GetWindowSizeInPixels(window.getWindow(), &w, &h);
    std::vector<unsigned char> bytes(size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, bytes.data());
    require(glGetError() == GL_NO_ERROR, "Reference capture GL error");
    std::ofstream file(path, std::ios::binary); file << "P6\n" << w << ' ' << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y) file.write(reinterpret_cast<char*>(bytes.data() + size_t(y) * w * 3), size_t(w) * 3);
    require(bool(file), "Reference capture write failed");
#endif
}
}
int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    int result = 0;
    try {
        auto begin = std::chrono::steady_clock::now();
        std::string path = argc > 1 ? argv[1] : "triangle.ppm";
        std::string root = std::string(VPP_SOURCE_DIR) + "/docs/comparison/m01-basics";
        InputManagerSDL input;
        WindowSDL window("MPP/VPP public triangle comparison");
        window.create(320, 240, false, false);
        window.processEvents(&input);
        Logger logger;
        RenderSystem renderer(320, 240, &logger);
        ResourceManager resources(&renderer, &logger);
#ifdef VPP_VULKAN
        std::cout << "device=" << getBackendCapabilities(renderer).device << '\n';
        auto variant = "vpp";
        fails("RenderSystem::setProjection3dPerspective", [&] { renderer.setProjection3dPerspective(60, 0.1f, 100); });
        fails("RenderSystem::renderToScreen", [&] { renderer.renderToScreen(); });
        fails("RenderSystem::popModelMatrix", [&] { renderer.popModelMatrix(); });
        fails("RenderSystem::popCameraMatrix", [&] { renderer.popCameraMatrix(); });
        require(renderer.getProjectionMatrix() == glm::mat4(1), "initial projection is not identity");
#else
        auto variant = "mpp";
        GLuint vao{}; glGenVertexArrays(1, &vao);
#endif
        auto [programResource, created] = resources.declareResource("Basics.DrawTriangle",
            std::make_shared<resource_parsers::FileProgramStream>(&resources,
                root + "/shaders/" + variant + "/DrawTriangle.program.yaml"));
        auto program = static_cast<Program*>(programResource.get());
        auto materialStream = std::make_shared<ProgrammaticBasicMaterialStream>(&resources);
        materialStream->setProgram("Basics.DrawTriangle");
        materialStream->setUniform("TINT", glm::vec4(1));
        auto [materialResource, materialCreated] = resources.declareResource("Basics.TriangleMaterial", materialStream);
        auto material = static_cast<BasicMaterial*>(materialResource.get());
        auto vertices = loadVertices(root);
        Mesh mesh(&renderer, "AuthoredTriangle", materialResource, mesh::Primitive::Type::Triangles, 1,
            mesh::VertexBufferStorageType::Dynamic);
        auto buffer = fillMesh(mesh, vertices);
#ifdef VPP_VULKAN
        fails("not loaded", [&] { mesh.render(1); });
#endif
        mesh.load();
        auto draw = [&](Mesh& geometry, float x, float scale, glm::vec4 tint, uint32_t start = 0, size_t count = 1) {
            program->bind();
            material->setUniforms();
            renderer.resetTransform();
            renderer.pushModelMatrix();
            renderer.translateTransform3d(glm::vec3(x, 0, 0));
            renderer.scaleTransform3d(glm::vec3(scale, scale, 1));
            renderer.rotateTransform3d(0, glm::vec3(0, 0, 1));
            UniformCollection uniforms;
            uniforms.setUniform("TRANSFORM", renderer.getModelCameraProjectionMatrix());
            if (tint != glm::vec4(1)) uniforms.setUniform("TINT", tint);
            uniforms.bindUniforms(programResource);
#ifndef VPP_VULKAN
            // GL-only binding adapter; uses public VertexBuffer operations.
            glBindVertexArray(vao);
            geometry.getVertexBuffer(0)->bind();
            for (uint32_t i = 0; i < geometry.getVertexBuffer(0)->getNumAttributes(); ++i)
                geometry.getVertexBuffer(0)->enableAttribute(i, true);
#endif
            geometry.render(1, start, count);
            renderer.popModelMatrix();
            require(renderer.getModelMatrix() == glm::mat4(1), "model stack did not restore identity");
        };
        renderer.pushCameraMatrix();
        renderer.setCamera3d(glm::vec3(0, 0, 0.25f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        for (int frame = 0; frame < 48; ++frame) {
            if (frame == 20) { window.setSize(400, 280); SDL_SyncWindow(window.getWindow()); }
            window.processEvents(&input);
            int w{}, h{}; SDL_GetWindowSizeInPixels(window.getWindow(), &w, &h);
            renderer.setDisplay(w, h); renderer.resetViewport();
            renderer.clearScreen(Colour(0.05f, 0.075f, 0.1f, 1));
            if (frame < 30) draw(mesh, 0, 1, glm::vec4(1));
            else {
                draw(mesh, -0.5f, 0.5f, glm::vec4(1, 0.25f, 0.25f, 1));
                draw(mesh, 0.5f, 0.5f, glm::vec4(0.25f, 1, 1, 1));
            }
            if (frame == 19 || frame == 29 || frame == 47) {
                auto output = path + (frame == 19 ? ".initial.ppm" : frame == 29 ? ".resized.ppm" : "");
                capture(renderer, window, output);
                std::cout << "capture=" << output << " frame=" << frame << " size=" << w << 'x' << h
                          << " origin=top-left encoding=RGB8-linear-UNORM scene="
                          << (frame == 47 ? "split-tinted" : "single-white-material") << '\n';
            }
            window.show();
#ifdef VPP_VULKAN
            // Updates after submit while earlier frames may still read storage.
            buffer->mapBufferData(0, 3);
            if (frame == 10 || frame == 35) {
                mesh.unload(); program->unload();
                program->load(); mesh.load();
            }
#endif
        }
        renderer.popCameraMatrix();
#ifdef VPP_VULKAN
        // Ranges and layout mismatches must fail before Vulkan command recording.
        program->bind();
        fails("instanced", [&] { mesh.render(2); });
        fails("draw range", [&] { mesh.render(1, 3, 1); });
        fails("overflow", [&] { mesh.render(1, 0, SIZE_MAX); });
        {
            Mesh wrong(&renderer, "WrongLayout", materialResource, mesh::Primitive::Type::Triangles, 1,
                mesh::VertexBufferStorageType::Static);
            auto wrongBuffer = fillMesh(wrong, vertices);
            wrongBuffer->setAttribute(2, mesh::Vertex::DataType::Float, 3, 12, false);
            wrong.load();
            fails("layout/topology", [&] { wrong.render(1); });
        }
        renderer.setCamera3d(glm::vec3(0, 0, 0.25f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        renderer.clearScreen(Colour(0.05f, 0.075f, 0.1f, 1));
        draw(mesh, 0, 1, glm::vec4(1));
        auto moved = vertices;
        for (auto& vertex : moved) vertex.position[0] += 3;
        std::memcpy(buffer->getBufferData().data(), moved.data(), moved.size() * sizeof(Vertex));
        buffer->mapBufferData(0, 3); // copy-on-write preserves the queued draw
        capture(renderer, window, path + ".queued.ppm"); window.show();
        renderer.clearScreen(Colour::Black);
        draw(mesh, 0, 1, glm::vec4(1)); // new geometry is outside the viewport
        capture(renderer, window, path + ".updated.ppm"); window.show();
        std::memcpy(buffer->getBufferData().data(), vertices.data(), vertices.size() * sizeof(Vertex));
        buffer->mapBufferData(3);
        draw(mesh, 0, 1, glm::vec4(1));
        // Clear discards pending draws and releases their pins.
        renderer.clearScreen(Colour::Black);
        capture(renderer, window, path + ".clear.ppm"); window.show();

        // Indexed draws, queued destruction and pipeline retirement before show.
        for (int width : {16, 32}) {
            std::vector<uint8_t> indices(size_t(width / 8) * 3);
            for (uint32_t i = 0; i < 3; ++i) std::memcpy(indices.data() + i * (width / 8), &i, width / 8);
            auto indexed = std::make_unique<Mesh>(&renderer, "IndexedTriangle", materialResource,
                mesh::Primitive::Type::Triangles, 1, width, indices, mesh::VertexBufferStorageType::Dynamic);
            fillMesh(*indexed, vertices); indexed->load();
            program->bind();
            auto valid = indexed->getIndexData();
            uint32_t bad = 99;
            std::memcpy(indexed->getIndexData().data(), &bad, width / 8);
            indexed->mapIndexData(1);
            indexed->getIndexData() = valid; // unuploaded CPU edits cannot mask invalid GPU indices
            fails("outside uploaded storage", [&] { indexed->render(1); });
            indexed->mapIndexData(1);
            fails("draw range", [&] { indexed->render(1, 1, 1); });
            renderer.clearScreen(Colour(0.05f, 0.075f, 0.1f, 1));
            renderer.setCamera3d(glm::vec3(0, 0, 0.25f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
            draw(*indexed, 0, 1, glm::vec4(1));
            indexed.reset(); program->unload();
            capture(renderer, window, path + ".indexed" + std::to_string(width) + ".ppm");
            window.show(); program->load();
        }
        // Retained asymmetric range units: non-indexed start is vertices,
        // indexed start is primitives. Select the second of two triangles.
        auto rangedVertices = moved;
        rangedVertices.insert(rangedVertices.end(), vertices.begin(), vertices.end());
        {
            Mesh ranged(&renderer, "RangedTriangle", materialResource, mesh::Primitive::Type::Triangles,
                2, mesh::VertexBufferStorageType::Static);
            fillMesh(ranged, rangedVertices); ranged.load();
            renderer.clearScreen(Colour(0.05f, 0.075f, 0.1f, 1));
            draw(ranged, 0, 1, glm::vec4(1), 3, 1);
            capture(renderer, window, path + ".range.ppm"); window.show();
        }
        {
            std::vector<uint8_t> indices(12);
            for (uint16_t i = 0; i < 6; ++i) std::memcpy(indices.data() + i * 2, &i, 2);
            Mesh ranged(&renderer, "RangedIndexedTriangle", materialResource, mesh::Primitive::Type::Triangles,
                2, 16, indices, mesh::VertexBufferStorageType::Static);
            fillMesh(ranged, rangedVertices); ranged.load();
            renderer.clearScreen(Colour(0.05f, 0.075f, 0.1f, 1));
            draw(ranged, 0, 1, glm::vec4(1), 1, 1);
            capture(renderer, window, path + ".range-indexed.ppm"); window.show();
        }
        // Keep the live scene and another queued draw when destroying the window.
        draw(mesh, 0, 1, glm::vec4(1)); window.show();
        draw(mesh, 0, 1, glm::vec4(1)); window.destroy();
        fails("window was destroyed", [&] { renderer.clearScreen(Colour::Black); });
#else
        glDeleteVertexArrays(1, &vao);
#endif
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        std::cout << "PASS public authored triangle, per-draw uniforms, resize and live scene lifetime; elapsed_ms=" << ms << '\n';
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; result = 1; }
    SDL_Quit(); return result;
}
