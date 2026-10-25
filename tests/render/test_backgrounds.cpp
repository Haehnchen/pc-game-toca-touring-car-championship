#include "assets/store.hpp"
#include "core/crc32.hpp"
#include "formats/cm3d.hpp"
#include "image/decode.hpp"
#include "scene/catalog.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/view.hpp"
#include "scene/weather_render.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <set>
#include <sstream>

namespace {
using namespace toca::scene;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string checksum(std::span<const std::uint8_t> bytes) {
    std::ostringstream text;
    text << std::hex << std::setw(8) << std::setfill('0') << toca::crc32(bytes);
    return text.str();
}

std::set<std::string> verified_images;
std::set<std::string> strong_wrap_images;
std::string selected_material;
void storm(toca::gpu::Renderer& renderer, SDL_Window* window, const toca::AssetStore& store, const SceneResources& original, const SceneResources& mega);

std::array<double, 2> wrap_contrast(SDL_Surface* source) {
    toca::gpu::Surface rgba(SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    expect(bool(rgba), "Cannot inspect sky wrap pixels");
    double maximum = 0, total = 0;
    const auto rows = rgba->h * 7 / 10;
    for (int y = 0; y < rows; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(rgba->pixels) + y * rgba->pitch;
        unsigned delta = 0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            delta += unsigned(std::abs(int(row[axis]) - int(row[(rgba->w - 1) * 4 + axis])));
        }
        maximum = std::max(maximum, double(delta));
        total += delta;
    }
    return {maximum, total / rows};
}

void verify(const toca::Model& source, const toca::Model& mega, const std::filesystem::path& directory, bool required = true) {
    expect(source.nodes.size() == mega.nodes.size() && source.meshes.size() == mega.meshes.size() && source.textures.size() == mega.textures.size(),
        "Background artwork must preserve original nodes, meshes and material slots");
    for (std::size_t i = 0; i < source.nodes.size(); ++i) {
        const auto& a = source.nodes[i];
        const auto& b = mega.nodes[i];
        expect(a.parent == b.parent && a.transform == b.transform && a.id_flags == b.id_flags && a.variants == b.variants, "Background node identity changed");
    }
    for (std::size_t i = 0; i < source.meshes.size(); ++i) {
        const auto& a = source.meshes[i];
        const auto& b = mega.meshes[i];
        expect(a.name == b.name && a.vertices.size() == b.vertices.size() && a.faces.size() == b.faces.size(), "Background geometry changed");
        for (std::size_t j = 0; j < a.vertices.size(); ++j) {
            expect(a.vertices[j].position == b.vertices[j].position && a.vertices[j].normal == b.vertices[j].normal && a.vertices[j].u == b.vertices[j].u
                       && a.vertices[j].v == b.vertices[j].v,
                "Background positions and cap/wrap UVs must remain original");
        }
        for (std::size_t j = 0; j < a.faces.size(); ++j) {
            expect(
                a.faces[j].indices == b.faces[j].indices && a.faces[j].texture == b.faces[j].texture && a.faces[j].color == b.faces[j].color, "Background face material changed");
        }
    }
    for (std::size_t i = 0; i < source.textures.size(); ++i) {
        const auto& before = source.textures[i];
        const auto& after = mega.textures[i];
        const auto path = directory / "scene" / (checksum(before.bmp) + ".webp");
        expect(before.name == after.name, "Original background material slot changed");
        const bool selected = selected_material.empty() || before.name == selected_material;
        if ((!required || !selected) && !std::filesystem::exists(path)) {
            expect(after.encoding == toca::ImageEncoding::bmp && before.bmp == after.bmp, "Unfinished background must retain its exact original BMP fallback");
            std::cout << "ORIGINAL " << before.name << " source_crc32=" << checksum(before.bmp) << '\n';
            continue;
        }
        expect(after.encoding == toca::ImageEncoding::webp, "Background must load the exact authored WebP, without fallback");
        std::ifstream file(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        expect(!bytes.empty() && bytes == after.bmp, "Loaded background bytes differ from the exact source-CRC32 bundle path");
        const auto decoded = toca::decode_model_texture(after);
        expect(decoded->w == 512 && decoded->h == 512, "Authored background must be 512 square");
        const auto source_pixels = toca::decode_model_texture(before);
        const auto source_wrap = wrap_contrast(source_pixels.get()), target_wrap = wrap_contrast(decoded.get());
        if (selected) {
            if (target_wrap[0] > std::max(240., source_wrap[0] + 130.) || target_wrap[1] > std::max(20., source_wrap[1] + 10.)) {
                std::cerr << "WRAP " << before.name << " source_mean=" << source_wrap[1] << " authored_mean=" << target_wrap[1] << '\n';
                // Keep the gate, but capture native views before reporting it.
                strong_wrap_images.insert(before.name);
            }
        }
        expect(bytes.size() <= 256 * 1024, "Authored sky exceeds its256KiB material budget");
        expect(decoded->format == SDL_PIXELFORMAT_RGBA32, "WebP validation needs decoded RGBA pixels");
        std::size_t black = 0;
        for (int y = 0; y < decoded->h; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(decoded->pixels) + y * decoded->pitch;
            for (int x = 0; x < decoded->w; ++x) {
                expect(row[x * 4 + 3] == 255, "Sky pixels, including black Extras, must remain fully opaque");
                black += row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2] < 6;
            }
        }
        if (before.name == "SILLYLAN") {
            expect(black > 1000, "Star Extras must retain an opaque black night background");
        }
        if ((selected_material.empty() || before.name == selected_material) && verified_images.insert(before.name).second) {
            std::cout << "LOAD " << before.name << ' ' << path.string() << " output_crc32=" << checksum(bytes) << " bytes=" << bytes.size() << " wrap_max=" << target_wrap[0]
                      << " wrap_mean=" << target_wrap[1] << '\n';
        }
    }
}

CameraInputs camera(float angle, unsigned pitch, unsigned width, unsigned height) {
    const auto radians = angle * std::numbers::pi_v<float> / 4.f + .03f;
    const auto elevation = pitch == 2 ? 3.f : pitch ? .8f : 0.f;
    const std::array<std::int32_t, 3> target{std::int32_t(std::sin(radians) * 65536.f), std::int32_t(elevation * 65536.f), std::int32_t(std::cos(radians) * 65536.f)};
    std::array<SceneCamera, 1> cameras{};
    return {FinishCamera(cameras).world_to_view(target), default_projection(4, width, height)};
}

std::vector<Uint32> render(toca::gpu::Renderer& renderer, const toca::gpu::Frame& scene, unsigned width, unsigned height, const std::string& filename = {}) {
    constexpr std::uint32_t uncovered = 0xfffd02f1;
    toca::gpu::Frame frame;
    frame.quad({.depth_write = false}, 0, 0, float(width), float(height), uncovered);
    frame.append_view(scene, {0, 0, width, height});
    std::vector<Uint32> pixels;
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        SDL_PumpEvents();
        expect(renderer.draw(frame, false), "Background GPU target unavailable");
        pixels = renderer.readback();
        if (pixels.size() == width * height) {
            break;
        }
        SDL_Delay(5);
    }
    expect(pixels.size() == width * height, "Background swapchain did not reach requested size");
    if (!filename.empty()) {
        renderer.save_screenshot(filename);
    }
    return pixels;
}

