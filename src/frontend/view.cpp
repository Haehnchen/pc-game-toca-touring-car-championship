#include "frontend/view.hpp"

#include "assets/store.hpp"
#include "frontend/canvas.hpp"
#include "frontend/championship_presentation.hpp"
#include "frontend/extras_icon.hpp"
#include "frontend/font.hpp"
#include "frontend/menu_transition.hpp"
#include "frontend/menus.hpp"
#include "frontend/records.hpp"
#include "frontend/resources.hpp"
#include "frontend/selection_resources.hpp"
#include "frontend/track_lighting.hpp"
#include "frontend/track_resources.hpp"
#include "image/artwork.hpp"
#include "image/decode.hpp"
#include "render/gpu_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <variant>

namespace toca::frontend {
namespace {
void check(bool value, const char* operation) {
    gpu::check(value, operation);
}

Surface wide_title(const std::filesystem::path& artwork_directory) {
    return load_artwork(artwork_directory, "title-wide.webp", true);
}

struct TrackPreview {
    Model model;
    std::vector<glm::mat4> transforms;
    std::vector<gpu::Texture> textures;
    std::vector<bool> palette;

    TrackPreview(const AssetStore& store, gpu::Renderer& renderer, std::uint32_t track) : model(load_track_preview(store, track)) {
        for (const auto& node : model.nodes) {
            const auto local = glm::make_mat4(node.transform.data());
            transforms.push_back(node.parent < 0 ? local : transforms.at(std::size_t(node.parent)) * local);
        }
        for (const auto& texture : model.textures) {
            const auto surface = decode_bmp(texture.bmp);
            textures.push_back(renderer.upload(surface.get(), true));
            palette.push_back(SDL_ISPIXELFORMAT_INDEXED(surface->format));
        }
    }

    void append(gpu::Frame& frame, float rotation, bool available) const {
        constexpr float tau = 6.2831853071795864769f;
        const auto orientation = glm::rotate(glm::mat4(1), tau * .5f, glm::vec3(0, 0, 1)) * glm::rotate(glm::mat4(1), -tau * .125f, glm::vec3(1, 0, 0))
                                 * glm::rotate(glm::mat4(1), tau * (rotation - 2048.f) / 4096.f, glm::vec3(0, 0, 1));
        const auto root = glm::translate(glm::mat4(1), glm::vec3(0, 12, 0)) * orientation;
        constexpr float projection_x = 2.f * (float(0xb333) / 65536.f), projection_y = 2.5f * (float(0xb333) / 65536.f);
        constexpr float near = .25f, far = 200.f;
        for (std::size_t index = 0; index < model.nodes.size(); ++index) {
            const auto& node = model.nodes[index];
            if (node.variants.empty() || node.variants.front() == 0xffff) {
                continue;
            }
            const auto& mesh = model.meshes.at(node.variants.front());
            const auto world = root * transforms[index];
            for (const auto& face : mesh.faces) {
                if (frame.vertices.size() + 3 > 65536) {
                    throw std::runtime_error("Track preview exceeds the original 16-bit frame index range");
                }
                const bool textured = face.texture >= 0;
                frame.draws.push_back({{.texture = textured ? textures.at(std::size_t(face.texture)).get() : nullptr,
                                           .depth = true,
                                           .key = textured,
                                           .clamp = true,
                                           .palette = textured && palette.at(std::size_t(face.texture))},
                    Uint32(frame.indices.size()), 3});
                for (const auto vertex_index : face.indices) {
                    const auto& vertex = mesh.vertices.at(vertex_index);
                    const auto position = glm::vec3(world * glm::vec4(glm::make_vec3(vertex.position.data()), 1));
                    const auto normal = glm::normalize(glm::mat3(world) * glm::make_vec3(vertex.normal.data()));
                    const auto illumination = track_preview_illumination({position.x, position.y, position.z}, {normal.x, normal.y, normal.z}, available);
                    // FUN_00495ee0 uses white texture material; face RGB is only
                    // the untextured material. The helper combines point light
                    // with the explicitly retained native preview ambient.
                    Uint32 color = 0xff000000;
                    for (unsigned channel = 0; channel < 3; ++channel) {
                        const auto component = textured ? 255 : face.color[channel];
                        color |= Uint32(std::lround(float(component) * illumination)) << ((2 - channel) * 8);
                    }
                    const auto z = position.z + 100.f;
                    if (!std::isfinite(z) || z <= near) {
                        throw std::runtime_error("Original track preview crosses its recovered near plane");
                    }
                    frame.indices.push_back(Uint16(frame.vertices.size()));
                    frame.vertices.push_back({(1 + projection_x * position.x / z) * 320.f - .5f, (1 - projection_y * position.y / z) * 240.f - .5f,
                        far / (far - near) - near * far / ((far - near) * z), 1 / z, color, 0xff000000, vertex.u, vertex.v});
                }
            }
        }
    }
};

bool rolling_menu(Screen screen) {
    switch (screen) {
        case Screen::title:
        case Screen::copyright:
        case Screen::scene_loading:
        case Screen::scene:
        case Screen::championship_honors:
        case Screen::championship_finale_loading:
        case Screen::championship_finale:
            return false;
        default:
            return true;
    }
}

gpu::Vertex interpolate(const gpu::Vertex& a, const gpu::Vertex& b, float t) {
    const auto color = [t](std::uint32_t first, std::uint32_t second) {
        std::uint32_t result = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            result |= std::uint32_t(std::lround(std::lerp(float((first >> shift) & 255), float((second >> shift) & 255), t))) << shift;
        }
        return result;
    };
    return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t), std::lerp(a.rhw, b.rhw, t), color(a.diffuse, b.diffuse), color(a.specular, b.specular),
        std::lerp(a.u, b.u, t), std::lerp(a.v, b.v, t)};
}

