#pragma once

// Pixel assertions for the GPU regression checks; included in the test namespace.
std::array<Vertex, 4> perspective_vertices() {
    return {{{19.5f, 19.5f, .5f, 1, 0xffffffff, 0, 0, 0}, {179.5f, 19.5f, .5f, .25f, 0xffffffff, 0, 1, 0}, {179.5f, 179.5f, .5f, .25f, 0xffffffff, 0, 1, 1},
        {19.5f, 179.5f, .5f, 1, 0xffffffff, 0, 0, 1}}};
}

void expect(const std::vector<std::uint32_t>& pixels, int x, int y, std::uint32_t wanted, const char* feature, int tolerance = 0) {
    const auto actual = pixels.at(y * width + x);
    for (const int shift : {0, 8, 16}) {
        const int delta = int((actual >> shift) & 255) - int((wanted >> shift) & 255);
        if (delta < -tolerance || delta > tolerance) {
            throw std::runtime_error(std::string(feature) + " pixel mismatch at " + std::to_string(x) + "," + std::to_string(y));
        }
    }
}

void verify_checks(const std::vector<std::uint32_t>& pixels) {
    expect(pixels, 50, 50, background, "color key");
    expect(pixels, 150, 50, 0x00c800, "palette green");
    expect(pixels, 50, 150, 0x0000c8, "palette blue");
    expect(pixels, 150, 150, 0xf0f0f0, "palette white");
    expect(pixels, 300, 100, 0x00c800, "depth ordering");
    expect(pixels, 500, 100, 0x640064, "alpha blend", 1);
    expect(pixels, 50, 300, 0x0000c8, "full fog");
    expect(pixels, 150, 300, 0x640064, "partial fog", 1);
    expect(pixels, 300, 300, 0xc80000, "no fog");
    expect(pixels, 500, 330, 0xf0c800, "indexed triangle");
    expect(pixels, 430, 250, background, "triangle boundary");
    std::cout << "PASS: palette, color key, depth, blending, fog, indexed TL vertices\n";
}

void verify_perspective(const std::vector<std::uint32_t>& pixels) {
    // u = (t * .25) / ((1-t) + t*.25), so u=.5 occurs at t=.8, not .5.
    expect(pixels, 140, 50, 0xff00ff, "perspective-correct UV before boundary");
    expect(pixels, 170, 50, 0x00c800, "perspective-correct UV after boundary");
    std::cout << "PASS: nonuniform reciprocal W preserves perspective-correct UVs\n";
}

void screenshot(std::vector<std::uint32_t>& pixels, const std::string& path) {
    Surface surface(SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_XRGB8888, pixels.data(), width * 4), SDL_DestroySurface);
    sdl_check(bool(surface), "Create screenshot");
    sdl_check(SDL_SavePNG(surface.get(), path.c_str()), "Save screenshot");
    std::cout << "Screenshot: " << path << '\n';
}

struct Options {
    std::string image, data_root, screenshot;
};