unsigned difference(std::uint32_t a, std::uint32_t b) {
    unsigned result = 0;
    for (const auto shift : {0u, 8u, 16u}) {
        result += unsigned(std::abs(int((a >> shift) & 255) - int((b >> shift) & 255)));
    }
    return result;
}

void coverage(const std::vector<Uint32>& original, const std::vector<Uint32>& mega, unsigned width, unsigned height) {
    constexpr std::uint32_t uncovered = 0xfffd02f1;
    std::size_t holes = 0, covered = 0;
    for (std::size_t index = 0; index < original.size(); ++index) {
        covered += original[index] != uncovered;
        holes += original[index] != uncovered && mega[index] == uncovered;
    }
    expect(covered > width * height / 4 && holes == 0, "Mega sky introduced an uncovered seam, cap hole or strip");
    // A heading just beside the wrap boundary exposes a vertical seam near
    // center. Compare its upper-sky jump against the actual original seam.
    unsigned source_jump = 0, mega_jump = 0;
    for (unsigned y = height / 10; y < height / 3; ++y) {
        const auto index = y * width + width / 2;
        source_jump = std::max(source_jump, difference(original[index - 2], original[index + 2]));
        mega_jump = std::max(mega_jump, difference(mega[index - 2], mega[index + 2]));
    }
    std::cout << "UV covered_pixels=" << covered << " holes=" << holes << " source_center_gradient=" << source_jump << " mega_center_gradient=" << mega_jump << '\n';
}