// The original scans each destination row independently. Small horizontal
// strips preserve that mapping for both indexed content and high-resolution
// Mega glyph/icon quads, including triangles from the track miniature.
void roll_region(gpu::Frame& target, const gpu::Frame& source, const MenuTransitionRegion& region, const MenuQuad& quad, const MenuTransitionRegion* mapping_region = nullptr) {
    const auto& mapping = mapping_region ? *mapping_region : region;
    // Four half-plane clips add at most four corners to a convex triangle.
    // Retain the X-clipped polygon while reusing scratch space for every row.
    std::vector<gpu::Vertex> triangle, scratch, polygon;
    triangle.reserve(7);
    scratch.reserve(7);
    polygon.reserve(7);
    const auto clip = [](std::span<const gpu::Vertex> polygon, std::vector<gpu::Vertex>& result, bool horizontal, float edge, bool above) {
        result.clear();
        if (polygon.empty()) {
            return;
        }
        auto previous = polygon.back();
        const auto coordinate = [horizontal](const gpu::Vertex& vertex) {
            return (horizontal ? vertex.x : vertex.y) + .5f;
        };
        for (const auto& current : polygon) {
            const auto a = coordinate(previous), b = coordinate(current);
            const bool inside_a = above ? a >= edge : a <= edge, inside_b = above ? b >= edge : b <= edge;
            if (inside_a != inside_b) {
                result.push_back(interpolate(previous, current, (edge - a) / (b - a)));
            }
            if (inside_b) {
                result.push_back(current);
            }
            previous = current;
        }
    };
    for (const auto& draw : source.draws) {
        for (std::size_t index = draw.first; index + 2 < std::size_t(draw.first) + draw.count; index += 3) {
            triangle.clear();
            for (std::size_t corner = 0; corner < 3; ++corner) {
                triangle.push_back(source.vertices.at(std::size_t(draw.vertex_offset) + source.indices.at(index + corner)));
            }
            clip(triangle, scratch, true, region.x, true);
            clip(scratch, triangle, true, region.x + region.width, false);
            if (triangle.empty()) {
                continue;
            }
            float top = region.y + region.height, bottom = region.y;
            for (const auto& vertex : triangle) {
                top = std::min(top, vertex.y + .5f);
                bottom = std::max(bottom, vertex.y + .5f);
            }
            const int first = int(std::floor(std::max(top, region.y))), last = int(std::ceil(std::min(bottom, region.y + region.height)));
            for (int row = first; row < last; ++row) {
                clip(triangle, scratch, false, float(row), true);
                clip(scratch, polygon, false, float(row + 1), false);
                if (polygon.size() < 3) {
                    continue;
                }
                const auto offset = target.batch_vertex_offset(polygon.size(), (polygon.size() - 2) * 3);
                const auto base = Uint16(target.vertices.size() - std::size_t(offset));
                auto state = draw.state;
                const auto count = Uint32((polygon.size() - 2) * 3);
                if (!target.draws.empty() && target.draws.back().state == state && target.draws.back().vertex_offset == offset) {
                    target.draws.back().count += count;
                } else {
                    target.draws.push_back({state, Uint32(target.indices.size()), count, offset});
                }
                for (auto vertex : polygon) {
                    const auto point = menu_transition_point(quad, (vertex.x + .5f - mapping.x) / mapping.width, (vertex.y + .5f - mapping.y) / mapping.height);
                    vertex.x = point.x - .5f;
                    vertex.y = point.y - .5f;
                    target.vertices.push_back(vertex);
                }
                for (std::size_t corner = 1; corner + 1 < polygon.size(); ++corner) {
                    target.indices.insert(target.indices.end(), {base, Uint16(base + corner), Uint16(base + corner + 1)});
                }
            }
        }
    }
}
} // namespace

struct View::Impl {
    const AssetStore& store;
    gpu::Renderer& renderer;
    std::uint32_t background_index;
    gpu::Texture title{nullptr, gpu::Release{}}, loading{nullptr, gpu::Release{}}, title_wide{nullptr, gpu::Release{}}, menu_texture{nullptr, gpu::Release{}},
        side_backdrop{nullptr, gpu::Release{}}, mega_backdrop{nullptr, gpu::Release{}}, mega_frame{nullptr, gpu::Release{}};
    gpu::Texture content_texture{nullptr, gpu::Release{}}, outgoing_texture{nullptr, gpu::Release{}};
    std::array<gpu::Texture, 14> roll_backdrops;
    MenuTransitionResources menu_transition;
    gpu::Frame previous_content, outgoing_content;
    std::uint64_t transition_started_ns = 0;
    int transition_direction = 1;
    bool transition_startup = false, transition_running = false;
    std::uint32_t previous_exit_icon = 0;
    std::uint32_t title_width = 0, title_height = 0;
    std::uint32_t mega_backdrop_width = 0, mega_backdrop_height = 0;
    Menus menus;
    ChampionshipPresentationResources championship_presentation;
    gpu::Texture honors_background{nullptr, gpu::Release{}};
    std::optional<std::uint32_t> honors_block;
    SelectionResources selections;
    TrackResources tracks;
    RecordResources record_resources;
    LapRecords default_records;
    std::array<Surface, 4> record_medals{{{nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}}};
    std::optional<TrackPreview> track_preview;
    std::optional<std::uint32_t> preview_track;
    std::array<Font, 6> fonts;

    using FontGlyph = Canvas::FontGlyph;
    using CanvasSprite = Canvas::CanvasSprite;
    using ArtworkSprite = Canvas::ArtworkSprite;
    Canvas::FontColors font_colors;

    std::vector<std::variant<FontGlyph, CanvasSprite, ArtworkSprite>> font_overlay;
    std::vector<std::variant<FontGlyph, CanvasSprite, ArtworkSprite>> outgoing_overlay;
    std::map<std::string, gpu::Texture> menu_artwork;
    std::map<std::string, gpu::Texture> garage_artwork;
    bool mega_fonts = false, white_font_overlay = false;
    std::array<Surface, 4> icons{{{nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}}};
    Surface background{nullptr, SDL_DestroySurface}, car_background{nullptr, SDL_DestroySurface}, car_inset{nullptr, SDL_DestroySurface}, canvas{nullptr, SDL_DestroySurface},
        disabled{nullptr, SDL_DestroySurface};
    Surface name_cursor{nullptr, SDL_DestroySurface}, record_badges{nullptr, SDL_DestroySurface};
    Surface rgba_canvas{nullptr, SDL_DestroySurface};
    std::vector<std::uint8_t> backdrop_pixels;
    std::vector<std::uint8_t> content_mask = std::vector<std::uint8_t>(640 * 480);
    bool recording_content = false;
    std::array<Surface, 2> extras_icons{{{nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}}};
    std::vector<Surface> native_icons;
    std::vector<std::uint8_t> alias;
    bool ready = false;
    std::optional<Screen> cached_screen;
    std::uint64_t cached_menu_entered_ns = 0;
    std::size_t cached_selection = 0;
    std::uint32_t cached_language = 0;
    std::uint8_t cached_color = 0;
    std::string cached_content;

    Canvas painter{{renderer, fonts, font_colors, font_overlay, canvas, rgba_canvas, alias, content_mask, recording_content, mega_fonts, white_font_overlay}};

    Impl(const AssetStore& source, gpu::Renderer& gpu, std::uint32_t selected_background) : store(source), renderer(gpu), background_index(selected_background) {
        if (background_index >= 8) {
            throw std::runtime_error("Original frontend background index must be 0..7");
        }
        const auto artwork_directory = artwork_root();
        const auto original = load_title(artwork_directory);
        title = renderer.upload(original.get());
        loading = renderer.upload(load_copyright(artwork_directory, true).get());
        const auto wide = wide_title(artwork_directory);
        title_wide = renderer.upload(wide.get());
        title_width = static_cast<std::uint32_t>(wide->w);
        title_height = static_cast<std::uint32_t>(wide->h);
    }

    bool artwork(const std::string& filename, int width, int height, int x, int y) {
        if (!mega_fonts) {
            return false;
        }
        const auto selected = find_artwork(artwork_root(), "menu-icons/" + filename, true);
        // Optional authored icons retain the indexed BFP sprite when absent.
        if (!selected) {
            return false;
        }
        const auto entry = menu_artwork.try_emplace(selected->string(), nullptr, gpu::Release{}).first;
        if (!entry->second) {
            const auto surface = load_artwork_file(*selected);
            if (surface->w != width * 4 || surface->h != height * 4) {
                throw std::runtime_error("Mega menu artwork must preserve fourfold original sprite dimensions: " + selected->string());
            }
            entry->second = renderer.upload(surface.get());
        }
        font_overlay.emplace_back(ArtworkSprite{entry->second.get(), x, y, width, height});
        return true;
    }

