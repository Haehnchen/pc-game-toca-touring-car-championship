#pragma once

#include "frontend/menus.hpp"
#include "network/session.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace toca::frontend {
// Native connection/status wording. Canonical dialogs and weather captions are
// supplied by Menus; names always come from the current authoritative roster.
std::string network_error_text(std::uint32_t language, std::string_view error, const NetworkLabels* original = nullptr);
std::string network_endpoint_help(std::uint32_t language);
std::string network_local_marker(std::uint32_t language);
std::string network_host_marker(std::uint32_t language);
std::vector<std::string> network_lobby_status(std::uint32_t language, const network::SessionView&, const NetworkLabels* original = nullptr);
std::vector<std::string> network_loading_status(std::uint32_t language, const network::SessionView&, const network::RaceView*);
std::string network_pause_notice(std::uint32_t language, const network::SessionView&, const network::RaceView*);
} // namespace toca::frontend