void pictures(toca::gpu::Renderer& renderer, SDL_Window* window, const SceneResources& original, const SceneResources& mega, bool pilot) {
    const auto name = original.horizon.textures.front().name;
    SceneResources before{}, after{};
    before.setup = after.setup = mega.setup;
    before.horizon = original.horizon;
    after.horizon = mega.horizon;

    SceneView source_view(renderer, before);

    SceneView mega_view(renderer, after);

    std::size_t encoded = 0;
    for (const auto& texture : mega.horizon.textures) {
        encoded += texture.bmp.size();
    }
    const auto rgba_bytes = mega.horizon.textures.size() * 512 * 512 * 4;
    expect(rgba_bytes <= 2 * 1024 * 1024 && encoded <= 2 * 256 * 1024, "Selected sky exceeded its two-material Mega memory budget");
    std::ofstream budget("background-budget.csv", std::ios::app);
    budget << mega.setup.selection.track_id << ',' << name << ',' << mega.horizon.textures.size() << ',' << encoded << ',' << rgba_bytes << '\n';
    std::cout << "BUDGET " << name << " resident_sky_materials=" << mega.horizon.textures.size() << " encoded_bytes=" << encoded << " rgba8_bytes=" << rgba_bytes << '\n';
    const std::array<std::array<unsigned, 2>, 3> sizes{{{640, 480}, {960, 540}, {960, 600}}};
    const bool special = name == "LAVABAK" || name == "SILLYLAN" || name == "CARTOON";
    for (unsigned aspect = 0; aspect < (pilot || special ? sizes.size() : 1); ++aspect) {
        const auto [width, height] = sizes[aspect];
        toca::gpu::check(SDL_SetWindowSize(window, int(width), int(height)), "Resize background render target");
        SDL_PumpEvents();
        for (unsigned angle = 0; angle < (pilot ? 10u : 4u); ++angle) {
            // The half-sector heading brings atlas U=.5 into the center,
            // exposing an interior cut that a wrap-only view can hide.
            const auto heading = pilot ? float(angle % 8) : angle == 3 ? .5f : 0.f;
            const auto pitch = pilot ? (angle < 8 ? 0u : angle == 8 ? 1u : 2u) : angle == 3 ? 0u : angle;
            const auto inputs = camera(heading, pitch, width, height);
            const auto draw = [&](SceneView& view, const char* label) {
                const auto frame = view.frame(inputs, scene_lighting(mega.setup), {unit}, {}, {});
                expect(toca::test::materialize_world(frame).indices.size() >= 3, "Background must produce visible dome triangles");
                return render(
                    renderer, frame, width, height, name + "-" + std::to_string(width) + "x" + std::to_string(height) + "-" + std::to_string(angle) + "-" + label + ".png");
            };
            const auto a = draw(source_view, "original"), b = draw(mega_view, "mega");
            expect(a.size() == b.size() && b.size() == width * height, "Background readback size mismatch");
            std::size_t changes = 0;
            for (std::size_t i = 0; i < a.size(); ++i) {
                changes += a[i] != b[i];
            }
            expect(changes > 1000, "Authored background must visibly replace original pixels in every view");
            coverage(a, b, width, height);
            std::cout << "PASS " << name << ' ' << width << 'x' << height << " angle=" << angle << " changed_pixels=" << changes << '\n';
        }
    }
}