    void composed_garage(std::uint32_t variant, std::uint32_t car) {
        const auto filename = "gar" + std::to_string(variant) + "-car" + std::to_string(car) + ".webp";
        const auto root = artwork_root();
        const auto selected = find_artwork(root, "garage/" + filename, mega_fonts);
        if (!selected) {
            throw std::runtime_error("Required garage artwork not found: " + filename);
        }
        const auto entry = garage_artwork.try_emplace(selected->string(), nullptr, gpu::Release{}).first;
        if (!entry->second) {
            const auto surface = load_artwork_file(*selected);
            const bool original_size = surface->w == 573 && surface->h == 357;
            const bool mega_size = surface->w == 1146 && surface->h == 714;
            if (!original_size && !mega_size) {
                throw std::runtime_error("Garage artwork must retain its 573x357 opening at original or twofold resolution: " + selected->string());
            }
            entry->second = renderer.upload(surface.get());
        }
        font_overlay.emplace_back(ArtworkSprite{entry->second.get(), 34, 63, 573, 357});
    }

    void arrow(std::string_view direction, int x, int y, bool selected = true, bool alias_edges = true) {
        const bool up = direction == "up";
        const int width = up ? 30 : 24, height = up ? 24 : 30;
        if (artwork("arrow-" + std::string(direction) + (selected ? "-selected.webp" : "-normal.webp"), width, height, x, y)) {
            return;
        }
        const int source_x = (direction == "left" ? 64 : 1) + (selected ? 0 : 90);
        painter.blit(icons[1].get(), source_x, up ? 344 : 400, width, height, x, y, true, alias_edges);
    }

    void unavailable_icon(int x, int y) {
        if (!artwork("unavailable.webp", 88, 56, x, y)) {
            painter.blit(disabled.get(), 0, 0, 88, 56, x, y, true, true);
        }
    }

    void icon(std::uint32_t id, int x, int y, bool selected, bool unavailable) {
        if (id == 0) {
            return;
        }
        const bool native = id >= extras_icon && id <= unlocks_icon;
        if (!native && (id > 60 || (id > 36 && id < 49))) {
            throw std::runtime_error("Frontend icon ID has no original atlas sheet");
        }
        if (artwork("icon-" + std::to_string(id) + (selected ? "-selected.webp" : "-normal.webp"), 88, 56, x, y)) {
            if (unavailable) {
                unavailable_icon(x, y);
            }
            return;
        }
        if (id == extras_icon) {
            painter.blit(extras_icons[std::size_t(selected)].get(), 0, 0, extras_icon_width, extras_icon_height, x, y, true, true);
            if (unavailable) {
                unavailable_icon(x, y);
            }
            return;
        }
        if (id >= fullscreen_icon && id <= unlocks_icon) {
            painter.blit(native_icons.at((id - fullscreen_icon) * 2 + std::size_t(selected)).get(), 0, 0, extras_icon_width, extras_icon_height, x, y, true, true);
            if (unavailable) {
                unavailable_icon(x, y);
            }
            return;
        }
        const auto group = (id - 1) / 12;
        const std::size_t sheet = group == 4 ? 3 : group == 2 ? 2 : group == 1 ? 1 : 0;
        if (group == 1) {
            id -= 12;
        } else if (group == 2) {
            id -= 24;
        } else if (group == 4) {
            id -= 48;
        }
        const auto row = id / 3;
        const auto remainder = id % 3;
        const int column = remainder == 0 ? 2 : static_cast<int>(remainder) - 1;
        const int source_row = remainder == 0 ? static_cast<int>(row) * 2 - 2 + int(selected) : static_cast<int>(row) * 2 + int(selected);
        painter.blit(icons[sheet].get(), column * 89 + 1, source_row * 57 + 1, 88, 56, x, y, true, true);
        if (unavailable) {
            unavailable_icon(x, y);
        }
    }

    void prepare_backdrop() {
        // FUN_0047206f first draws the car backdrop, then color-keyed ROLL artwork.
        painter.blit(car_background.get(), 0, 0, 640, 480, 0, 0, false);
        const auto& inset = menus.backdrop_inset;
        painter.blit(car_inset.get(), inset.x, inset.y, inset.width, inset.height, inset.x, inset.y, false);
        painter.blit(background.get(), 0, 0, 640, 480, 0, 0, true);
        backdrop_pixels.resize(640 * 480);
        for (int y = 0; y < 480; ++y) {
            std::memcpy(backdrop_pixels.data() + y * 640, static_cast<const std::uint8_t*>(canvas->pixels) + y * canvas->pitch, 640);
        }
    }

    void prepare_roll_backdrops(const Surface& third, const std::vector<Surface>& rolls) {
        for (std::size_t phase = 0; phase < rolls.size(); ++phase) {
            painter.blit(car_background.get(), 0, 0, 640, 480, 0, 0, false);
            // 47206f changes the inset photograph every three ROLL phases.
            // 470e74 clips its screen-space pixels rather than rotating them.
            const auto* photo = ((phase / 3) % 2 == 0 ? car_inset : third).get();
            const auto& quad = menu_transition.frames[phase][std::size_t(MenuRegion::content)];
            for (int y = 0; y < 480; ++y) {
                float left = 640, right = 0;
                for (std::size_t edge = 0; edge < 4; ++edge) {
                    const auto& a = quad.corners[edge];
                    const auto& b = quad.corners[(edge + 1) % 4];
                    const auto row = float(y) + .5f;
                    if ((a.y <= row && row < b.y) || (b.y <= row && row < a.y)) {
                        const auto x = std::lerp(a.x, b.x, (row - a.y) / (b.y - a.y));
                        left = std::min(left, x);
                        right = std::max(right, x);
                    }
                }
                for (int x = std::max(0, int(std::ceil(left))); x < std::min(640, int(std::ceil(right))); ++x) {
                    painter.destination(x, y) = Canvas::pixel(photo, x, y);
                }
            }
            painter.blit(rolls[phase].get(), 0, 0, 640, 480, 0, 0, true);
            check(SDL_BlitSurface(canvas.get(), nullptr, rgba_canvas.get(), nullptr), "Convert original menu roll backdrop");
            roll_backdrops[phase] = renderer.upload(rgba_canvas.get());
        }
    }

    void backdrop() {
        recording_content = false;
        std::ranges::fill(content_mask, 0);
        if (mega_fonts) {
            // Mega draws its generated frame directly at display resolution.
            // Original ROLL1, Audi1 and its inset stay in the other profiles.
            check(SDL_FillSurfaceRect(canvas.get(), nullptr, 0), "Clear Mega menu canvas");
            recording_content = true;
            return;
        }
        for (int y = 0; y < 480; ++y) {
            std::memcpy(static_cast<std::uint8_t*>(canvas->pixels) + y * canvas->pitch, backdrop_pixels.data() + y * 640, 640);
        }
        recording_content = true;
    }

    Surface load_mega_image(const std::string& filename) {
        return load_artwork(artwork_root(), filename, true);
    }

    void load_mega_menu_artwork() {
        if (!mega_backdrop) {
            constexpr std::array backgrounds{"menu-background-wide.webp", "menu-background-renault-wide.webp", "menu-background-honda-wide.webp", "menu-background-volvo-wide.webp",
                "menu-background-ford-wide.webp", "menu-background-nissan-wide.webp", "menu-background-peugeot-wide.webp", "menu-background-vectra-wide.webp"};
            const auto surface = load_mega_image(backgrounds.at(background_index));
            mega_backdrop = renderer.upload(surface.get());
            mega_backdrop_width = std::uint32_t(surface->w);
            mega_backdrop_height = std::uint32_t(surface->h);
        }
        if (!mega_frame) {
            const auto surface = load_mega_image("menu-frame.webp");
            mega_frame = renderer.upload(surface.get());
        }
    }

