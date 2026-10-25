#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace toca::gpu {
struct BindingCounts {
    Uint64 pipelines = 0, viewports = 0, scissors = 0;
    Uint64 vertex_uniforms = 0, fragment_uniforms = 0;
    Uint64 storage_buffers = 0, samplers = 0;
    bool operator==(const BindingCounts&) const = default;
};

// Own the cached bindings for exactly one SDL render pass. SDL preserves its
// logical resource/uniform bindings across pipeline changes, then unsets all
// graphics bindings when the pass ends. Background draws use this same cache.
class PassBindings {
    struct Uniform {
        std::array<Uint8, 256> bytes{};
        std::size_t size = 0;

        template <class T> bool changed(const T& value) {
            static_assert(std::is_trivially_copyable_v<T> && sizeof(T) <= sizeof(bytes));
            if (size == sizeof(T) && std::memcmp(bytes.data(), &value, sizeof(T)) == 0) {
                return false;
            }
            std::memcpy(bytes.data(), &value, sizeof(T));
            size = sizeof(T);
            return true;
        }
    };

    SDL_GPUCommandBuffer* command_;
    SDL_GPURenderPass* pass_;
    BindingCounts& counts_;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUViewport viewport_{};
    SDL_Rect scissor_{};
    bool viewport_set_ = false, scissor_set_ = false, storage_set_ = false;
    std::array<SDL_GPUBuffer*, 4> storage_{};
    SDL_GPUTextureSamplerBinding sampler_{};
    std::array<Uniform, 2> vertex_uniforms_;
    Uniform fragment_uniform_;

public:
    PassBindings(SDL_GPUCommandBuffer* command, SDL_GPURenderPass* pass, BindingCounts& counts) : command_(command), pass_(pass), counts_(counts) {}

    void viewport(const SDL_GPUViewport& value) {
        if (!viewport_set_ || viewport_.x != value.x || viewport_.y != value.y || viewport_.w != value.w || viewport_.h != value.h || viewport_.min_depth != value.min_depth
            || viewport_.max_depth != value.max_depth) {
            SDL_SetGPUViewport(pass_, &value);
            viewport_ = value;
            viewport_set_ = true;
            ++counts_.viewports;
        }
    }

    void scissor(const SDL_Rect& value) {
        if (!scissor_set_ || scissor_.x != value.x || scissor_.y != value.y || scissor_.w != value.w || scissor_.h != value.h) {
            SDL_SetGPUScissor(pass_, &value);
            scissor_ = value;
            scissor_set_ = true;
            ++counts_.scissors;
        }
    }

    void pipeline(SDL_GPUGraphicsPipeline* value) {
        if (pipeline_ != value) {
            SDL_BindGPUGraphicsPipeline(pass_, value);
            pipeline_ = value;
            ++counts_.pipelines;
        }
    }

    template <class T> void vertex_uniform(Uint32 slot, const T& value) {
        if (slot >= vertex_uniforms_.size()) {
            throw std::logic_error("Render pass vertex uniform slot exceeds its shader contract");
        }
        // Length is part of the key: TL dimensions and resident projection
        // occupy the same slot with different layouts.
        if (vertex_uniforms_[slot].changed(value)) {
            SDL_PushGPUVertexUniformData(command_, slot, &value, sizeof(T));
            ++counts_.vertex_uniforms;
        }
    }

    template <class T> void fragment_uniform(const T& value) {
        if (fragment_uniform_.changed(value)) {
            SDL_PushGPUFragmentUniformData(command_, 0, &value, sizeof(T));
            ++counts_.fragment_uniforms;
        }
    }

    void storage(const std::array<SDL_GPUBuffer*, 4>& value) {
        if (!storage_set_ || storage_ != value) {
            SDL_BindGPUVertexStorageBuffers(pass_, 0, value.data(), Uint32(value.size()));
            storage_ = value;
            storage_set_ = true;
            ++counts_.storage_buffers;
        }
    }

    void sampler(const SDL_GPUTextureSamplerBinding& value) {
        if (sampler_.texture != value.texture || sampler_.sampler != value.sampler) {
            SDL_BindGPUFragmentSamplers(pass_, 0, &value, 1);
            sampler_ = value;
            ++counts_.samplers;
        }
    }
};
} // namespace toca::gpu