void material(toca::gpu::Renderer& renderer, SDL_Window* window, const toca::AssetStore& store, const std::filesystem::path& directory) {
    for (std::uint32_t track = 0; track < 10; ++track) {
        std::vector<SceneSetup> setups;
        for (std::uint32_t weather = 1; weather <= 5; ++weather) {
            for (const auto choice : {1u, 2u}) {
                setups.push_back(resolve_setup({.track_id = track, .weather_choice = weather, .detail = 3}, choice));
            }
        }
        setups.push_back(resolve_setup({.track_id = track, .detail = 3, .stars = true}));
        setups.push_back(resolve_setup({.track_id = track, .detail = 3, .toon = true}));
        if (track == 6) {
            setups.push_back(resolve_setup({.track_id = track, .detail = 3, .championship_meeting = 6}));
        }
        const auto archive = store.open_archive(std::string(track_content(track).archive_prefix) + "hor.bfp");
        for (const auto& setup : setups) {
            const auto exact = toca::parse_cm3d(archive.member(setup.horizon_index + 2).bytes());
            if (!std::ranges::any_of(exact.textures, [&](const auto& texture) {
                    return texture.name == selected_material;
                })) {
                continue;
            }

            const auto original = load_scene_resources(store, setup);

            auto mega = original;
            mega.setup.selection.detail = 4;
            apply_mega_scene_resources(mega, directory);

            verify(original.horizon, mega.horizon, directory);
            pictures(renderer, window, original, mega, false);
            if (selected_material.ends_with("05L")) {
                storm(renderer, window, store, original, mega);
            }
            return;
        }
    }
    throw std::runtime_error("Selected material has no reachable original sky consumer");
}

toca::ModelTexture solid(std::string name, std::array<std::uint8_t, 3> rgb) {
    std::vector<std::uint8_t> bmp(58);
    const auto word = [&](unsigned offset, std::uint32_t value, unsigned size = 4) {
        for (unsigned byte = 0; byte < size; ++byte) {
            bmp[offset + byte] = std::uint8_t(value >> (byte * 8));
        }
    };
    bmp[0] = 'B';
    bmp[1] = 'M';
    word(2, 58);
    word(10, 54);
    word(14, 40);
    word(18, 1);
    word(22, 1);
    word(26, 1, 2);
    word(28, 24, 2);
    bmp[54] = rgb[2];
    bmp[55] = rgb[1];
    bmp[56] = rgb[0];
    return {std::move(name), std::move(bmp)};
}

