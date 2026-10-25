#pragma once

#include <SDL3/SDL.h>

#include <utility>

namespace toca::gpu::detail {
struct CommandOperations {
    static void end(SDL_GPUCopyPass* pass) {
        SDL_EndGPUCopyPass(pass);
    }

    static void end(SDL_GPURenderPass* pass) {
        SDL_EndGPURenderPass(pass);
    }

    static bool cancel(SDL_GPUCommandBuffer* commands) {
        return SDL_CancelGPUCommandBuffer(commands);
    }

    static bool submit(SDL_GPUCommandBuffer* commands) {
        return SDL_SubmitGPUCommandBuffer(commands);
    }

    static SDL_GPUFence* submit_fenced(SDL_GPUCommandBuffer* commands) {
        return SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    }
};

// SDL consumes a submission even when it fails. A real acquired swapchain must
// be submitted rather than cancelled, including while unwinding an exception.
template <class Operations = CommandOperations> class CommandScope {
    SDL_GPUCommandBuffer* commands_;
    SDL_GPUCopyPass* copy_ = nullptr;
    SDL_GPURenderPass* render_ = nullptr;
    bool swapchain_ = false;

public:
    explicit CommandScope(SDL_GPUCommandBuffer* commands) : commands_(commands) {}

    CommandScope(const CommandScope&) = delete;
    CommandScope& operator=(const CommandScope&) = delete;

    ~CommandScope() {
        end_copy();
        end_render();
        if (commands_) {
            if (swapchain_) {
                Operations::submit(commands_);
            } else {
                Operations::cancel(commands_);
            }
        }
    }

    SDL_GPUCommandBuffer* get() const {
        return commands_;
    }

    void acquired_swapchain() {
        swapchain_ = true;
    }

    SDL_GPUCopyPass* copy(SDL_GPUCopyPass* pass) {
        return copy_ = pass;
    }

    SDL_GPURenderPass* render(SDL_GPURenderPass* pass) {
        return render_ = pass;
    }

    void end_copy() {
        if (copy_) {
            Operations::end(std::exchange(copy_, nullptr));
        }
    }

    void end_render() {
        if (render_) {
            Operations::end(std::exchange(render_, nullptr));
        }
    }

    bool submit() {
        return Operations::submit(std::exchange(commands_, nullptr));
    }

    SDL_GPUFence* submit_fenced() {
        return Operations::submit_fenced(std::exchange(commands_, nullptr));
    }
};

struct TransferOperations {
    static void* map(SDL_GPUDevice* device, SDL_GPUTransferBuffer* buffer, bool cycle) {
        return SDL_MapGPUTransferBuffer(device, buffer, cycle);
    }

    static void unmap(SDL_GPUDevice* device, SDL_GPUTransferBuffer* buffer) {
        SDL_UnmapGPUTransferBuffer(device, buffer);
    }
};

template <class Operations = TransferOperations> class TransferMapping {
    SDL_GPUDevice* device_;
    SDL_GPUTransferBuffer* buffer_;
    void* data_;

public:
    TransferMapping(SDL_GPUDevice* device, SDL_GPUTransferBuffer* buffer, bool cycle) : device_(device), buffer_(buffer), data_(Operations::map(device, buffer, cycle)) {}

    TransferMapping(const TransferMapping&) = delete;
    TransferMapping& operator=(const TransferMapping&) = delete;

    ~TransferMapping() {
        if (data_) {
            Operations::unmap(device_, buffer_);
        }
    }

    void* get() const {
        return data_;
    }
};

template <class Resource, class First, class Second> void replace_pair(Resource& first, Resource& second, First create_first, Second create_second) {
    auto next_first = create_first();
    auto next_second = create_second();
    first = std::move(next_first);
    second = std::move(next_second);
}
} // namespace toca::gpu::detail
