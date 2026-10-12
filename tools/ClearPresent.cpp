// Shared public-API scenario; only startup/capture/backend assertions differ.
#include "mpp/RenderSystem.h"
#include "mpp/app/WindowSDL.h"
#include "mpp/app/InputManagerSDL.h"
#ifdef VPP_VULKAN
#include "mpp/BackendCapabilities.h"
#else
#include <GL/glew.h>
#endif
#include <SDL3/SDL.h>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cmath>
using namespace mpp;
namespace {
void require(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }
void verify(std::string const& path, int w, int h, Colour const& c) {
    std::ifstream f(path, std::ios::binary);
    std::string magic; int width{}, height{}, maximum{};
    f >> magic >> width >> height >> maximum; f.get();
    if (magic != "P6" || width != w || height != h || maximum != 255)
        throw std::runtime_error("capture dimensions/format mismatch: " + path + " expected=" + std::to_string(w) + "x" + std::to_string(h) + " actual=" + std::to_string(width) + "x" + std::to_string(height));
    std::vector<unsigned char> bytes(size_t(w)*h*3); f.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    require(size_t(f.gcount()) == bytes.size(), "truncated capture");
    for (size_t i = 0; i < bytes.size(); ++i)
        require(std::abs(int(bytes[i]) - int(std::lround(c[int(i%3)]*255))) <= 1, "capture clear colour mismatch (tolerance 1/255)");
}
void capture(RenderSystem& renderer, WindowSDL& window, std::string const& path) {
#ifdef VPP_VULKAN
    captureNextPresent(renderer, path);
#else
    int w{}, h{}; SDL_GetWindowSizeInPixels(window.getWindow(), &w, &h);
    std::vector<unsigned char> bytes(size_t(w)*h*3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, bytes.data());
    require(glGetError() == GL_NO_ERROR, "reference capture GL error");
    std::ofstream f(path, std::ios::binary); f << "P6\n" << w << ' ' << h << "\n255\n";
    for (int y = h-1; y >= 0; --y) f.write(reinterpret_cast<char*>(bytes.data()+size_t(y)*w*3), size_t(w)*3);
    require(bool(f), "reference capture write failed");
#endif
}
}
int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 1; }
    int result = 0;
    try {
        std::string path = argc > 1 ? argv[1] : "clear.ppm";
        bool minimize = argc > 2 && std::string(argv[2]) == "--minimize";
        int cycles = minimize ? 1 : 3;
        Logger logger;
#ifdef VPP_VULKAN
        bool missingWindow = false;
        try { RenderSystem noWindow(320, 240, &logger); }
        catch (std::runtime_error const&) { missingWindow = true; }
        require(missingWindow, "startup without a Vulkan window must fail");
#endif
        InputManagerSDL input;
        auto start = std::chrono::steady_clock::now();
        // Repeat create/destroy. Alternate teardown order: window destruction must
        // retire in-flight work even while RenderSystem remains alive.
        for (int cycle = 0; cycle < cycles; ++cycle) {
            WindowSDL window("MPP/VPP shared clear comparison");
            window.create(320, 240, false, cycle != 1);
            SDL_Delay(200); window.processEvents(&input);
#ifdef VPP_VULKAN
            bool rejected = false;
            try { window.show(); } catch (std::runtime_error const&) { rejected = true; }
            require(rejected, "presentation before initialization must fail");
            RenderSystemOptions unsupportedOptions;
            unsupportedOptions.antiAliasing.msaa = AntiAliasingSamples::X4;
            rejected = false;
            try { RenderSystem aa(320, 240, &logger, unsupportedOptions); }
            catch (std::runtime_error const&) { rejected = true; }
            require(rejected, "unimplemented AA must not be accepted");
            if (cycle == 0) {
                WindowSDL extra;
                extra.create(160, 120, false, true);
                rejected = false;
                try { RenderSystem ambiguous(320, 240, &logger); }
                catch (std::runtime_error const&) { rejected = true; }
                require(rejected, "ambiguous window association must fail");
                extra.destroy();
            }
#endif
            auto renderer = std::make_unique<RenderSystem>(320, 240, &logger);
#ifdef VPP_VULKAN
            auto backend = getBackendCapabilities(*renderer);
            require(backend.backend == "Vulkan" && backend.apiVersionMajor >= 1 && backend.clearPresentation && !backend.shaderCompilation, "untruthful backend caps");
            require(renderer->getCaps().glVersionMajor == 0 && !renderer->getCaps().compatibilityProfile, "Vulkan claims a GL context");
            std::cout << backend.backend << ' ' << backend.apiVersionMajor << '.' << backend.apiVersionMinor << " device=" << backend.device << '\n';
            bool unsupported = false;
            try { renderer->renderText("not delivered", 0, 0, Colour::White); }
            catch (std::runtime_error const& e) { unsupported = std::string(e.what()).find("not delivered") != std::string::npos; }
            require(unsupported, "unsupported rendering must fail explicitly");
#endif
            for (int frame = 0; frame < 60; ++frame) {
                if (frame == 30) window.setSize(400, 280);
                if (minimize && frame == 40) {
                    require(SDL_MinimizeWindow(window.getWindow()), "minimize request failed");
                    require(SDL_SyncWindow(window.getWindow()), "minimize synchronization failed");
                    SDL_Delay(100); window.processEvents(&input);
                }
                if (minimize && frame == 42) {
                    require(SDL_RestoreWindow(window.getWindow()), "restore request failed");
                    require(SDL_RaiseWindow(window.getWindow()), "raise request failed");
                    SDL_SyncWindow(window.getWindow());
                    SDL_Delay(100);
                    for (int attempt = 0; attempt < 40; ++attempt) {
                        window.processEvents(&input);
                        if (!(SDL_GetWindowFlags(window.getWindow()) & SDL_WINDOW_MINIMIZED)) break;
                        SDL_RestoreWindow(window.getWindow());
                        SDL_RaiseWindow(window.getWindow());
                        SDL_Delay(50);
                    }
                    require(!(SDL_GetWindowFlags(window.getWindow()) & SDL_WINDOW_MINIMIZED), "window manager did not restore window");
                }
                window.processEvents(&input);
                int w{}, h{}; SDL_GetWindowSizeInPixels(window.getWindow(), &w, &h);
                renderer->setDisplay(w, h);
                renderer->resetViewport();
                Colour clear = frame < 30 ? Colour(0.2f, 0.4f, 0.6f, 1) : Colour(0.6f, 0.2f, 0.4f, 1);
                renderer->startStatsCollection(); renderer->clearScreen(clear); renderer->finishStatsCollection();
                std::string output = path + (frame == 29 ? ".initial.ppm" : "");
                bool capturing = frame == 29 || frame == 59;
                if (capturing) {
                    std::filesystem::remove(output); // stale output cannot hide a missed capture
                    capture(*renderer, window, output);
                }
                window.show();
                if (capturing) verify(output, w, h, clear);
                SDL_Delay(1);
            }
#ifdef VPP_VULKAN
            if (cycle % 2) {
                window.destroy();
                bool stale = false;
                try { renderer->clearScreen(Colour::White); } catch (std::runtime_error const&) { stale = true; }
                require(stale, "destroyed window must reject rendering");
            }
#endif
            renderer.reset(); window.destroy(); window.destroy();
        }
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
        std::cout << "PASS " << cycles*60 << " frames, paired clear captures, resize, " << cycles << " lifecycle cycles"
                  << (minimize ? ", minimize/restore" : "") << "; elapsed_ms=" << ms << '\n';
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; result = 1; }
    SDL_Quit(); return result;
}