void storm(toca::gpu::Renderer& renderer, SDL_Window* window, const toca::AssetStore& store, const SceneResources& original, const SceneResources& mega) {
    toca::gpu::check(SDL_SetWindowSize(window, 640, 480), "Resize storm probe target");
    const auto weather = load_weather_resources(store);
    SceneResources source{}, detailed{}, mask{};
    source.setup = detailed.setup = mask.setup = mega.setup;
    source.horizon = original.horizon;
    detailed.horizon = mega.horizon;
    mask.horizon = original.horizon;
    expect(mask.horizon.textures.size() == 2 && mask.horizon.nodes.size() == 3, "Storm must preserve both original node and material slots");
    mask.horizon.textures[0] = solid("ordinary", {20, 180, 20});
    mask.horizon.textures[1] = solid("bolt", {240, 10, 10});
    mask.horizon.textures.push_back(solid("original-uv-delta", {10, 10, 240}));
    const auto signature = [](const toca::ModelMesh& mesh, const toca::ModelFace& face) {
        std::array<std::array<float, 3>, 3> positions;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            positions[corner] = mesh.vertices[face.indices[corner]].position;
        }
        std::ranges::sort(positions);
        return positions;
    };
    std::size_t uv_triangles = 0;
    for (auto& face : mask.horizon.meshes[1].faces) {
        if (face.texture != 0) {
            continue;
        }
        const auto& ordinary = original.horizon.meshes[0];
        const auto& lit = original.horizon.meshes[1];
        const auto counterpart = std::ranges::find_if(ordinary.faces, [&](const auto& candidate) {
            return signature(ordinary, candidate) == signature(lit, face);
        });
        expect(counterpart != ordinary.faces.end(), "Storm meshes changed physical panel geometry");
        bool changed = false;
        for (const auto index : face.indices) {
            const auto& vertex = lit.vertices[index];
            for (const auto before : counterpart->indices) {
                if (ordinary.vertices[before].position == vertex.position) {
                    changed |= ordinary.vertices[before].u != vertex.u || ordinary.vertices[before].v != vertex.v;
                }
            }
        }
        if (changed) {
            face.texture = 2;
            ++uv_triangles;
        }
    }
    expect(uv_triangles <= 1, "Canonical storm contains at most one ordinary triangle with different source UVs");
    SceneView source_view(renderer, source, &weather), mega_view(renderer, detailed, &weather), mask_view(renderer, mask, &weather);
    const std::array<std::array<unsigned, 2>, 3> sizes{{{640, 480}, {960, 540}, {960, 600}}};
    for (const auto [width, height] : sizes) {
        toca::gpu::check(SDL_SetWindowSize(window, int(width), int(height)), "Resize storm aspect target");
        bool bolt_seen = false;
        for (unsigned heading = 0; heading < 8; ++heading) {
            const auto inputs = camera(heading, 0, width, height);
            const auto raw_frame = [&](SceneView& view, std::uint8_t alpha, bool overlay) {
                WeatherFrame flash;
                flash.lightning_alpha = alpha;
                auto frame = view.frame(inputs, scene_lighting(mega.setup), {unit}, {}, {}, {}, {}, &flash);
                if (alpha && !overlay) {
                    expect(!frame.draws.empty() && frame.draws.back().state.blend && frame.draws.back().state.texture == nullptr,
                        "Original weather flash must remain the final white alpha overlay");
                    frame.draws.pop_back();
                }
                return frame;
            };
            const auto dark = render(renderer, raw_frame(source_view, 0, false), width, height);
            const auto lit = render(renderer, raw_frame(source_view, 1, false), width, height);
            const auto panels = render(renderer, raw_frame(mask_view, 1, false), width, height);
            const auto mega_dark = render(renderer, raw_frame(mega_view, 0, false), width, height);
            const auto mega_lit = render(renderer, raw_frame(mega_view, 1, false), width, height);
            std::size_t changes = 0, outside = 0, mega_outside = 0, bolt_pixels = 0, uv_pixels = 0;
            for (std::size_t index = 0; index < dark.size(); ++index) {
                const auto red = (panels[index] >> 16) & 255, green = (panels[index] >> 8) & 255, blue = panels[index] & 255;
                const bool panel = red > green && red > blue + 50;
                const bool uv_delta = blue > green && blue > red + 50;
                bolt_pixels += panel;
                uv_pixels += uv_delta;
                changes += dark[index] != lit[index];
                outside += !(red > green || blue > green) && dark[index] != lit[index];
                mega_outside += !(red > green || blue > green) && mega_dark[index] != mega_lit[index];
            }
            expect(outside < 16, "Storm node switching changed pixels outside original05L panels and the documented source UV-delta triangle");
            expect(mega_outside < 16, "Authored storm node switching changed pixels outside original05L panels and the source UV-delta triangle");
            if (!bolt_seen && bolt_pixels > 1000 && changes > 100) {
                const auto name = original.horizon.textures[1].name + (width == 640 ? std::string{} : "-" + std::to_string(width) + "x" + std::to_string(height));
                render(renderer, raw_frame(source_view, 0, false), width, height, name + "-dark-original.png");
                render(renderer, raw_frame(source_view, 1, false), width, height, name + "-bolt-original.png");
                render(renderer, raw_frame(mega_view, 0, false), width, height, name + "-dark-mega.png");
                render(renderer, raw_frame(mega_view, 1, false), width, height, name + "-bolt-mega.png");
                render(renderer, raw_frame(mega_view, 255, true), width, height, name + "-weather-flash.png");
                const auto flash = render(renderer, raw_frame(mega_view, 255, true), width, height);
                expect(std::ranges::all_of(flash,
                           [](auto pixel) {
                               return (pixel & 0xffffff) == 0xffffff;
                           }),
                    "Full weather flash must still cover the entire scene after the lit storm sky");
                expect(mega_dark != mega_lit, "Authored05L atlas must visibly alter the original lightning panels");
                std::cout << "STORM " << name << " heading=" << heading << " source_changed_pixels=" << changes << " panel_pixels=" << bolt_pixels
                          << " source_uv_triangles=" << uv_triangles << " uv_delta_pixels=" << uv_pixels << " outside=" << outside << " mega_outside=" << mega_outside << '\n';
                bolt_seen = true;
            }
        }
        expect(bolt_seen, "Eight storm headings did not expose the original localized lightning artwork");
    }
}

