#include "assets/store.hpp"
#include "scene/qualifying_view.hpp"

#include <algorithm>
#include <iostream>

namespace {
void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void reference(const char* root) {
    using namespace toca;
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private pit caption view");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA pit captions", 640, 480, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private pit caption window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(root);
    for (std::uint32_t language = 0; language < 3; ++language) {
        scene::QualifyingView view(store, renderer, language);
        for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
            gpu::check(SDL_SetWindowSize(window.get(), size[0], size[1]), "Resize pit caption target");
            gpu::check(SDL_SyncWindow(window.get()), "Synchronize pit caption target");
            SDL_PumpEvents();
            gpu::Frame entry, hidden, next, exiting;
            view.append_pit(entry, false, 659, size[0], size[1]);
            view.append_pit(hidden, false, 660, size[0], size[1]);
            view.append_pit(next, false, 1000, size[0], size[1]);
            view.append_pit(exiting, true, 660, size[0], size[1]);
            expect(!entry.vertices.empty() && hidden.vertices.empty() && next.vertices.size() == entry.vertices.size() && !exiting.vertices.empty(),
                "Original pit message must flash660ms per second while the exit hint remains visible");
            const auto bounded = [&](const auto& frame) {
                return std::ranges::all_of(frame.vertices, [&](const auto& vertex) {
                    return vertex.x >= 0 && vertex.x <= size[0] && vertex.y >= 0 && vertex.y <= size[1];
                });
            };
            expect(bounded(entry) && bounded(exiting), "Localized original pit caption exceeds its drawable");
            expect(renderer.draw(entry, false), "Private pit caption target was unavailable");
            const auto first = renderer.readback();
            expect(renderer.draw(exiting, false), "Private pit exit target was unavailable");
            const auto last = renderer.readback();
            expect(first != last, "Pit exit must select its distinct original localized caption");
            renderer.save_screenshot("pit-exit-lang" + std::to_string(language) + "-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
        }
    }
    std::cout << "PASS: original localized pit/exit captions, flash phases and three drawable aspects\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Usage: toca-pit-view-tests CANONICAL_ROOT");
        }
        reference(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
