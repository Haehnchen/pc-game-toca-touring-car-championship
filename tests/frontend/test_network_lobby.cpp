#include "frontend/controller.hpp"
#include "frontend/network_lobby.hpp"
#include "frontend/network_status.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

using namespace toca;
using namespace toca::frontend;

void endpoint_and_waiting() {
    NetworkLobby lobby;
    network::SessionView view;
    const auto press = [&](Action action) {
        return lobby.update({.pressed = action_mask(action), .text_input = {}}, view);
    };
    expect(lobby.menu(0, view).items.size() == 3, "Native Linkup entry exposes host, join and Back");
    press(Action::confirm);
    expect(lobby.entering_text(view) && lobby.menu(0, view).items.size() == 3, "Endpoint setup precedes the shared name editor");
    lobby.update({.pressed = action_mask(Action::down), .text_input = "27123"}, view);
    expect(lobby.selection() == 1 && lobby.menu(0, view).items[0].label == "PORT : 27123", "Coalesced endpoint text applies before Down");
    expect(press(Action::confirm).enter_name, "Endpoint Continue opens the shared editor without creating a player");
    auto command = lobby.connect("SESSION", 1, true);
    expect(command.kind == NetworkLobbyCommandKind::host && command.endpoint == "27123" && command.name == "SESSION", "Host editor confirms one shared session/player identity");
    view.phase = network::SessionPhase::lobby;
    view.hosting = true;
    view.local_player = view.host_player = 1;
    for (std::uint32_t id = 1; id <= 8; ++id) {
        view.roster.players.push_back({id, "DRIVER " + std::to_string(id), std::uint8_t(id - 1), false, id == 1});
    }
    lobby.show_waiting();
    const auto waiting = lobby.menu(0, view);
    expect(waiting.waiting_room && waiting.lines.size() == 8 && waiting.items.size() == 1 && waiting.lines[0].status == "OK" && waiting.lines[1].car.empty(),
        "One Waiting Room shows all eight names, committed cars/status and host Start without selectable roster rows");
    expect(lobby.item_available(0, view) && press(Action::confirm).command->kind == NetworkLobbyCommandKind::start,
        "Committed host may start while another member is still selecting");
    expect(press(Action::back).withdraw, "Waiting Room Back withdraws commitment instead of leaving the session");
    lobby.set_error("Race started without you");
    expect(lobby.menu(0, view).notice == network_error_text(0, "Race started without you") && press(Action::back).command->kind == NetworkLobbyCommandKind::leave,
        "Selection failures remain visible until Back closes the session and returns to the chooser");
    press(Action::down);
    press(Action::confirm);
    lobby.update({.text_input = "192.168.1.20:27015"}, view);
    press(Action::down);
    expect(press(Action::confirm).enter_name && !lobby.hosting(), "Join endpoint leads to the player editor");
    command = lobby.connect("CLIENT", 2, false);
    expect(command.kind == NetworkLobbyCommandKind::join && command.endpoint == "192.168.1.20:27015", "Join uses the edited native IPv4 endpoint");
    view.hosting = false;
    view.local_player = 2;
    lobby.show_waiting();
    const auto client = lobby.menu(0, view);
    expect(client.items.size() == 1 && client.items[0].label.empty() && client.details.size() == 2 && !press(Action::confirm).withdraw && press(Action::back).withdraw,
        "Client Waiting Room displays host track, ignores Confirm and withdraws only on Escape");
}

struct Frontend {
    Controller controller;