void inactive(const toca::AssetStore& store, std::uint32_t track, const std::filesystem::path& directory) {
    if (track != 2 && track != 3 && track != 8) {
        return;
    }
    const auto member = track == 3 ? 4u : 3u;
    const auto archive = store.open_archive(std::string(track_content(track).archive_prefix) + "hor.bfp");
    const auto source = toca::parse_cm3d(archive.member(member).bytes());
    const auto expected = track == 2 ? "TH02" : track == 3 ? "BH03" : "SN02";
    expect(source.textures.size() == 1 && source.textures[0].name == expected, "Inactive canonical cloudy atlas disappeared from its source member");
    for (std::uint32_t weather = 0; weather <= 5; ++weather) {
        for (const auto variant : {1u, 2u}) {
            expect(
                resolve_setup({.track_id = track, .weather_choice = weather}, variant).horizon_index + 2 != member, "Inactive cloudy image became selected by normal game weather");
        }
    }
    SceneResources isolated;
    isolated.setup = resolve_setup({.track_id = track, .weather_choice = 1, .detail = 4});
    isolated.horizon = source;
    apply_mega_scene_resources(isolated, directory);
    verify(source, isolated.horizon, directory, false);
}

void track_package(toca::gpu::Renderer& renderer, SDL_Window* window, const toca::AssetStore& store, const std::filesystem::path& directory, std::uint32_t track, bool all) {
    std::set<std::uint32_t> captured;
    std::vector<std::uint8_t> rain;
    for (std::uint32_t weather = 0; weather <= 5; ++weather) {
        for (const auto variant : {1u, 2u}) {
            if (variant == 2 && !(weather == 2 && (track == 1 || track == 4 || track == 5 || track == 7))) {
                continue;
            }
            const auto setup = resolve_setup({.track_id = track, .weather_choice = weather, .detail = 3}, variant);
            const auto original = load_scene_resources(store, setup);
            const auto archive = store.open_archive(std::string(track_content(track).archive_prefix) + "hor.bfp");
            const auto exact = toca::parse_cm3d(archive.member(setup.horizon_index + 2).bytes());
            expect(original.horizon.textures.front().bmp == exact.textures.front().bmp, "Resolved horizon did not load its exact original weather member");
            auto mega = original;
            mega.setup.selection.detail = 4;
            apply_mega_scene_resources(mega, directory);
            verify(original.horizon, mega.horizon, directory);
            if (track != 9 && weather == 3) {
                rain = mega.horizon.textures.front().bmp;
            }
            if (track != 9 && weather == 4) {
                expect(rain == mega.horizon.textures.front().bmp, "Rain and snow must share the identical authored04 image");
            }
            if (captured.insert(setup.horizon_index).second) {
                pictures(renderer, window, original, mega, false);
                if (setup.horizon_index == 4) {
                    storm(renderer, window, store, original, mega);
                }
            }
        }
    }
    if (track == 6) {
        const auto original = load_scene_resources(store, resolve_setup({.track_id = 6, .detail = 3, .championship_meeting = 6}));
        auto mega = original;
        mega.setup.selection.detail = 4;
        apply_mega_scene_resources(mega, directory);
        expect(original.horizon.textures.front().name == "CR02", "Championship meeting6 must reach Croft's first cloudy variant");
        verify(original.horizon, mega.horizon, directory);
        pictures(renderer, window, original, mega, false);
    }
    for (const auto stars : {true, false}) {
        const auto original = load_scene_resources(store, resolve_setup({.track_id = track, .detail = 3, .stars = stars, .toon = true}));
        auto mega = original;
        mega.setup.selection.detail = 4;
        apply_mega_scene_resources(mega, directory);
        verify(original.horizon, mega.horizon, directory, all || track == 9);
        if (all && track == 0) {
            pictures(renderer, window, original, mega, false);
        }
    }
    for (unsigned effect = 0; effect < 3; ++effect) {
        const auto original = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 5, .detail = 3, .rain_up = effect != 1, .cat_dog = effect != 0}));
        auto mega = original;
        mega.setup.selection.detail = 4;
        apply_mega_scene_resources(mega, directory);
        expect(original.setup.horizon_index == (track == 9 ? 0u : 3u), "Rain Extras must retain04 sky, with Lavaland's original higher priority");
        verify(original.horizon, mega.horizon, directory);
    }
    inactive(store, track, directory);
}

