//Test-only responsive peer which intentionally never completes resource loading.
//The real application receives no artificial loading-delay flag or test hooks.
#include "assets/store.hpp"
#include "network/content.hpp"
#include "network/session.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
volatile std::sig_atomic_t stopped = 0;

void stop(int) {
    stopped = 1;
}
} // namespace

int main(int argc, char** argv) {
    using namespace toca::network;
    try {
        if (argc != 4) {
            throw std::invalid_argument("Usage: loading_peer DATA_ROOT IP:PORT NAME");
        }
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        const toca::AssetStore assets(argv[1]);
        Session session;
        session.join(parse_address(argv[2]), {simulation_content_crc32(assets), argv[3], 0, false});
        auto previous = SessionPhase::idle;
        bool committed = false;
        while (!stopped) {
            session.pump(monotonic_millis());
            const auto& view = session.view();
            if (view.phase != previous) {
                std::cout << "phase=" << phase_name(view.phase) << " player=" << view.local_player << " players=" << view.roster.players.size() << " error=" << view.error
                          << std::endl;
                previous = view.phase;
            }
            if (view.phase == SessionPhase::failed) {
                break;
            }
            if (view.phase == SessionPhase::lobby && !committed) {
                if (!session.ready(true)) {
                    throw std::runtime_error("Loading fixture could not commit its car");
                }
                committed = true;
            }
            //In loading we continue ordinary ENet service, statuses and ACKs,
            //but deliberately never call Session::loaded().
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (session.view().phase != SessionPhase::failed) {
            session.leave();
        }
        const auto deadline = monotonic_millis() + 2000;
        while (session.draining() && monotonic_millis() < deadline) {
            session.pump(monotonic_millis());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