    void render_menu(const Controller& controller, const Menu& menu) {
        backdrop();
        painter.fit(menu.title, 3, 3, 41, 599, 24, -1, true, false);
        if (controller.screen() == Screen::controller_keyboard_bindings || controller.screen() == Screen::controller_gamepad_bindings) {
            // 473d59: centered prompt at120 and the current action at150.
            // The native transaction retains all nine assignments off screen.
            const auto& labels = menus.controller_labels.at(controller.language());
            painter.fit(menu.notice, 2, 2, 41, 599, 120, 0x28, true, false);
            painter.fit(labels.actions.at(controller.selection()), 2, 2, 41, 599, 150, 0x28, true, false);
            painter.fit(menu.status, 2, 2, 41, 599, 360, 0x61, true, false);
            painter.fit(menu.items.at(controller.selection()).help, 2, 2, 41, 599, 440, 0x28, true, false);
            painter.upload(menu_texture.get(), content_texture.get());
            return;
        }
        if (menu.waiting_room) {
            // 466f4d: eight roster lines share the Waiting Room with Start.
            // Names remain visible while their owner is still selecting a car.
            for (std::size_t index = 0; index < menu.lines.size(); ++index) {
                const auto& line = menu.lines[index];
                const auto y = 82 + static_cast<int>(index) * 28;
                const auto color = line.committed ? 0x61 : 0x28;
                painter.fit(line.name, 1, 2, 63, 236, y, color, false, true);
                painter.fit(line.car, 1, 2, 245, 366, y, color, false, true);
                painter.fit(line.markers, 2, 2, 375, 505, y + 2, color, false, true);
                painter.fit(line.status, 1, 2, 525, 590, y, color, false, true);
            }
            painter.fit(menu.status, 1, 2, 73, 590, 315, 0x61, false, true);
            for (std::size_t index = 0; index < menu.details.size() && index < 6; ++index) {
                const auto y = index < 2 ? 302 + int(index) * 16 : 375 + int(index - 2) * 16;
                painter.fit(menu.details[index], 2, 2, 41, 599, y, 0x61, true, true);
            }
            for (std::size_t index = 0; index < menu.items.size(); ++index) {
                const auto available = controller.item_available(index);
                const auto color = controller.selection() == index && available ? controller.highlight_color() : 0x28;
                painter.fit(menu.items[index].label, 0, 1, 194, 599, 346 + static_cast<int>(index) * 36, color, false, true);
            }
            painter.fit(menu.notice, 2, 2, 41, 599, 411, 0x61, true, false);
            painter.fit("ESC : " + menu.items.front().help, 2, 2, 41, 599, 440, 0x28, true, false);
            painter.upload(menu_texture.get(), content_texture.get());
            return;
        }
        if (menu.compact) {
            const bool network = controller.screen() == Screen::network_linkup;
            for (std::size_t index = 0; index < menu.items.size(); ++index) {
                const auto y = network ? 76 + static_cast<int>(index) * 32 : settings_row_y(index);
                const bool selected = controller.selection() == index;
                const bool available = controller.item_available(index);
                if (menu.items[index].adjustable) {
                    arrow("left", 53, y - 3, selected && available);
                    arrow("right", 563, y - 3, selected && available);
                }
                painter.fit(menu.items[index].label, 1, 2, 91, 549, y, selected && available ? controller.highlight_color() : 0x28, true, true);
            }
            painter.fit(menu.notice, 2, 2, 41, 599, 401, 0x61, true, false);
            painter.fit(menu.items.at(controller.selection()).help, 2, 2, 41, 599, 440, 0x28, true, false);
            painter.upload(menu_texture.get(), content_texture.get());
            return;
        }
        // Preserve the original icon size and row spacing for longer native menus.
        const auto page_size = std::min(std::size_t(5), menu.items.size());
        const auto first = controller.selection() / page_size * page_size;
        const auto end = std::min(first + page_size, menu.items.size());
        if (menu.items.size() > page_size) {
            painter.fit(std::to_string(first / page_size + 1) + " / " + std::to_string((menu.items.size() + page_size - 1) / page_size), 2, 2, 520, 599, 75, 0x28, false, true);
        }
        for (std::size_t index = first; index < end; ++index) {
            const auto y = menu_row_y(page_size, index - first);
            const auto& item = menu.items[index];
            const bool selected = controller.selection() == index;
            const bool controller_menu =
                controller.screen() == Screen::controller_players || controller.screen() == Screen::controller_options || controller.screen() == Screen::controller_auto_configure;
            const bool available = !controller_menu || controller.item_available(index);
            icon(item.icon, 73, y, selected, !available);
            if ((controller.screen() == Screen::single_options && index < 4) || item.adjustable) {
                // FUN_0046e1b0 uses atlas offset90 for unselected arrows;
                // FUN_0046dee5 uses offset0 for the selected adjustable row.
                arrow("left", 47, y + 15, selected && item.decrease_available);
                arrow("right", 164, y + 15, selected && item.increase_available);
            }
            const int color = selected && available ? controller.highlight_color() : 0x28;
            const auto label = item.suffix.empty() ? item.label : item.label + " " + item.suffix;
            painter.fit(label, 0, 1, 194, 599, y + 19, color, false, true);
        }
        const auto& help = menu.items.at(controller.selection()).help;
        if (!menu.notice.empty()) {
            painter.fit(menu.notice, 2, 2, 41, 599, 401, 0x61, true, false);
        }
        painter.fit(help, 2, 2, 41, 599, 440, 0x28, true, false);
        painter.upload(menu_texture.get(), content_texture.get());
    }

    void render_name(const Controller& controller) {
        backdrop();
        const auto& labels = selections.labels.at(controller.language());
        painter.fit(labels.name_title + (controller.setup().players == 2 ? " - P" + std::to_string(controller.selection_player() + 1) : ""), 3, 3, 41, 599, 24, -1, true, false);
        const auto& network = menus.network_labels.at(controller.language());
        const auto& prompt = controller.setup().mode == GameMode::linkup ? (controller.network_hosting() ? network.session_name : network.player_name) : labels.name_prompt;
        painter.fit(prompt, 0, 0, 41, 599, 161, 0x28, true, true);
        painter.fit(controller.player_name(controller.selection_player()), 0, 0, 0, 600, 219, 0x61, true, true);
        if (controller.name_cursor_visible()) {
            // FUN_00477d73/004782dd: centered text end +1, baseline -4.
            const auto width = fonts[0].text_width(controller.player_name(controller.selection_player()));
            painter.blit(name_cursor.get(), 0, 0, 11, 25, 300 - width / 2 + width + 1, 215, true);
        }
        if (!controller.cheats_error().empty()) {
            constexpr std::array<std::string_view, 3> failed{"COULD NOT SAVE EXTRAS", "ECHEC SAUVEGARDE EXTRAS", "EXTRAS SPEICHERN FEHLGESCHLAGEN"};
            painter.fit(failed.at(controller.language()), 2, 2, 41, 599, 401, 0x61, true, false);
        }
        painter.fit(labels.name_help, 2, 2, 41, 599, 440, 0x28, true, false);
        painter.upload(menu_texture.get(), content_texture.get());
    }