void woodland(toca::gpu::Renderer& renderer, SDL_Window* window, const toca::AssetStore& store, const std::filesystem::path& directory) {
    toca::gpu::check(SDL_SetWindowSize(window, 960, 600), "Resize woodland surroundings target");
    for (const auto track : {6u, 7u, 8u}) {
        const auto original = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 2, .detail = 3}, 2));
        auto mega = original;
        mega.setup.selection.detail = 4;
        apply_mega_scene_resources(mega, directory);
        verify(original.horizon, mega.horizon, directory, false);
        const std::set<std::string> materials = track == 6   ? std::set<std::string>{"SAP1"}
                                                : track == 7 ? std::set<std::string>{"trge31"}
                                                             : std::set<std::string>{"LLRTS2", "LLRTSX"};
        for (std::size_t slot = 0; slot < original.trees.textures.size(); ++slot) {
            const auto& texture = original.trees.textures[slot];
            if (!materials.contains(texture.name)) {
                continue;
            }
            const auto path = directory / "scene" / (checksum(texture.bmp) + ".webp");
            std::ifstream stream(path, std::ios::binary);
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            const auto& actual = mega.trees.textures.at(slot);
            expect(actual.name == texture.name && actual.encoding == toca::ImageEncoding::webp && !bytes.empty() && bytes == actual.bmp,
                "Woodland surroundings require the exact newly authored M02a strip, without original fallback");
            const auto source = toca::decode_model_texture(texture), image = toca::decode_model_texture(actual);
            expect(image->w == 512 && image->h == source->h * 4, "M02a strip changed original aspect ratio or512px budget");
            std::cout << "WOODLAND_LOAD " << texture.name << ' ' << path.string() << " output_crc32=" << checksum(bytes) << '\n';
        }
        const toca::HpcSurface ground(original.collision_hpc);
        const auto& grid = original.grid.front();
        const auto sample = ground.sample(grid.first_xz[0], grid.first_xz[1]);
        expect(bool(sample), "Woodland surroundings need the original grid's ground support");
        const std::array<std::int32_t, 3> target{grid.first_xz[0], sample->height + 65536, grid.first_xz[1]};
        FinishCamera authored(original.cameras);
        authored.update(target);
        const CameraInputs inputs{authored.world_to_view(target), default_projection(4, 960, 600), authored.position()};
        SceneView source_view(renderer, original, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false);
        SceneView mega_view(renderer, mega, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false);
        toca::SourceRandom random(7);
        FlagAnimation flags(original.flags, random);
        HorizonActors actors(original.moving_horizon);
        const auto& actor_frame = actors.frame();
        CarPose car{unit};
        car.object_to_world[12] = float(target[0]) / 65536;
        car.object_to_world[13] = float(sample->height) / 65536;
        car.object_to_world[14] = float(target[2]) / 65536;
        const auto before = source_view.frame(inputs, scene_lighting(mega.setup), car, flags.variants(), actor_frame);
        const auto after = mega_view.frame(inputs, scene_lighting(mega.setup), car, flags.variants(), actor_frame);
        render(renderer, before, 960, 600, "woodland-track-" + std::to_string(track) + "-original.png");
        render(renderer, after, 960, 600, "woodland-track-" + std::to_string(track) + "-mega.png");
        std::cout << "WOODLAND track=" << track << " authored_camera=" << authored.selected() << " source_roots=" << original.trees.nodes.size()
                  << " camera_q16=" << authored.position()[0] << ',' << authored.position()[1] << ',' << authored.position()[2] << '\n';
        for (const auto& material : materials) {
            const auto slot = std::size_t(std::ranges::find(original.trees.textures, material, &toca::ModelTexture::name) - original.trees.textures.begin());
            const auto found = std::ranges::find_if(original.trees_baked.chunks, [&](const auto& chunk) {
                return std::ranges::any_of(chunk.mesh.faces, [&](const auto& face) {
                    return face.texture == std::int16_t(slot);
                });
            });
            expect(found != original.trees_baked.chunks.end(), "M02a woodland material needs an actual original baked strip consumer");
            std::array<float, 3> minimum{32768, 32768, 32768}, maximum{-32768, -32768, -32768};
            for (const auto& face : found->mesh.faces) {
                if (face.texture != std::int16_t(slot)) {
                    continue;
                }
                for (const auto index : face.indices) {
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        minimum[axis] = std::min(minimum[axis], found->mesh.vertices[index].position[axis]);
                        maximum[axis] = std::max(maximum[axis], found->mesh.vertices[index].position[axis]);
                    }
                }
            }
            const std::array<std::int32_t, 3> strip_target{
                std::int32_t((minimum[0] + maximum[0]) * 32768), std::int32_t((minimum[1] + maximum[1]) * 32768), std::int32_t((minimum[2] + maximum[2]) * 32768)};
            FinishCamera focused(original.cameras);
            focused.update(strip_target);
            const CameraInputs nearby{focused.world_to_view(strip_target), default_projection(4, 960, 600), focused.position()};
            render(renderer, source_view.frame(nearby, scene_lighting(mega.setup), car, flags.variants(), actor_frame), 960, 600,
                "woodland-track-" + std::to_string(track) + "-" + material + "-original.png");
            render(renderer, mega_view.frame(nearby, scene_lighting(mega.setup), car, flags.variants(), actor_frame), 960, 600,
                "woodland-track-" + std::to_string(track) + "-" + material + "-mega.png");
            std::cout << "WOODLAND_STRIP track=" << track << " material=" << material << " original_chunk_node=" << found->node_index << " authored_camera=" << focused.selected()
                      << '\n';
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc >= 3, "Usage: toca-background-tests CANONICAL_ROOT MEGA_ROOT [--pilot|--track N|--material NAME|--all|--woodland-only] [--woodland]");
        bool all = false, woods = false, pilot = true;
        std::optional<std::uint32_t> selected;
        for (int argument = 3; argument < argc; ++argument) {
            const std::string option = argv[argument];
            if (option == "--all") {
                all = true;
                pilot = false;
            } else if (option == "--track") {
                expect(argument + 1 < argc, "--track requires original track0..9");
                selected = std::uint32_t(std::stoul(argv[++argument]));
                expect(*selected < 10, "Background track must be0..9");
                pilot = false;
            } else if (option == "--woodland") {
                woods = true;
            } else if (option == "--woodland-only") {
                woods = true;
                pilot = false;
            } else if (option == "--material") {
                expect(argument + 1 < argc, "--material requires a canonical material name");
                selected_material = argv[++argument];
                pilot = false;
            } else {
                expect(option == "--pilot", "Unknown background acceptance option");
            }
        }
        toca::gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated background test");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Mega backgrounds", 640, 480, 0), SDL_DestroyWindow);
        toca::gpu::check(bool(window), "Create isolated background window");
        toca::gpu::Renderer renderer(window.get());
        const auto store = toca::AssetStore(argv[1]).for_loading();
        {
            std::ofstream budget("background-budget.csv");
            budget << "track_id,material,resident_sky_materials,encoded_bytes,rgba8_bytes\n";
        }
        if (!selected_material.empty()) {
            material(renderer, window.get(), store, argv[2]);
        }
        if (!pilot) {
            for (std::uint32_t track = 0; track < 10; ++track) {
                if (all || (selected && *selected == track)) {
                    track_package(renderer, window.get(), store, argv[2], track, all);
                }
            }
            if (all) {
                expect(verified_images.size() == 48, "Final acceptance must load all48 reachable sky images exactly once by source identity");
            }
        }
        std::vector<std::uint8_t> shared;
        for (const auto track : pilot ? std::vector<unsigned>{0u, 5u} : std::vector<unsigned>{}) {
            const auto original = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 2, .detail = 3}, 1));
            auto mega = original;
            mega.setup.selection.detail = 4;
            apply_mega_scene_resources(mega, argv[2]);
            verify(original.horizon, mega.horizon, argv[2]);
            expect(original.horizon.textures.front().name == "DO02", "Pilot must use Donington cloudy variant one");
            if (track == 0) {
                shared = mega.horizon.textures.front().bmp;
                pictures(renderer, window.get(), original, mega, true);
            } else {
                expect(shared == mega.horizon.textures.front().bmp, "Donington GP and Short must share the same authored image");
            }
        }
        if (woods) {
            woodland(renderer, window.get(), store, argv[2]);
        }
        expect(strong_wrap_images.empty(), "Authored left/right atlas edges introduced a strong repeated upper-sky seam; inspect the captured native views");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
