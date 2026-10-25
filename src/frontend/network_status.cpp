#include "frontend/network_status.hpp"

#include <algorithm>
#include <array>

namespace toca::frontend {
namespace {
using Text = std::array<std::string_view, 5>;

std::string local(const Text& text, std::uint32_t language) {
    return std::string(text.at(std::min(language, 4U)));
}

std::string join(const std::vector<std::string>& names, std::string_view separator = ", ") {
    std::string result;
    for (const auto& name : names) {
        if (!result.empty()) {
            result += separator;
        }
        result += name;
    }
    return result;
}

std::string player_name(const network::SessionView& session, network::PlayerId id) {
    const auto found = std::find_if(session.roster.players.begin(), session.roster.players.end(), [=](const auto& player) {
        return player.player == id;
    });
    return found == session.roster.players.end() ? std::to_string(id) : found->name;
}

constexpr Text yes{"YES", "OUI", "JA", "SI", "SI"}, no{"NO", "NON", "NEIN", "NO", "NO"};
constexpr Text weather{"WEATHER", "METEO", "WETTER", "METEO", "TIEMPO"}, laps{"LAPS", "TOURS", "RUNDEN", "GIRI", "VUELTAS"};
constexpr Text boost{"CAR BOOST", "BOOST AUTO", "AUFHOLHILFE", "AIUTO AUTO", "AYUDA AUTO"}, ai{"COMPUTER CARS", "VOITURES IA", "COMPUTERAUTOS", "AUTO COMPUTER", "COCHES IA"};
constexpr std::array<std::string_view, 10> tracks{
    "DONINGTON GP", "SILVERSTONE", "THRUXTON", "BRANDS HATCH", "OULTON PARK", "DONINGTON SHORT", "CROFT", "KNOCKHILL", "SNETTERTON", "LAVALAND"};
} // namespace

std::string network_error_text(std::uint32_t language, std::string_view error, const NetworkLabels* original) {
    if (error.empty()) {
        return {};
    }
    if (error == "Host left the session") {
        if (original && !original->session_lost.empty()) {
            return original->session_lost;
        }
        return local({"LINKUP SESSION LOST", "SESSION RESEAU PERDUE", "NETZWERKSITZUNG VERLOREN", "SESSIONE PERSA", "SESION PERDIDA"}, language);
    }
    if (error == "Race started without you") {
        if (original && !original->race_started_without_you.empty()) {
            return original->race_started_without_you;
        }
        return local({"RACE STARTED WITHOUT YOU", "COURSE COMMENCEE SANS VOUS", "RENNEN OHNE DICH GESTARTET", "GARA INIZIATA SENZA DI TE", "CARRERA INICIADA SIN TI"}, language);
    }

    struct Error {
        std::string_view key;
        Text text;
    };

    static constexpr std::array<Error, 14> errors{{
        {"Incompatible game protocol",
            {"INCOMPATIBLE GAME VERSION", "VERSION DU JEU INCOMPATIBLE", "SPIELVERSION NICHT KOMPATIBEL", "VERSIONE NON COMPATIBILE", "VERSION NO COMPATIBLE"}},
        {"Game data differs from the host",
            {"GAME DATA DIFFERS FROM HOST", "DONNEES DIFFERENTES DE L'HOTE", "SPIELDATEN WEICHEN VOM HOST AB", "DATI DIVERSI DALL'HOST", "DATOS DIFERENTES DEL ANFITRION"}},
        {"Session full", {"SESSION FULL", "SESSION COMPLETE", "SITZUNG VOLL", "SESSIONE PIENA", "SESION LLENA"}},
        {"Race already started", {"RACE ALREADY STARTED", "COURSE DEJA COMMENCEE", "RENNEN BEREITS GESTARTET", "GARA GIA INIZIATA", "CARRERA YA INICIADA"}},
        {"Invalid session message", {"INVALID NETWORK MESSAGE", "MESSAGE RESEAU INVALIDE", "UNGUELTIGE NETZWERKNACHRICHT", "MESSAGGIO NON VALIDO", "MENSAJE DE RED NO VALIDO"}},
        {"Connection timed out", {"CONNECTION TIMED OUT", "DELAI DE CONNEXION DEPASSE", "VERBINDUNGSZEIT ABGELAUFEN", "CONNESSIONE SCADUTA", "CONEXION AGOTADA"}},
        {"Race loading cancelled", {"RACE LOADING CANCELLED", "CHARGEMENT ANNULE", "LADEN ABGEBROCHEN", "CARICAMENTO ANNULLATO", "CARGA CANCELADA"}},
        {"Unable to load the race",
            {"UNABLE TO LOAD THE RACE", "IMPOSSIBLE DE CHARGER LA COURSE", "RENNEN KONNTE NICHT GELADEN WERDEN", "IMPOSSIBILE CARICARE LA GARA", "NO SE PUEDE CARGAR LA CARRERA"}},
        {"Race loading timed out", {"RACE LOADING TIMED OUT", "DELAI DE CHARGEMENT DEPASSE", "LADEZEIT ABGELAUFEN", "CARICAMENTO SCADUTO", "TIEMPO DE CARGA AGOTADO"}},
        {"Port must be between 1 and 65535", {"PORT MUST BE 1 TO 65535", "PORT ENTRE 1 ET 65535", "PORT MUSS 1 BIS 65535 SEIN", "PORTA DA 1 A 65535", "PUERTO ENTRE 1 Y 65535"}},
        {"Enter four IPv4 numbers from 0 to 255", {"ENTER FOUR IPV4 NUMBERS, 0 TO 255", "ENTRER QUATRE NOMBRES IPV4, 0 A 255", "VIER IPV4-ZAHLEN VON 0 BIS 255 EINGEBEN",
                                                      "QUATTRO NUMERI IPV4, DA 0 A 255", "CUATRO NUMEROS IPV4, DE 0 A 255"}},
        {"Unable to create ENet endpoint", {"CANNOT OPEN PORT, CHECK OTHER GAMES", "PORT INDISPONIBLE, VERIFIER LES JEUX", "PORT BELEGT, ANDERE SPIELE PRUEFEN",
                                               "PORTA NON DISPONIBILE", "NO SE PUEDE ABRIR EL PUERTO"}},
        {"ENet initialization failed",
            {"NETWORK INITIALIZATION FAILED", "ECHEC INITIALISATION RESEAU", "NETZWERKSTART FEHLGESCHLAGEN", "AVVIO RETE FALLITO", "ERROR AL INICIAR LA RED"}},
        {"ENet endpoint service failed", {"NETWORK CONNECTION FAILED", "ECHEC CONNEXION RESEAU", "NETZWERKVERBINDUNG FEHLGESCHLAGEN", "CONNESSIONE FALLITA", "ERROR DE CONEXION"}},
    }};
    if (error.starts_with("Enter an IPv4 address and port")) {
        return network_endpoint_help(language);
    }
    for (const auto& entry : errors) {
        if (error == entry.key) {
            return local(entry.text, language);
        }
    }
    // Unexpected diagnostic details must not leak untranslated implementation
    // wording or paths into the player-facing flow.
    return local({"NETWORK ERROR, TRY AGAIN", "ERREUR RESEAU, REESSAYER", "NETZWERKFEHLER, ERNEUT VERSUCHEN", "ERRORE RETE, RIPROVARE", "ERROR DE RED, REINTENTAR"}, language);
}

std::string network_endpoint_help(std::uint32_t language) {
    return local({"ENTER HOST IP:PORT, E.G. 192.168.1.20:27015", "ENTRER IP:PORT DE L'HOTE, EX. 192.168.1.20:27015", "HOST-IP:PORT EINGEBEN, Z.B. 192.168.1.20:27015",
                     "INSERIRE IP:PORTA HOST, ES. 192.168.1.20:27015", "INTRODUCIR IP:PUERTO, EJ. 192.168.1.20:27015"},
        language);
}

std::string network_local_marker(std::uint32_t language) {
    return local({"YOU", "TOI", "DU", "TU", "TU"}, language);
}

std::string network_host_marker(std::uint32_t language) {
    return local({"HOST", "HOTE", "HOST", "HOST", "HOST"}, language);
}

std::vector<std::string> network_lobby_status(std::uint32_t language, const network::SessionView& session, const NetworkLabels* original) {
    const auto& settings = session.settings;
    const auto weather_name = original && !original->weather_values.at(std::min<std::uint8_t>(settings.weather, 5)).empty()
                                  ? original->weather_values.at(std::min<std::uint8_t>(settings.weather, 5))
                                  : local(weather, language) + " " + std::to_string(settings.weather + 1);
    const auto track = original && !original->track_names.at(std::min<std::uint8_t>(settings.track, 9)).empty()
                           ? original->track_names.at(std::min<std::uint8_t>(settings.track, 9))
                           : std::string(tracks.at(std::min<std::uint8_t>(settings.track, 9)));
    const auto on_off = [&](bool value) {
        return local(value ? yes : no, language);
    };
    std::vector<std::string> result;
    result.push_back(track + " / " + weather_name + " / " + std::to_string(settings.laps) + " " + local(laps, language));
    result.push_back((original && !original->car_boost.empty() ? original->car_boost : local(boost, language)) + " : " + on_off(settings.car_boost) + " / "
                     + (original && !original->computer_cars.empty() ? original->computer_cars : local(ai, language)) + " : " + on_off(settings.ai));
    if (session.hosting) {
        const auto prefix = local({"HOST ADDRESS", "ADRESSE HOTE", "HOST-ADRESSE", "INDIRIZZO HOST", "DIRECCION HOST"}, language);
        if (session.host_endpoints.empty()) {
            result.push_back(prefix + " : " + local({"NO LAN IPV4 FOUND", "AUCUNE IPV4 LAN", "KEINE LAN-IPV4 GEFUNDEN", "NESSUN IPV4 LAN", "SIN IPV4 DE RED LOCAL"}, language)
                             + " / PORT " + std::to_string(session.port));
        } else {
            for (std::size_t index = 0; index < session.host_endpoints.size() && index < 4; ++index) {
                auto line = prefix + " : " + session.host_endpoints[index];
                if (index == 3 && session.host_endpoints.size() > 4) {
                    line += " / " + std::to_string(session.host_endpoints.size() - 4) + " " + local({"MORE", "AUTRES", "WEITERE", "ALTRI", "MAS"}, language);
                }
                result.push_back(std::move(line));
            }
        }
    }
    return result;
}

std::vector<std::string> network_loading_status(std::uint32_t language, const network::SessionView& session, const network::RaceView* race) {
    std::vector<std::string> result;
    result.push_back(local({"LOADING NETWORK RACE", "CHARGEMENT COURSE RESEAU", "NETZWERKRENNEN WIRD GELADEN", "CARICAMENTO GARA IN RETE", "CARGANDO CARRERA EN RED"}, language));
    std::vector<std::string> waiting;
    if (race) {
        for (const auto& actor : race->actors) {
            if (actor.active_human && !actor.loaded) {
                waiting.push_back(player_name(session, actor.player));
            }
        }
    }
    if (!waiting.empty()) {
        result.push_back(local({"WAITING FOR", "EN ATTENTE DE", "WARTE AUF", "IN ATTESA DI", "ESPERANDO A"}, language) + " :");
        // Eight bounded names fit in four rows without truncating identities.
        for (std::size_t i = 0; i < waiting.size(); i += 2) {
            result.push_back(waiting[i] + (i + 1 < waiting.size() ? " / " + waiting[i + 1] : ""));
        }
    }
    result.push_back(local({"ESC : CANCEL", "ECHAP : ANNULER", "ESC : ABBRECHEN", "ESC : ANNULLA", "ESC : CANCELAR"}, language));
    return result;
}

std::string network_pause_notice(std::uint32_t language, const network::SessionView& session, const network::RaceView* race) {
    if (!race || !race->paused) {
        return {};
    }
    std::vector<std::string> names;
    for (const auto& actor : race->actors) {
        if (actor.active_human && actor.pause_requested) {
            names.push_back(player_name(session, actor.player));
        }
    }
    if (names.empty()) {
        return {};
    }
    return local({"PAUSED BY", "PAUSE PAR", "PAUSE DURCH", "PAUSA DI", "PAUSA POR"}, language) + " : " + join(names);
}
} // namespace toca::frontend