    void render_car(const Controller& controller) {
        const auto car = controller.car_id(controller.selection_player());
        const auto state = cheats::for_mode(controller.cheats_state(), static_cast<std::uint32_t>(controller.setup().mode));
        const auto variant = garage_variant(car, cheats::car_available(state, 8), cheats::car_available(state, 9));
        backdrop();
        composed_garage(variant, car);
        const auto& labels = selections.labels.at(controller.language());
        const auto& info = selections.cars.at(car);
        const bool available = controller.car_available();
        painter.fit(labels.title + (controller.setup().players == 2 ? " - P" + std::to_string(controller.selection_player() + 1) : ""), 3, 3, 41, 599, 24, -1, true, false);
        painter.text(fonts[2], available ? info.name : labels.unknown, 48, 155, 0x61);
        painter.text(fonts[2], labels.drive + ": " + (available ? labels.drive_types.at(info.drive) : labels.unknown), 48, 170, 0x61);
        painter.text(fonts[2], labels.valves + ": " + (available ? std::to_string(info.valves) : labels.unknown), 48, 185, 0x61);
        std::string power = available ? info.power : labels.unknown;
        if (available && info.rpm != 0) {
            power += " " + labels.power + ", " + std::to_string(info.rpm) + " " + labels.rpm;
        }
        painter.text(fonts[2], power, 48, 200, 0x61);
        if (!available) {
            painter.text(fonts[2], labels.unknown, 48, 215, 0x61);
        }
        painter.text(fonts[0], controller.transmission_manual(controller.selection_player()) ? labels.manual : labels.automatic, 48, 384, 0x61);
        icon(23, 468, 351, false, !available);
        if (car > 0) {
            arrow("left", 442, 366);
        }
        if (car < 9) {
            arrow("right", 559, 366);
        }
        arrow("up", 499, 325);
        const bool save_error = controller.setup().mode == GameMode::championship && !controller.championship_error().empty();
        painter.fit(save_error  ? menus.championship_labels.at(controller.language()).file_error
                    : available ? labels.transmission_help
                                : labels.unavailable_help,
            2, 2, 41, 599, 440, save_error ? 0x61 : 0x28, true, false);
        painter.upload(menu_texture.get(), content_texture.get());
    }

    void render_track(const Controller& controller) {
        const auto track = controller.track_id();
        if (!preview_track || *preview_track != track) {
            track_preview.emplace(store, renderer, track);
            preview_track = track;
        }
        backdrop();
        const auto& labels = tracks.labels.at(controller.language());
        const auto& info = tracks.tracks.at(track);
        // FUN_0046dafc/0046d56b: centered text origin subtracts 41, adds UI x33.
        painter.fit(labels.title, 3, 3, 33, 591, 24, -1, true, false);
        painter.fit(info.name, 0, 0, 33, 591, 78, 0x61, true, true);
        painter.text(fonts[2], labels.location + ": " + info.location, 73, 345, 0x61, 0, true);
        painter.text(fonts[2], labels.length + ": " + track_length(info, controller.language()) + " " + labels.unit, 73, 365, 0x61, 0, true);
        const auto weather = controller.setup().weather == 0 ? info.weather : controller.setup().weather;
        const auto& weather_text = controller.setup().weather == 0 && weather == 2 && (track == 0 || track == 8) ? labels.overcast : labels.weather_values.at(weather);
        painter.text(fonts[2], labels.weather + " : " + weather_text, 73, 384, 0x61, 0, true);

        // FUN_0046d56b reserves width for nine 'W' characters even for an empty name.
        const auto& record = (controller.records() ? *controller.records() : default_records).at(track);
        const auto fastest = labels.fastest_lap + ": " + record_time(record.lap_ms);
        const auto record_width = std::max(fonts[2].text_width(labels.fastest_lap + ": 0:00.00"), fonts[2].text_width(labels.by + " WWWWWWWWW"));
        const auto record_x = 599 - record_width - 20;
        painter.text(fonts[2], fastest, record_x, 345, 0x61, 0, true);
        painter.text(fonts[2], labels.by + " " + record.driver, record_x, 365, 0x61, 0, true);
        painter.text(fonts[2], labels.in + " " + selections.cars.at(record.car_id).name, record_x, 384, 0x61, 0, true);
        if (track > 0) {
            arrow("left", 125, 189, true, false);
        }
        if (track < 9) {
            arrow("right", 474, 189, track < controller.track_max_id(), false);
        }
        painter.fit(controller.track_available() ? labels.help : labels.locked_help, 2, 2, 33, 591, 440, 0x28, true, false);
        painter.upload(menu_texture.get(), content_texture.get());
    }

    void render_championship_dialog(const Controller& controller, const Menu& menu) {
        backdrop();
        painter.fit(menu.title, 4, 4, 41, 599, 144, 0x61, true, true);
        if (controller.screen() != Screen::championship_save_feedback) {
            for (std::size_t index = 0; index < menu.items.size(); ++index) {
                painter.fit(
                    menu.items[index].label, 4, 4, 41, 599, 221 + static_cast<int>(index) * 42, index == controller.selection() ? controller.highlight_color() : 0x28, true, true);
            }
        }
        painter.upload(menu_texture.get(), content_texture.get());
    }

    void render_championship_honors(const Controller& controller) {
        const auto& honors = controller.championship_honors().value();
        if (honors_block != honors.block) {
            auto background = load_championship_honors_background(store, honors.block, 640);
            honors_background = renderer.upload(background.get());
            honors_block = honors.block;
        }
        check(SDL_FillSurfaceRect(canvas.get(), nullptr, 0), "Clear championship honors overlay");
        for (const auto& line : championship_honors_lines(championship_presentation, honors, controller.language())) {
            const auto y = static_cast<int>((std::uint64_t(line.y_q16) * 480) >> 16);
            painter.fit(line.text, line.heading ? 5 : 4, line.heading ? 5 : 4, 20, 620, y, line.heading ? 0x61 : 0x90, true, true);
        }
        if (!controller.championship_error().empty()) {
            painter.fit(menus.championship_labels.at(controller.language()).file_error, 4, 4, 20, 620, 440, 0x61, true, true);
        }
        painter.upload(menu_texture.get(), content_texture.get(), true, true);
    }

    void render_championship(const Controller& controller) {
        backdrop();
        const auto& labels = menus.championship_labels.at(controller.language());
        const auto& state = controller.championship_state().value();
        painter.fit(labels.table_title, 3, 3, 41, 599, 24, -1, true, false);
        std::string status = labels.round + " " + std::to_string(state.meeting + 1) + " / 12   " + labels.race + " " + std::to_string(state.race + 1);
        if (state.phase == race::ChampionshipPhase::complete) {
            status = labels.complete;
        } else if (state.phase == race::ChampionshipPhase::eliminated) {
            status = labels.eliminated;
        }
        const bool terminal = state.phase == race::ChampionshipPhase::complete || state.phase == race::ChampionshipPhase::eliminated;
        // GSTRINGS game captions include punctuation absent from every menu
        // font. The original game row font retains those glyphs at28px high.
        painter.fit(status, terminal ? 4 : 1, terminal ? 4 : 2, 41, 599, 74, 0x61, true, true);
        painter.fit(labels.player, 1, 2, 105, 449, 111, 0x28, false, true);
        painter.fit(labels.points, 1, 2, 453, 550, 111, 0x28, false, true);
        std::array<std::uint32_t, 16> order{};
        std::iota(order.begin(), order.end(), 0u);
        std::stable_sort(order.begin(), order.end(), [&](auto left, auto right) {
            return state.points[left] > state.points[right];
        });
        const auto first = controller.selection() * 8;
        for (std::size_t row = 0; row < 8; ++row) {
            const auto actor = order[first + row];
            const auto y = 147 + static_cast<int>(row) * 28;
            const auto color = actor == state.local_actor ? 0x61 : 0x90;
            const auto& driver = actor == state.local_actor ? state.driver : state.drivers[actor].driver;
            painter.fit(std::to_string(first + row + 1), 1, 2, 63, 100, y, color, false, true);
            painter.fit(championship_driver_label(driver), 1, 2, 105, 449, y, color, false, true);
            painter.fit(std::to_string(state.points[actor]), 1, 2, 453, 550, y, color, false, true);
        }
        painter.fit(std::to_string(controller.selection() + 1) + " / 2", 2, 2, 510, 599, 389, 0x28, false, true);
        if (!controller.championship_error().empty()) {
            painter.fit(labels.file_error, 2, 2, 41, 599, 410, 0x61, true, false);
        }
        painter.fit(labels.continue_label + " / " + labels.back, 2, 2, 41, 599, 440, 0x28, true, false);
        painter.upload(menu_texture.get(), content_texture.get());
    }

