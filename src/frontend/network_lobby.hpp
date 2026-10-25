#pragma once

#include "core/contracts.hpp"
#include "frontend/menus.hpp"
#include "network/session.hpp"

#include <optional>
#include <string>

namespace toca::frontend {
enum class NetworkLobbyCommandKind {
    host,
    join,
    leave,
    select_car,
    ready,
    set_settings,
    start
};

struct NetworkLobbyCommand {
    NetworkLobbyCommandKind kind = NetworkLobbyCommandKind::leave;
    std::string endpoint{}, name{};
    std::uint8_t car = 0;
    bool manual = false, ready = false;
    network::LobbySettings settings{};
};

struct NetworkLobbyResult {
    std::optional<std::uint32_t> cue;
    std::optional<NetworkLobbyCommand> command;
    bool back = false, enter_name = false, withdraw = false;
};

// Native endpoint setup replaces the DirectPlay provider/session browser. Name,
// options, garage and track selection use the ordinary source frontend screens.
class NetworkLobby {
    enum class Page {
        choose,
        host,
        join,
        connecting,
        waiting,
        error
    };
    Page page_ = Page::choose;
    std::size_t selection_ = 0;
    std::string host_port_ = "27015", join_endpoint_, error_;
    bool hosting_ = true, edited_ = false;

public:
    void reset();
    NetworkLobbyResult update(const InputSnapshot& input, const network::SessionView& view);
    Menu menu(std::uint32_t language, const network::SessionView& view, const NetworkLabels* labels = nullptr) const;
    NetworkLobbyCommand connect(std::string name, std::uint8_t car, bool manual);
    void show_waiting();
    void show_endpoint();
    void set_error(std::string error);
    std::size_t selection() const;
    bool hosting() const;
    bool entering_text(const network::SessionView& view) const;
    bool item_available(std::size_t index, const network::SessionView& view) const;
};
} // namespace toca::frontend
