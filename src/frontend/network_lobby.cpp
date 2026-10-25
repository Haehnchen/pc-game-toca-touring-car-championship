#include "frontend/network_lobby.hpp"

#include "frontend/network_status.hpp"

#include <algorithm>
#include <array>

namespace toca::frontend {
namespace {
struct EndpointLabels {
    const char *port, *address, *connecting, *help;
};

constexpr std::array<EndpointLabels, 5> endpoint_labels{{
    {"PORT", "ADDRESS", "CONNECTING", "TYPE TO EDIT, BACKSPACE TO DELETE"},
    {"PORT", "ADRESSE", "CONNEXION", "SAISIR, RETOUR ARRIERE POUR EFFACER"},
    {"PORT", "ADRESSE", "VERBINDUNG", "EINGEBEN, RUECKTASTE ZUM LOESCHEN"},
    {"PORTA", "INDIRIZZO", "CONNESSIONE", "SCRIVI, BACKSPACE PER CANCELLARE"},
    {"PUERTO", "DIRECCION", "CONECTANDO", "ESCRIBIR, RETROCESO PARA BORRAR"},
}};
constexpr std::array<std::string_view, 10> cars{"HONDA", "AUDI", "VAUXHALL", "VOLVO", "FORD", "NISSAN", "PEUGEOT", "RENAULT", "TANK", "FLEX"};

bool pressed(const InputSnapshot& input, Action action) {
    return (input.pressed & action_mask(action)) != 0;
}
} // namespace

void NetworkLobby::reset() {
    page_ = Page::choose;
    selection_ = 0;
    error_.clear();
    edited_ = false;
}

NetworkLobbyResult NetworkLobby::update(const InputSnapshot& input, const network::SessionView& view) {
    NetworkLobbyResult result;
    if (!input.focused) {
        return result;
    }
    const auto open = [&](Page page, std::size_t selection = 0) {
        page_ = page;
        selection_ = selection;
        edited_ = false;
        error_.clear();
        result.cue = 2;
    };
    if (pressed(input, Action::back)) {
        if (page_ == Page::choose) {
            result.back = true;
            result.cue = 2;
        } else if (page_ == Page::waiting) {
            result.withdraw = true;
            result.cue = 2;
        } else if (page_ == Page::connecting || page_ == Page::error) {
            result.command = NetworkLobbyCommand{.kind = NetworkLobbyCommandKind::leave};
            open(Page::choose);
        } else {
            open(Page::choose);
        }
        return result;
    }
    // SDL may coalesce the last typed character with navigation in one frame.
    // Apply text to its current field before consuming Down/Enter.
    if (entering_text(view)) {
        auto& field = hosting_ ? host_port_ : join_endpoint_;
        if (pressed(input, Action::backspace) && !field.empty()) {
            field.pop_back();
            edited_ = true;
            result.cue = 3;
        }
        for (const unsigned char ch : input.text_input) {
            const bool allowed = (ch >= '0' && ch <= '9') || (!hosting_ && (ch == '.' || ch == ':'));
            const auto limit = hosting_ ? 5U : 21U;
            if (allowed && (!edited_ || field.size() < limit)) {
                if (!edited_) {
                    field.clear();
                    edited_ = true;
                }
                field.push_back(char(ch));
                result.cue = 3;
            }
        }
    }
    const auto count = menu(0, view).items.size();
    selection_ = std::min(selection_, count - 1);
    if ((pressed(input, Action::up) || pressed(input, Action::down)) && page_ != Page::waiting) {
        selection_ = (selection_ + (pressed(input, Action::up) ? count - 1 : 1)) % count;
        result.cue = 0;
    } else if (pressed(input, Action::confirm)) {
        if (page_ == Page::choose) {
            if (selection_ == 2) {
                result.back = true;
                result.cue = 2;
            } else {
                hosting_ = selection_ == 0;
                open(hosting_ ? Page::host : Page::join);
            }
        } else if (page_ == Page::host || page_ == Page::join) {
            if (selection_ == 2) {
                open(Page::choose);
            } else if (selection_ == 1) {
                if ((hosting_ ? host_port_ : join_endpoint_).empty()) {
                    error_ = hosting_ ? "Port must be between 1 and 65535" : "Enter an IPv4 address and port";
                    selection_ = 0;
                    result.cue = 4;
                } else {
                    result.enter_name = true;
                    result.cue = 2;
                }
            }
        } else if (page_ == Page::waiting) {
            if (!hosting_) {
                return result;
            }
            if (item_available(selection_, view)) {
                result.command = NetworkLobbyCommand{.kind = NetworkLobbyCommandKind::start};
                result.cue = 2;
            } else {
                result.cue = 4;
            }
        } else {
            result.command = NetworkLobbyCommand{.kind = NetworkLobbyCommandKind::leave};
            open(Page::choose);
        }
    }
    return result;
}

Menu NetworkLobby::menu(std::uint32_t language, const network::SessionView& view, const NetworkLabels* original) const {
    const auto& endpoint = endpoint_labels.at(std::min(language, 4U));
    const NetworkLabels fallback{.title = "LINKUP GAME",
        .host = "HOST A NEW GAME",
        .join = "JOIN GAME",
        .back = "BACK",
        .continue_label = "CONTINUE",
        .waiting_room = "WAITING ROOM",
        .start = "START RACE",
        .track = "TRACK",
        .ok = "OK"};
    const auto& text = original ? *original : fallback;
    Menu result;
    result.title = text.title;
    result.notice = network_error_text(language, !error_.empty() ? error_ : view.error, original);
    const auto row = [&](std::string label, std::string help = {}, std::uint32_t icon = 20) {
        result.items.push_back({std::move(label), std::move(help), icon});
    };
    if (page_ == Page::choose) {
        row(text.host, text.host_help, text.host_icon);
        row(text.join, text.join_help, text.join_icon);
        row(text.back, {}, 16);
    } else if (page_ == Page::host || page_ == Page::join) {
        row(std::string(hosting_ ? endpoint.port : endpoint.address) + " : " + (hosting_ ? host_port_ : join_endpoint_), endpoint.help);
        if (!hosting_) {
            result.notice = result.notice.empty() ? network_endpoint_help(language) : result.notice;
        }
        row(text.continue_label, {}, 29);
        row(text.back, {}, 16);
    } else if (page_ == Page::waiting) {
        result.title = text.waiting_room;
        result.waiting_room = true;
        for (const auto& player : view.roster.players) {
            std::string markers;
            if (player.player == view.host_player) {
                markers = network_host_marker(language);
            }
            if (player.player == view.local_player) {
                markers += (markers.empty() ? "" : "/") + network_local_marker(language);
            }
            result.lines.push_back(
                {player.name, player.ready ? std::string(cars.at(std::min<std::uint8_t>(player.car, 9))) : "", player.ready ? text.ok : "", player.ready, markers});
        }
        result.details = network_lobby_status(language, view, original);
        // 46731c consumes host Confirm and Escape; roster rows and Back are
        // not a second navigable menu. Keep one inert row for generic caching.
        row(hosting_ ? text.start : std::string{}, text.back, 3);
    } else {
        result.title = page_ == Page::connecting ? endpoint.connecting : text.title;
        row(text.back, {}, 16);
    }
    return result;
}

NetworkLobbyCommand NetworkLobby::connect(std::string name, std::uint8_t car, bool manual) {
    page_ = Page::connecting;
    selection_ = 0;
    error_.clear();
    return {.kind = hosting_ ? NetworkLobbyCommandKind::host : NetworkLobbyCommandKind::join,
        .endpoint = hosting_ ? host_port_ : join_endpoint_,
        .name = std::move(name),
        .car = car,
        .manual = manual};
}

void NetworkLobby::show_waiting() {
    page_ = Page::waiting;
    selection_ = 0;
    error_.clear();
}

void NetworkLobby::show_endpoint() {
    page_ = hosting_ ? Page::host : Page::join;
    selection_ = 1;
    edited_ = false;
    error_.clear();
}

void NetworkLobby::set_error(std::string error) {
    error_ = std::move(error);
    page_ = Page::error;
    selection_ = 0;
}

std::size_t NetworkLobby::selection() const {
    return selection_;
}

bool NetworkLobby::hosting() const {
    return hosting_;
}

bool NetworkLobby::entering_text(const network::SessionView&) const {
    return (page_ == Page::host || page_ == Page::join) && selection_ == 0;
}

bool NetworkLobby::item_available(std::size_t index, const network::SessionView& view) const {
    if (page_ != Page::waiting || index != 0) {
        return true;
    }
    if (!hosting_) {
        return false;
    }
    return view.phase == network::SessionPhase::lobby && std::any_of(view.roster.players.begin(), view.roster.players.end(), [&](const auto& player) {
        return player.player == view.local_player && player.ready;
    });
}
} // namespace toca::frontend
