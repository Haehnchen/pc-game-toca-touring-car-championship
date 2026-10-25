#pragma once

#include "app/options.hpp"
#include "assets/store.hpp"
#include "network/session.hpp"

#include <fstream>

namespace toca {
// The common application services this independently of focus and simulation time.
class NetworkRuntime {
    const AssetStore& assets_;
    std::optional<std::uint32_t> content_;
    network::Session session_;
    std::ofstream trace_;
    std::uint64_t next_trace_ = 0;
    std::uint64_t load_service_calls_ = 0;
    network::MonotonicMillis last_load_service_ = 0, max_load_service_gap_ = 0;
    network::Hello identity(std::string name, std::uint8_t car, bool manual, std::uint8_t sensitivity);

public:
    NetworkRuntime(const AssetStore& assets, const Options& options);
    void host(std::uint16_t port, std::string name, std::uint8_t car = 0, bool manual = false, std::uint8_t sensitivity = 2);
    void join(std::string_view address, std::string name, std::uint8_t car = 0, bool manual = false, std::uint8_t sensitivity = 2);
    void pump();
    // Safe for AssetStore loading callbacks: services only network/trace state.
    // Host departure publishes failed state; it never touches scene/frontend.
    void pump_load();
    void wait_until(std::uint64_t deadline_ns);
    void shutdown();
    network::Session& session();
};
} // namespace toca