    void render_records(const Controller& controller) {
        backdrop();
        const auto& labels = record_resources.labels.at(controller.language());
        painter.fit(labels.title, 3, 3, 41, 599, 24, -1, true, false);
        // 46b957: original frontend offset33/15, five rows per page.
        painter.fit(labels.track, 0, 1, 63, 188, 78, 0x28, false, true);
        painter.fit(labels.time, 0, 1, 188, 333, 78, 0x28, false, true);
        painter.fit(labels.player, 0, 1, 333, 599, 78, 0x28, false, true);
        const auto& records = controller.records() ? *controller.records() : default_records;
        for (std::uint32_t row = 0; row < 5; ++row) {
            const auto track = std::uint32_t(controller.selection()) * 5 + row;
            if (track == 9 && !cheats::track_available(cheats::for_mode(controller.cheats_state(), static_cast<std::uint32_t>(controller.setup().mode)), 9)) {
                continue;
            }
            const auto& record = records.at(track);
            const auto color = controller.record_highlight_track() == track ? 0x61 : 0x90;
            const auto y = 124 + int(row) * (21 + fonts[0].line_height * 2);
            painter.text(fonts[0], record_resources.tracks.at(track), 63, y, color, 0, true);
            painter.fit(record.driver, 0, 1, 333, 599, y, color, false, true);
            int x = 188;
            for (const auto character : record_time(record.lap_ms)) {
                const auto value = std::string(1, character);
                const auto width = fonts[0].find(character == ':' || character == '.' ? ':' : '0')->width + fonts[0].advance;
                // 64dff halves the cell and glyph separately; their parity
                // differs for these digits, so keep both integer truncations.
                painter.text(fonts[0], value, x + width / 2 - fonts[0].text_width(value) / 2, y, color, 0, true);
                x += width;
            }
            if (!record.code.empty()) {
                painter.text(fonts[2], record.code, 188, y + 18, 0x61, 0, true, 1);
            }
            const auto& badge = record_resources.badges.at(record.car_id);
            painter.blit(record_badges.get(), badge.x, badge.y, badge.width, badge.height, 551 + (48 - badge.width) / 2, y, true);
        }
        painter.upload(menu_texture.get(), content_texture.get());
    }

    void render_championship_records(const Controller& controller) {
        backdrop();
        const auto& labels = record_resources.championship_labels.at(controller.language());
        //46c1ce/46c21b: frontend offset33/15, nine rows, four16px cups.
        painter.fit(labels.title, 3, 3, 41, 599, 24, -1, true, false);
        painter.fit(labels.player, 0, 1, 63, 283, 78, 0x28, false, true);
        painter.fit(labels.points, 0, 1, 283, 431, 78, 0x28, false, true);
        painter.fit(labels.round, 0, 1, 431, 599, 78, 0x28, false, true);
        const auto records = controller.championship_records() ? std::span<const ChampionshipRecord>(*controller.championship_records()) : std::span<const ChampionshipRecord>{};
        for (std::size_t row = 0; row < records.size(); ++row) {
            const auto& record = records[row];
            const auto y = 122 + int(row) * (17 + fonts[0].line_height);
            const auto color = row == 0 ? controller.championship_record_color() : 0x90;
            painter.fit(record.driver, 0, 1, 63, 283, y, color, false, true);
            painter.text(fonts[0], std::to_string(record.points), 283, y, color, 0, true);
            painter.text(fonts[0], std::to_string(record.round), 431, y, color, 0, true);
            for (std::size_t block = 0; block < record.awards.size(); ++block) {
                painter.blit(record_medals.at(record.awards[block]).get(), 0, 0, 16, 16, 513 + int(block) * 17, y, true);
            }
        }
        if (!controller.championship_records_error().empty()) {
            //The original footer is palette97 yellow; retain its ordinary
            //contrasting help color40 and a black edge for the error caption.
            painter.fit(menus.championship_labels.at(controller.language()).file_error, 2, 2, 41, 599, 440, 0x28, true, true);
        }
        painter.upload(menu_texture.get(), content_texture.get());
    }
};

View::View(const AssetStore& store, gpu::Renderer& renderer, std::uint32_t background_index) : impl_(std::make_unique<Impl>(store, renderer, background_index)) {}

View::~View() = default;