    Frontend(bool host) {
        controller.set_name_defaults({"PLAYER 1", "PILOTE 1", "FAHRER 1", "PILOTA 1", "PILOTO 1"});
        controller.update({}, {0, 5'000'000'000});
        controller.frontend_ready();
        press(Action::confirm);
        press(Action::confirm);
        for (unsigned i = 0; i < 3; ++i) {
            press(Action::down);
        }
        press(Action::confirm);
        if (!host) {
            press(Action::down);
        }
        press(Action::confirm);
        expect(controller.screen() == Screen::network_linkup && controller.network_text_input(), "Linkup opens native endpoint setup first");
        if (!host) {
            expect(controller.network_menu(Menus{}).items[0].label == "ADDRESS : ", "Join must not prefill a loopback address");
            press(Action::down);
            press(Action::confirm);
            expect(controller.screen() == Screen::network_linkup && controller.selection() == 0 && !controller.network_menu(Menus{}).notice.empty(),
                "An empty join address must remain editable with host IP:port guidance");
            text("127.0.0.1:27015");
        }
        press(Action::down);
        press(Action::confirm);
        expect(controller.screen() == Screen::name_entry && controller.network_hosting() == host, "Host and client share the ordinary name editor");
    }

    void press(Action action) {
        controller.update({.pressed = action_mask(action), .text_input = {}}, {0, 1});
    }

    void text(const char* name) {
        controller.update({.text_input = name}, {0, 1});
    }

    std::optional<NetworkLobbyCommand> take() {
        return controller.take_network_request();
    }
};

void host_route() {
    Frontend f(true);
    f.text("host name123");
    f.press(Action::confirm);
    auto command = f.take();
    expect(command && command->kind == NetworkLobbyCommandKind::host && command->name == "HOST NAME" && !f.take(),
        "Shared name rules preserve nine uppercase characters and emit one host command");
    network::SessionView view;
    view.phase = network::SessionPhase::lobby;
    view.hosting = true;
    view.local_player = view.host_player = 1;
    view.roster = {1, {{1, "HOST NAME", 0, false, false}, {2, "SELECTING", 2, false, false}}};
    view.settings.revision = 1;
    f.controller.set_network_view(view);
    expect(f.controller.screen() == Screen::network_options && f.controller.selection() == 4, "Host connection enters original five options on Continue");
    for (unsigned i = 0; i < 4; ++i) {
        f.press(Action::up);
    }
    f.press(Action::left);
    f.press(Action::left);
    expect(f.controller.setup().laps == 1, "Linkup laps use source1,2,3,5,10,15,20 order");
    f.press(Action::down);
    f.press(Action::right);
    f.press(Action::down);
    f.press(Action::left);
    f.press(Action::down);
    f.press(Action::right);
    f.press(Action::down);
    f.press(Action::confirm);
    expect(f.controller.screen() == Screen::car_selection && f.controller.setup().weather == 1 && !f.controller.setup().car_boost && f.controller.network_ai_enabled(),
        "Host options preserve weather, catch-up Car Boost and optional computer cars before the illustrated garage");
    f.press(Action::right);
    f.press(Action::down);
    f.press(Action::back);
    expect(f.controller.screen() == Screen::network_options && f.controller.selection() == 4, "Host garage Back returns to original Linkup options");
    f.press(Action::confirm);
    expect(f.controller.car_id() == 0 && f.controller.transmission_manual(), "Garage reentry discards uncommitted car browsing and retains independent gearbox");
    f.press(Action::right);
    f.press(Action::confirm);
    expect(f.controller.screen() == Screen::track_selection && f.controller.stored_car_id() == 1, "Host garage commits car before opening the real track preview");
    f.press(Action::right);
    f.press(Action::back);
    f.press(Action::confirm);
    expect(f.controller.screen() == Screen::track_selection && f.controller.track_id() == 1, "Track Back stores its preview and reentry restores it");
    f.press(Action::confirm);
    expect(f.controller.screen() == Screen::network_linkup && f.controller.selection() == 0, "Host track Continue enters Waiting Room on Start");
    command = f.take();
    expect(command && command->kind == NetworkLobbyCommandKind::select_car && command->car == 1 && command->manual, "Waiting Room first publishes the committed garage choice");
    command = f.take();
    expect(command && command->kind == NetworkLobbyCommandKind::set_settings && command->settings.track == 1 && command->settings.laps == 1 && command->settings.weather == 1
               && command->settings.ai && !command->settings.car_boost && !f.take(),
        "FIFO publishes all host settings before readiness");
    view.settings = command->settings;
    f.controller.set_network_view(view);
    expect(!f.take(), "Commitment must wait for the authoritative local car/gearbox acknowledgment");
    view.roster.players[0].car = 1;
    view.roster.players[0].manual = true;
    f.controller.set_network_view(view);
    command = f.take();
    expect(command && command->kind == NetworkLobbyCommandKind::ready && command->ready && !f.take(), "Matching authoritative choices trigger one automatic commitment");
    view.roster.players[0].ready = true;
    f.controller.set_network_view(view);
    f.press(Action::confirm);
    command = f.take();
    expect(command && command->kind == NetworkLobbyCommandKind::start, "Host Start permits the committed subset without waiting for selectors");
    f.press(Action::back);
    command = f.take();
    expect(command && command->kind == NetworkLobbyCommandKind::ready && !command->ready && f.controller.screen() == Screen::track_selection,
        "Waiting Back withdraws the host and returns to track without closing its session");
    f.press(Action::back);
    f.press(Action::back);
    f.press(Action::back);
    expect(f.controller.screen() == Screen::name_entry, "Host Back chain is track, garage, options, name");
    f.press(Action::back);
    expect(f.controller.screen() == Screen::network_linkup && f.controller.selection() == 1 && f.take()->kind == NetworkLobbyCommandKind::leave,
        "Name Back closes native identity and restores endpoint Continue");
    f.press(Action::back);
    f.press(Action::back);
    expect(f.controller.screen() == Screen::race_menu && f.controller.selection() == 3, "Endpoint and chooser Back restore Linkup race-menu selection");
    network::RaceView race;
    view.generation = race.generation = 7;
    race.grid = {0};
    f.controller.begin_network_race(view, race);
    f.press(Action::back);
    expect(f.controller.screen() == Screen::scene_loading && f.take()->kind == NetworkLobbyCommandKind::leave,
        "Linkup Loading Escape queues cancellation and retains the loading screen until application cleanup");
}

void client_route_and_failure() {
    Frontend f(false);
    f.text("client");
    f.press(Action::confirm);
    expect(f.take()->kind == NetworkLobbyCommandKind::join, "Player name confirmation joins the chosen endpoint");
    network::SessionView view;
    view.phase = network::SessionPhase::lobby;
    view.local_player = 2;
    view.host_player = 1;
    view.roster = {1, {{1, "HOST", 0, false, true}, {2, "CLIENT", 0, false, false}}};
    f.controller.set_network_view(view);
    expect(f.controller.screen() == Screen::car_selection, "Clients skip host race options and track selection");
    f.press(Action::confirm);
    expect(f.controller.screen() == Screen::network_linkup && f.take()->kind == NetworkLobbyCommandKind::select_car && !f.take(),
        "Client car confirmation enters Waiting Room directly");
    f.controller.set_network_view(view);
    expect(f.take()->kind == NetworkLobbyCommandKind::ready, "Client commits automatically after acknowledged selection");
    f.press(Action::back);
    expect(f.controller.screen() == Screen::car_selection && !f.take()->ready, "Client Waiting Back withdraws and returns to garage");
    f.press(Action::back);
    expect(f.controller.screen() == Screen::name_entry, "Client garage Back returns to its shared player name editor");
    view.phase = network::SessionPhase::failed;
    view.error = "Race started without you";
    f.controller.set_network_view(view);
    Menus menus;
    expect(f.controller.screen() == Screen::network_linkup && f.controller.network_menu(menus).notice == network_error_text(0, view.error),
        "Host departure or starting without a selector must interrupt selection with a visible failure");
    menus.network_labels[0].race_started_without_you = "AUTHORED EXCLUSION";
    menus.network_labels[0].session_lost = "AUTHORED SESSION LOSS";
    expect(f.controller.network_menu(menus).notice == "AUTHORED EXCLUSION", "Excluded-selector errors use the loaded original localization");
    f.controller.network_error("Host left the session");
    expect(f.controller.network_menu(menus).notice == "AUTHORED SESSION LOSS", "Host departure uses the loaded original session-lost localization");
    f.controller.network_error("Connection timed out");
    expect(f.controller.network_menu(menus).notice == "CONNECTION TIMED OUT", "Native transport failures retain their specific localized meaning");
    f.press(Action::back);
    expect(f.take()->kind == NetworkLobbyCommandKind::leave && f.controller.selection() == 0, "Failure Back closes the native endpoint and restores chooser");
    f.controller.return_network_lobby();
    expect(f.controller.screen() == Screen::main_menu, "Original race exit closes Linkup and restores frontend Main state2");
}

void status_formatting() {
    using namespace toca::frontend;
    network::SessionView session;
    session.hosting = true;
    session.local_player = session.host_player = 1;
    session.port = 27123;
    session.host_endpoints = {"192.168.1.20:27123", "10.0.0.8:27123"};
    session.roster.players = {{1, "LOCAL", 0, false, true}, {2, "REMOTE", 1, false, true}, {3, "LEFT", 2, false, true}};
    NetworkLabels original;
    original.weather_values[0] = "AUTHORED WEATHER";
    original.track_names[0] = "AUTHORED TRACK";
    const auto lobby = network_lobby_status(2, session, &original);
    expect(lobby.size() == 4 && lobby[0].find("AUTHORED WEATHER") != std::string::npos && lobby[2].find("192.168.1.20:27123") != std::string::npos,
        "Waiting Room reports compiled weather/track plus actual host LAN candidates and bound port");
    network::RaceView race;
    race.actors = {{0, 1, true, false, true, false}, {1, 2, true, false, false, true}, {2, 3, false, false, false, true}};
    const auto loading = network_loading_status(2, session, &race);
    expect(loading.size() == 4 && loading[2] == "REMOTE" && loading.back() == "ESC : ABBRECHEN", "Loading names include only admitted active humans still waiting for Loaded");
    race.paused = true;
    expect(network_pause_notice(2, session, &race) == "PAUSE DURCH : REMOTE", "Shared pause identifies its actual requester and excludes inactive human slots");
    race.actors[0].pause_requested = true;
    expect(network_pause_notice(0, session, &race) == "PAUSED BY : LOCAL, REMOTE", "Pause notices include every actual requester without assuming the local menu opened");
    race.paused = false;
    expect(network_pause_notice(0, session, &race).empty(), "A cleared aggregate must remove the shared pause notice");
    expect(network_error_text(1, "Race loading timed out") == "DELAI DE CHARGEMENT DEPASSE"
               && network_error_text(2, "Port must be between 1 and 65535") == "PORT MUSS 1 BIS 65535 SEIN",
        "Native loading and endpoint failures are localized independently of protocol state");
}
} // namespace

int main() {
    try {
        endpoint_and_waiting();
        host_route();
        client_route_and_failure();
        status_formatting();
        std::cout << "Original Linkup selection and Waiting Room command flow passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