void View::load_frontend() {
    auto& view = *impl_;
    if (view.ready) {
        return;
    }
    const auto archive = view.store.open_archive("data/frontend.bfp");
    for (std::size_t index = 0; index < 4; ++index) {
        view.icons[index] = decode_bmp(archive.member(index).bytes());
        view.fonts[index] = decode_font(archive.member(24 + index).bytes());
    }
    const auto game_images = view.store.open_archive("data/image640.bfp");
    view.fonts[4] = decode_font(game_images.member(24).bytes()); // LIL640: original GSTRINGS punctuation.
    view.fonts[5] = decode_font(game_images.member(23).bytes()); // BIG640: original honors headings.
    add_menu_parentheses(view.fonts[0], view.fonts[2]);
    add_menu_parentheses(view.fonts[1], view.fonts[2]);
    // FUN_004714a5(5) starts the roll animation; FUN_00471ca3 reaches stable index 0.
    view.background = decode_bmp(archive.member(4).bytes()); // ROLL1, the complete menu frame.
    // FUN_0047024c advances five members per startup choice: four car images
    // and its palette alias. The stable composition uses the first two images.
    const auto car_member = 29 + 5 * view.background_index;
    view.car_background = decode_bmp(archive.member(car_member).bytes());
    view.car_inset = decode_bmp(archive.member(car_member + 1).bytes());
    view.disabled = decode_bmp(archive.member(23).bytes());
    view.name_cursor = decode_bmp(archive.member(28).bytes()); // FUN_00477d0a: CURSOR3.
    view.record_badges = decode_bmp(archive.member(22).bytes()); // 46ff22 / 470d63: car manufacturer badges.
    for (std::size_t tier = 0; tier < view.record_medals.size(); ++tier) {
        view.record_medals[tier] = decode_bmp(archive.member(18 + tier).bytes()); //46ff22/470ccc: gray, bronze, silver, gold.
    }
    check(view.name_cursor->w == 11 && view.name_cursor->h == 25 && view.name_cursor->format == SDL_PIXELFORMAT_INDEX8, "Read authored name cursor");
    const auto alias = archive.member(car_member + 4).bytes();
    if (alias.size() != 65536) {
        throw std::runtime_error("Frontend alias table must have 256x256 entries");
    }
    view.alias.assign(alias.begin(), alias.end());
    view.canvas.reset(SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_INDEX8));
    check(bool(view.canvas), "Create frontend indexed canvas");
    view.rgba_canvas.reset(SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_RGBA32));
    check(bool(view.rgba_canvas) && view.rgba_canvas->pitch == 640 * 4, "Create packed frontend RGBA canvas");
    view.menu_texture = view.renderer.texture(640, 480);
    view.content_texture = view.renderer.texture(640, 480);
    auto* palette = SDL_CreateSurfacePalette(view.canvas.get());
    auto* original = SDL_GetSurfacePalette(view.car_background.get());
    check(palette && original && original->ncolors == 256, "Read frontend palette");
    check(SDL_SetPaletteColors(palette, original->colors, 0, 256), "Copy frontend palette");
    const SDL_Color black{0, 0, 0, 255};
    check(SDL_SetPaletteColors(palette, &black, 0, 1) && SDL_SetPaletteColors(palette, &black, 155, 1), "Apply original frontend black palette entries");
    view.extras_icons[0] = make_extras_icon(palette, false);
    view.extras_icons[1] = make_extras_icon(palette, true);
    for (const auto symbol : {BadgeSymbol::fullscreen, BadgeSymbol::vsync, BadgeSymbol::fps, BadgeSymbol::camera, BadgeSymbol::unlocks}) {
        view.native_icons.push_back(make_badge_icon(palette, false, symbol));
        view.native_icons.push_back(make_badge_icon(palette, true, symbol));
    }
    // Keep the selected photograph separate from ROLL1, labels and selection
    // artwork. The renderer reflects only this corrected-palette image outward.
    check(SDL_SetSurfacePalette(view.car_background.get(), palette), "Share frontend backdrop palette");
    view.side_backdrop = view.renderer.upload(view.car_background.get());
    view.menus = load_menus(view.store);
    view.championship_presentation = load_championship_presentation(view.store);
    view.selections = load_selection_resources();
    view.tracks = load_track_resources();
    view.record_resources = load_record_resources();
    view.default_records = load_default_lap_records();
    view.menu_transition = load_menu_transition_resources();
    view.prepare_backdrop();
    std::vector<Surface> rolls;
    for (std::size_t phase = 0; phase < view.roll_backdrops.size(); ++phase) {
        rolls.push_back(decode_bmp(archive.member(4 + phase).bytes()));
    }
    view.prepare_roll_backdrops(decode_bmp(archive.member(car_member + 2).bytes()), rolls);
    view.ready = true;
}

const std::array<std::string, 5>& View::name_defaults() const {
    if (!impl_->ready) {
        throw std::logic_error("Frontend resources must load before name defaults are requested");
    }
    return impl_->menus.default_player_names;
}

gpu::Frame View::frame(const Controller& controller) {
    auto& view = *impl_;
    gpu::Frame frame;
    if (controller.screen() == Screen::title) {
        frame.quad({.texture = view.title.get()}, 0, 0, 640, 480);
        frame.non_4_3_image = gpu::Frame::CoverImage{view.title_wide.get(), view.title_width, view.title_height};
    } else if (controller.screen() == Screen::copyright) {
        frame.clear_color = {0, 0, 0, 1};
        frame.quad({.texture = view.loading.get()}, 0, 0, 640, 480);
    } else {
        if (!view.ready) {
            throw std::logic_error("Frontend resources must load on the copyright screen");
        }
        if (view.cached_screen != controller.screen() || view.cached_menu_entered_ns != controller.menu_entered_ns()) {
            const bool startup = !view.cached_screen;
            view.transition_running = rolling_menu(controller.screen()) && (startup ? controller.screen() == Screen::language : rolling_menu(*view.cached_screen));
            if (view.transition_running) {
                view.transition_started_ns = controller.menu_entered_ns();
                view.transition_startup = startup;
                view.transition_direction = startup ? 1 : (view.previous_exit_icon == 16 ? -1 : controller.menu_transition_direction());
                view.outgoing_content = std::move(view.previous_content);
                view.outgoing_overlay = std::move(view.font_overlay);
                view.outgoing_texture = std::move(view.content_texture);
                view.content_texture = view.renderer.texture(640, 480);
            }
        }
        const auto roll = view.transition_running
                              ? menu_transition_phase(controller.animation_time_ns() - view.transition_started_ns, view.transition_direction, view.transition_startup)
                              : MenuTransitionPhase{};
        view.transition_running = roll.active;
        if (!roll.active) {
            view.outgoing_content = {};
            view.outgoing_overlay.clear();
            view.outgoing_texture.reset();
        }
        std::optional<Menu> current;
        std::string content;
        if (controller.screen() == Screen::name_entry) {
            content = controller.player_name(controller.selection_player());
            content.push_back(controller.name_cursor_visible() ? '\1' : '\0');
            content += controller.cheats_error();
            content += controller.setup().mode == GameMode::linkup ? (controller.network_hosting() ? ":session" : ":player") : ":offline";
        } else if (controller.screen() == Screen::car_selection) {
            content = std::to_string(controller.car_id(controller.selection_player())) + (controller.transmission_manual(controller.selection_player()) ? ":manual" : ":auto");
            content += cheats::car_available(cheats::for_mode(controller.cheats_state(), static_cast<std::uint32_t>(controller.setup().mode)), 8) ? ":8" : "";
            content += cheats::car_available(cheats::for_mode(controller.cheats_state(), static_cast<std::uint32_t>(controller.setup().mode)), 9) ? ":9" : "";
            if (controller.setup().mode == GameMode::championship) {
                content += controller.championship_error();
            }
        } else if (controller.screen() == Screen::track_selection) {
            content = std::to_string(controller.track_id()) + ":" + std::to_string(controller.setup().weather);
            content += controller.track_available() ? ":open" : ":locked";
            const auto& record = (controller.records() ? *controller.records() : view.default_records).at(controller.track_id());
            content += std::to_string(record.lap_ms) + record.driver + std::to_string(record.car_id);
        } else if (controller.screen() == Screen::championship_honors) {
            const auto& honors = controller.championship_honors().value();
            content = std::to_string(honors.block) + ":" + std::to_string(honors.tier);
            for (const auto& line : championship_honors_lines(view.championship_presentation, honors, controller.language())) {
                content += ":" + line.text;
            }
            content += controller.championship_error();
        } else if (controller.screen() == Screen::championship_overview) {
            const auto& state = controller.championship_state().value();
            content = std::to_string(state.meeting) + ":" + std::to_string(state.race) + ":" + std::to_string(static_cast<unsigned>(state.phase));
            content += state.driver;
            content += controller.championship_error();
            for (std::size_t actor = 0; actor < state.points.size(); ++actor) {
                content += ":" + std::to_string(state.points[actor]) + ":" + state.drivers[actor].driver;
            }
        } else if (controller.screen() == Screen::championship_records) {
            content = std::to_string(controller.championship_record_color());
            content += controller.championship_records_error();
            const auto records =
                controller.championship_records() ? std::span<const ChampionshipRecord>(*controller.championship_records()) : std::span<const ChampionshipRecord>{};
            for (const auto& record : records) {
                content += ":" + record.driver + ":" + std::to_string(record.points) + ":" + std::to_string(record.round);
                for (const auto tier : record.awards) {
                    content += ":" + std::to_string(tier);
                }
            }
        } else if (controller.screen() == Screen::time_trial_records) {
            content = cheats::track_available(cheats::for_mode(controller.cheats_state(), static_cast<std::uint32_t>(controller.setup().mode)), 9) ? "lava" : "";
            content += controller.record_highlight_track() ? std::to_string(*controller.record_highlight_track()) : "none";
            for (const auto& record : controller.records() ? *controller.records() : view.default_records) {
                content += std::to_string(record.lap_ms) + ":" + record.driver + ":" + std::to_string(record.car_id) + ":" + record.code;
            }
        } else {
            current = menu_for(view.menus, controller);
            content = current->title;
            content += current->notice;
            content += current->status;
            for (const auto& detail : current->details) {
                content += ":" + detail;
            }
            for (const auto& line : current->lines) {
                content += ":" + line.name + ":" + line.car + ":" + line.status + ":" + line.markers + (line.committed ? ":ok" : ":selecting");
            }
            for (const auto& item : current->items) {
                content.push_back('\0');
                content += item.label;
                content += item.suffix;
                content.push_back('\0');
                content += item.help;
            }
        }
        const auto highlight = current ? controller.highlight_color() : std::uint8_t(0);
        const bool mega_fonts = controller.settings_values().graphics.detail == graphics_mega_detail;
        if (view.cached_screen != controller.screen() || view.cached_menu_entered_ns != controller.menu_entered_ns() || view.cached_selection != controller.selection()
            || view.cached_language != controller.language() || view.cached_color != highlight || view.cached_content != content || view.mega_fonts != mega_fonts) {
            view.mega_fonts = mega_fonts;
            view.white_font_overlay = controller.screen() == Screen::championship_honors;
            view.font_overlay.clear();
            for (auto& colors : view.font_colors) {
                for (auto& [key, texture] : colors) {
                    texture.used = false;
                }
            }
            if (controller.screen() == Screen::name_entry) {
                view.render_name(controller);
            } else if (controller.screen() == Screen::car_selection) {
                view.render_car(controller);
            } else if (controller.screen() == Screen::track_selection) {
                view.render_track(controller);
            } else if (controller.screen() == Screen::championship_honors) {
                view.render_championship_honors(controller);
            } else if (controller.screen() == Screen::championship_save_prompt || controller.screen() == Screen::championship_save_overwrite
                       || controller.screen() == Screen::championship_save_feedback) {
                view.render_championship_dialog(controller, *current);
            } else if (controller.screen() == Screen::championship_overview) {
                view.render_championship(controller);
            } else if (controller.screen() == Screen::time_trial_records) {
                view.render_records(controller);
            } else if (controller.screen() == Screen::championship_records) {
                view.render_championship_records(controller);
            } else {
                view.render_menu(controller, *current);
            }
            // Keep only colors used by this canvas. Animated menu highlights
            // tint shared OPTION/HELP textures; game-font edge colors stay fixed.
            for (std::size_t index = 4; !view.transition_running && index < view.font_colors.size(); ++index) {
                std::erase_if(view.font_colors[index], [](const auto& entry) {
                    return !entry.second.used;
                });
            }
            view.cached_screen = controller.screen();
            view.cached_menu_entered_ns = controller.menu_entered_ns();
            view.cached_selection = controller.selection();
            view.cached_language = controller.language();
            view.cached_color = highlight;
            view.cached_content = std::move(content);
        }
        if (controller.screen() == Screen::championship_honors) {
            frame.quad({.texture = view.honors_background.get()}, 0, 0, 640, 480);
        } else if (view.mega_fonts) {
            view.load_mega_menu_artwork();
            frame.quad({.texture = view.mega_frame.get(), .blend = true, .linear = true, .clamp = true}, 0, 0, 640, 480);
        }
        frame.quad({.texture = view.menu_texture.get(), .key = view.mega_fonts || controller.screen() == Screen::championship_honors}, 0, 0, 640, 480);
        // Fonts stay in GPU quads so their new outlines are not rasterized back
        // into the original 640x480 indexed menu canvas. Later canvas sprites
        // retain their original ordering over text, including badges/cursors.
        view.painter.append_overlays(frame);
        if (controller.screen() == Screen::championship_honors) {
            frame.side_backdrop = view.honors_background.get();
        } else if (view.mega_fonts) {
            frame.background_image = gpu::Frame::CoverImage{view.mega_backdrop.get(), view.mega_backdrop_width, view.mega_backdrop_height};
        } else {
            frame.side_backdrop = view.side_backdrop.get();
        }
        if (controller.screen() == Screen::track_selection) {
            view.track_preview->append(frame, controller.track_preview_presentation_rotation(), controller.track_available());
        }
        if (rolling_menu(controller.screen())) {
            view.previous_content = frame;
            view.previous_content.side_backdrop = nullptr;
            view.previous_content.background_image.reset();
            std::erase_if(view.previous_content.draws, [&](const auto& draw) {
                return view.mega_frame && draw.state.texture == view.mega_frame.get();
            });
            for (auto& draw : view.previous_content.draws) {
                if (draw.state.texture == view.menu_texture.get()) {
                    draw.state.texture = view.content_texture.get();
                    draw.state.key = true;
                }
            }
            view.previous_exit_icon = current && controller.selection() < current->items.size() ? current->items[controller.selection()].icon : 0;
        }
        if (roll.active) {
            gpu::Frame animated;
            animated.side_backdrop = frame.side_backdrop;
            animated.background_image = frame.background_image;
            if (view.mega_fonts) {
                gpu::Frame metal;
                metal.quad({.texture = view.mega_frame.get(), .blend = true, .linear = true, .clamp = true}, 0, 0, 640, 480);
                // The generated metal keeps its own artwork. Extend the same
                // recovered content-plane movement over its outer rails.
                auto metal_quad = view.menu_transition.frames[roll.frame][std::size_t(MenuRegion::content)];
                const auto& content_region = menu_transition_regions[std::size_t(MenuRegion::content)];
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    const float x = corner >= 2 ? 640.f : 0.f, y = corner == 1 || corner == 2 ? 480.f : 0.f;
                    metal_quad.corners[corner] = menu_transition_point(view.menu_transition.frames[roll.frame][std::size_t(MenuRegion::content)],
                        (x - content_region.x) / content_region.width, (y - content_region.y) / content_region.height);
                }
                roll_region(animated, metal, {0, 0, 640, 480}, metal_quad);
            } else {
                animated.quad({.texture = view.roll_backdrops[roll.frame].get()}, 0, 0, 640, 480);
            }
            if (roll.frame != 6 && !(view.transition_startup && roll.frame < 6)) {
                const bool outgoing = roll.frame == 0 || (view.transition_direction == 1 ? roll.frame < 6 : roll.frame >= 6);
                const auto& snapshot = outgoing && !view.transition_startup ? view.outgoing_content : view.previous_content;
                for (std::size_t region = 0; region < menu_transition_regions.size(); ++region) {
                    // Mega's title and help are printed on its generated metal
                    // frame, so they share that frame's content-plane motion.
                    // Original ROLL artwork has independent title/help faces.
                    const auto plane = view.mega_fonts ? std::size_t(MenuRegion::content) : region;
                    roll_region(animated, snapshot, menu_transition_regions[region], view.menu_transition.frames[roll.frame][plane],
                        view.mega_fonts ? &menu_transition_regions[std::size_t(MenuRegion::content)] : nullptr);
                }
            }
            return animated;
        }
    }
    return frame;
}
} // namespace toca::frontend
