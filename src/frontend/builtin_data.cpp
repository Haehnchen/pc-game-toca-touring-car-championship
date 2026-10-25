#include "frontend/builtin_data.hpp"

#include <stdexcept>

namespace toca::frontend::builtin {
namespace {
// Imported once from canonical 1.1a (source CRC32 8ce56d41).
// Preserve original font bytes, including ASCII substitutes and accents.
struct LocalizedText {
    std::string_view english, french, german, italian, spanish;
};

constexpr std::array<LocalizedText, static_cast<std::size_t>(TextId::count)> translations{{
    // continue_label
    {.english = "CONTINUE", .french = "CONTINUER", .german = "WEITER", .italian = "CONTINUA", .spanish = "CONTINUAR"},
    // ok
    {.english = "OK", .french = "OK", .german = "OK", .italian = "OK", .spanish = "OK"},
    // back
    {.english = "BACK", .french = "PREC.", .german = "ZURUECK", .italian = "INDIETRO", .spanish = "RETR."},
    // go_back_to_previous_screen
    {.english = "GO BACK TO PREVIOUS SCREEN",
        .french = "RETOURNER ECRAN PRECEDENT",
        .german = "ZURUECK ZUM VORIGEN BILDSCHIRM",
        .italian = "RITORNA AL MENU PRECEDENTE",
        .spanish = "VOLVER PANTALLA ANTERIOR"},
    // yes
    {.english = "YES", .french = "OUI", .german = "JA", .italian = "SI'", .spanish = "SI"},
    // no
    {.english = "NO", .french = "NON", .german = "NEIN", .italian = "NO", .spanish = "NO"},
    // main_menu_title
    {.english = "MAIN MENU", .french = "MENU PRINC.", .german = "HAUPTMENUE", .italian = "MENU PRINC.", .spanish = "MENU PRINC."},
    // start_race
    {.english = "START RACE", .french = "DEBUT COURSE", .german = "RENNEN STARTEN", .italian = "INIZIA GARA", .spanish = "EMP. CARRERA"},
    // high_scores
    {.english = "HIGH SCORES", .french = "SCORES ELEV.", .german = "HIGH SCORE", .italian = "PUNTEGGI RECORD", .spanish = "MEJORES PUNTUA."},
    // toca_showdown
    {.english = "TOCA SHOWDOWN", .french = "CONFRONT. TOCA", .german = "TOCA-DUELL", .italian = "SFIDA TOCA", .spanish = "DESAFIO TOCA"},
    // game_options
    {.english = "GAME OPTIONS", .french = "OPTIONS DE JEU", .german = "SPIELOPTIONEN", .italian = "OPZIONI GIOCO", .spanish = "OPCIONES"},
    // linkup_game
    {.english = "LINKUP GAME", .french = "JEU RESEAU", .german = "VERBINDUNGSSPIEL", .italian = "PART. COLLEG.", .spanish = "PARTIDA EN CONEX."},
    // quit
    {.english = "QUIT", .french = "QUITTER", .german = "BEENDEN", .italian = "ESCI", .spanish = "SALIR"},
    // select_and_start_race
    {.english = "SELECT AND START RACE",
        .french = "SELEC. ET COM. COURSE",
        .german = "RENNEN WAEHLEN UND STARTEN",
        .italian = "SELEZ. E INIZIA GARA",
        .spanish = "SELEC. Y EMP. CARRERA"},
    // view_the_high_scores
    {.english = "VIEW THE HIGH SCORES",
        .french = "VOIR SCORES ELEV.",
        .german = " HIGH SCORES ANSEHEN ",
        .italian = "VISUALIZZA PUNTEGGI RECORD",
        .spanish = "VER MEJORES PUNTUA."},
    // the_final_showdown
    {.english = "THE FINAL SHOWDOWN", .french = "CONFRONTATION FINALE", .german = "DAS LETZTE DUELL", .italian = "LA SFIDA FINALE", .spanish = "DESAFIO FINAL"},
    // sound_graphics_and_controller_setup
    {.english = "SOUND, GRAPHICS AND CONTROLLER SETUP",
        .french = "CONF. SON, GRAPHISMES ET MANETTE",
        .german = "SETUP FUER SOUND, GRAFIKEN & CONTROLLER",
        .italian = "IMPOST. SONORO, GRAFICA E PULSANTIERA",
        .spanish = "CONFIG. SONIDO, GRAFICOS Y CONTROLADOR"},
    // start_a_multiplayer_linkup_game
    {.english = "START A MULTIPLAYER LINKUP GAME",
        .french = "COMMENCER JEU RESEAU MULTIJOUEUR",
        .german = "MULTIPLAYER-SPIEL STARTEN",
        .italian = "INIZIA PARTITA MULTIGIOC. IN COLLEG.",
        .spanish = "EMP. PARTIDA CONEX VARIOS JUGADORES"},
    // end_the_game
    {.english = "END THE GAME", .french = "FINIR LA PARTIE", .german = "SPIEL BEENDEN", .italian = "ABB. PARTITA", .spanish = "FIN PARTIDA"},
    // race_menu
    {.english = "RACE MENU", .french = "MENU COURSE", .german = "RENNMENUE", .italian = "MENU GARA", .spanish = "MENU CARRERA"},
    // single_race
    {.english = "SINGLE RACE", .french = "COURSE UNIQUE", .german = "EINZELRENNEN", .italian = "GARA SING.", .spanish = "CARR. INDIV."},
    // championship
    {.english = "CHAMPIONSHIP", .french = "CHAMPIONNAT", .german = "MEISTERSCHAFT", .italian = "CAMPIONATO", .spanish = "CAMPEONATO"},
    // time_trial
    {.english = "TIME TRIAL", .french = "CTR MONTRE", .german = "ZEITFAHREN", .italian = "PR. CRONOM.", .spanish = "CONTRARRELOJ"},
    // start_single_race
    {.english = "START SINGLE RACE", .french = "DEBUT COURSE UNIQUE", .german = "EINZELRENNEN STARTEN", .italian = "INIZIA GARA SING.", .spanish = "EMP. CARRERA INDIV."},
    // start_autotrader_rac_championship_race
    {.english = "START AUTOTRADER RAC CHAMPIONSHIP RACE",
        .french = "COMMENCER CSE DE CHAMP. RAC AUTOTRADER",
        .german = "AUTOTRADER RAC/MEISTERSCHAFT STARTEN",
        .italian = "INIZIA GARA DEL CAMPIONATO AUTOTRADER RAC",
        .spanish = "COMENZAR CARRERA DE COMP. AUTOTRADER RAC"},
    // start_practice_race
    {.english = "START PRACTICE RACE", .french = "DEBUT COURSE ENTRAIN.", .german = "TRAININGSLAUF STARTEN", .italian = "INIZIA GARA DI PROVA", .spanish = "EMP. CARR. ENTREN."},
    // load_game
    {.english = "LOAD GAME", .french = "CHARG. JEU", .german = "SPIEL LADEN", .italian = "CAR. PARTITA", .spanish = "CARGAR PARTIDA"},
    // new_game
    {.english = "NEW GAME", .french = "NVEAU JEU", .german = "NEUES SPIEL", .italian = "NUOVA PART.", .spanish = "PARTIDA NUEVA"},
    // load_best_lap
    {.english = "LOAD BEST LAP", .french = "CHARG. MEILLEUR TOUR", .german = "BESTE RUNDE LADEN", .italian = "CAR. MIGLIOR GIRO", .spanish = "CARGAR MEJOR VUELTA"},
    // no_saved_games
    {.english = "NO SAVED GAMES", .french = "AUCUN JEU SAUV.", .german = "KEINE GESP. SPIELE", .italian = "NESS. PART. SALV.", .spanish = "NO HAY PARTIDAS"},
    // round
    {.english = "ROUND", .french = "MANCHE", .german = "RUNDE", .italian = "GARA", .spanish = "RECORRIDO"},
    // short_label
    {.english = "SHORT", .french = "COURT", .german = "KURZ", .italian = "RIDOTTO", .spanish = "CORTO"},
    // full
    {.english = "FULL", .french = "COMPLET", .german = "VOLL", .italian = "COMPLETO", .spanish = "COMPLETO"},
    // start_a_new_championship
    {.english = "START A NEW CHAMPIONSHIP",
        .french = "COM. NVEAU CHAMPIONNAT",
        .german = "NEUE MEISTERSCHAFT STARTEN",
        .italian = "INIZIA NUOVO CAMPIONATO",
        .spanish = "EMP. CAMPEONATO NUEVO"},
    // load_a_saved_championship_position
    {.english = "LOAD A SAVED CHAMPIONSHIP POSITION",
        .french = "CHARG. POSITION DU CHAMP. SAUVEGARDEE",
        .german = "GESP. MEISTERSCHAFTSPLAZIERUNG LADEN",
        .italian = "CARICA POSIZ. SALVATA DI CAMPIONATO",
        .spanish = "CARGAR UNA POSICION GUARDADA"},
    // load_game_from_slot_1
    {.english = "LOAD GAME FROM SLOT 1",
        .french = "CHARGER A PARTIR DE 1",
        .german = "SPIEL IN POS. 1 LADEN",
        .italian = "CARICA PART. DA SLOT 1",
        .spanish = "CARGAR PARTIDA DEL BLOQUE 1"},
    // load_game_from_slot_2
    {.english = "LOAD GAME FROM SLOT 2",
        .french = "CHARGER A PARTIR DE 2",
        .german = "SPIEL IN POS. 2 LADEN",
        .italian = "CARICA PART. DA SLOT 2",
        .spanish = "CARGAR PARTIDA DEL BLOQUE 2"},
    // load_game_from_slot_3
    {.english = "LOAD GAME FROM SLOT 3",
        .french = "CHARGER A PARTIR DE 3",
        .german = "SPIEL IN POS. 3 LADEN",
        .italian = "CARICA PART. DA SLOT 3",
        .spanish = "CARGAR PARTIDA DEL BLOQUE 3"},
    // load_game_from_slot_4
    {.english = "LOAD GAME FROM SLOT 4",
        .french = "CHARGER A PARTIR DE 4",
        .german = "SPIEL IN POS. 4 LADEN",
        .italian = "CARICA PART. DA SLOT 4",
        .spanish = "CARGAR PARTIDA DEL BLOQUE 4"},
    // load_game_from_slot_5
    {.english = "LOAD GAME FROM SLOT 5",
        .french = "CHARGER A PARTIR DE 5",
        .german = "SPIEL IN POS. 5 LADEN",
        .italian = "CARICA PART. DA SLOT 5",
        .spanish = "CARGAR PARTIDA DEL BLOQUE 5"},
    // load_best_lap_help
    {.english = "LOAD BEST LAP", .french = "CHARG. MEILLEUR TOUR", .german = "BESTE RUNDE LADEN", .italian = "CAR. MIGLIOR GIRO", .spanish = "CARGAR MEJOR VUELTA"},
    // players
    {.english = "PLAYERS", .french = "JOUEURS", .german = "SPIELER", .italian = "GIOCATORI", .spanish = "JUGADORES"},
    // select_the_number_of_players
    {.english = "SELECT THE NUMBER OF PLAYERS",
        .french = "SEL. NOMBRE DE JOUEURS",
        .german = "BITTE SPIELERZAHL WAEHLEN",
        .italian = "SELEZ. NUMERO DI GIOCATORI",
        .spanish = "SELECCIONA NO. DE JUGADORES"},
    // progress_to_the_next_options_screen
    {.english = "PROGRESS TO THE NEXT OPTIONS SCREEN",
        .french = "ALLER AU PROCHAIN ECRAN D'OPTIONS",
        .german = "WEITER ZUM NAECHSTEN OPTIONSSCHIRM ",
        .italian = "VAI ALLA VIDEATA OPZIONI SUCCESSIVA",
        .spanish = "SIGUIENTE PANTALLA DE OPCIONES"},
    // lap
    {.english = "LAP", .french = "TOUR", .german = "RUNDE", .italian = "GIRO", .spanish = "VUELTA"},
    // race
    {.english = "RACE", .french = "COURSE", .german = "RENNEN", .italian = "GARA", .spanish = "CARRERA"},
    // short_championship
    {.english = "SHORT CHAMPIONSHIP", .french = "CHAMPIONNAT COURT", .german = "KURZE MEISTERSCHAFT", .italian = "CAMPIONATO RIDOTTO", .spanish = "MINICAMPEONATO"},
    // full_championship
    {.english = "FULL CHAMPIONSHIP", .french = "CHAMPIONN. ENTIER", .german = "GANZE MEISTERSCHAFT", .italian = "CAMPIONATO COMPLETO", .spanish = "CAMPEONATO COMPLETO"},
    // select_the_number_of_laps_to_race
    {.english = "SELECT THE NUMBER OF LAPS TO RACE",
        .french = "SEL. NB DE TOURS POUR LA COURSE",
        .german = "ZAHL DER RUNDEN IM RENNEN WAHLEN",
        .italian = "SELEZ. NUMERO GIRI DA PERCORRERE",
        .spanish = "SELECCIONA NO. DE VUELTAS"},
    // select_the_length_of_championship_to_race
    {.english = "SELECT THE LENGTH OF CHAMPIONSHIP TO RACE",
        .french = "CHOISISSEZ LA DUREE DU CHAMPIONNAT",
        .german = "MEISTERSCHAFTSLAENGE FESTLEGEN",
        .italian = "SELEZIONA LA LUNGHEZZA DEL CAMPIONATO",
        .spanish = "SELECCIONA LA DURACION DEL CAMPEONATO"},
    // weather
    {.english = "WEATHER", .french = "METEO", .german = "WETTER", .italian = "TEMPO", .spanish = "TIEMPO"},
    // default_label
    {.english = "DEFAULT", .french = "DEFAUT", .german = "STANDARD", .italian = "PREDEFIN.", .spanish = "POR DEFEC."},
    // sunny
    {.english = "SUNNY", .french = "BEAU", .german = "SONNE", .italian = "SERENO", .spanish = "SOLEADO"},
    // rain
    {.english = "RAIN", .french = "PLUIE", .german = "REGEN", .italian = "PIOGGIA", .spanish = "LLUVIOSO"},
    // snow
    {.english = "SNOW", .french = "NEIGE", .german = "SCHNEE", .italian = "NEVE", .spanish = "NEVANDO"},
    // cloudy
    {.english = "CLOUDY", .french = "COUVERT", .german = "BEWOELKT", .italian = "NUVOLOSO", .spanish = "NUBLADO"},
    // storm
    {.english = "STORM", .french = "ORAGE", .german = "GEWITTER", .italian = "TEMPORALE", .spanish = "TORMENTA"},
    // overcast
    {.english = "OVERCAST", .french = "COUVERT", .german = "BEDECKT", .italian = "NUVOLOSO", .spanish = "NUBLADO"},
    // choose_the_weather_for_the_race
    {.english = "CHOOSE THE WEATHER FOR THE RACE",
        .french = "CHOISIR METEO POUR LA COURSE",
        .german = "WETTER FUER DAS RENNEN WAEHLEN",
        .italian = "SELEZIONA IL TEMPO PER LA GARA",
        .spanish = "ELIGE EL TIEMPO PARA LA CARRERA"},
    // qualify
    {.english = "QUALIFY", .french = "QUALIF.", .german = "QUALIFIK.", .italian = "QUALIFICHE", .spanish = "CLASIFICACION"},
    // car_boost
    {.english = "CAR BOOST", .french = "TURBO", .german = "TEMPOSCHUB", .italian = "POTENZIATORE AUTO", .spanish = "REFUERZO"},
    // computer_cars
    {.english = "COMPUTER CARS", .french = "VOITURES COMPUTERISE", .german = "COMPUTER CARS", .italian = "MACCHINE COMPUTERIZZATE", .spanish = "COCHES DEL ORDENADOR"},
    // choose_to_compete_with_computer_cars
    {.english = "CHOOSE TO COMPETE WITH COMPUTER CARS",
        .french = "CHOISIR DE COMBATTRE CONTRE VOITURES COMPUTERISE",
        .german = "CHOOSE TO COMPETE WITH COMPUTER CARS",
        .italian = "SCEGLERE DI COMPETERE CONTRO MACCHINE COMPUTERIZZATE",
        .spanish = "COMPETIR CON LOS COCHES DEL ORDENADOR"},
    // difficulty
    {.english = "DIFFICULTY", .french = "DIFFICULTE", .german = "SCHWIERIGKEIT", .italian = "LIV. DIFFICOLTA", .spanish = "DIFICULTAD"},
    // easy
    {.english = "EASY", .french = "FACILE", .german = "LEICHT", .italian = "FACILE", .spanish = "FACIL"},
    // hard
    {.english = "HARD", .french = "DUR", .german = "SCHWER", .italian = "DIFFICILE", .spanish = "DIFICIL"},
    // choose_the_difficulty_level
    {.english = "CHOOSE THE DIFFICULTY LEVEL",
        .french = "CHOISIR NIVEAU DIFFICULTE",
        .german = "SCHWIERIGKEITSGRAD WAEHLEN",
        .italian = "SCEGLI LIV. DIFFICOLTA'",
        .spanish = "ESCOGE EL NIVEL DE DIFICULTAD"},
    // decide_if_you_want_to_do_a_qualifying_lap
    {.english = "DECIDE IF YOU WANT TO DO A QUALIFYING LAP",
        .french = "VOULEZ-VOUS FAIRE UN TOUR DE QUALIFICATION",
        .german = "QUALIFIKATIONSRUNDE ABSOLVIEREN?",
        .italian = "DECIDI SE FARE UN GIRO DI QUALIFICAZIONE",
        .spanish = "\277QUIERES CORRER UNA VUELTA DE CLASIFICACION?"},
    // speed_up_slow_player_cars
    {.english = "SPEED UP SLOW PLAYER CARS?",
        .french = "ACCELERER VOITURES LENTES?",
        .german = "LANGSAME SP-WAGEN BESCHLEUNIGEN?",
        .italian = "POTENZIARE AUTO GIOCAT. LENTO?",
        .spanish = "\277ACELERAR COCHES JUGADOR LENTO?"},
    // sound_setup
    {.english = "SOUND SETUP", .french = "CONF. SON", .german = "SOUND-SETUP", .italian = "IMP. SONORO", .spanish = "CONFIG. SONIDO"},
    // effects
    {.english = "EFFECTS", .french = "EFFETS", .german = "EFFEKTE", .italian = "EFF. SON.", .spanish = "EFECTOS"},
    // cd_music
    {.english = "CD MUSIC", .french = "MUSIQ. CD", .german = "CD-MUSIK", .italian = "MUSICA CD", .spanish = "MUSICA CD"},
    // volume
    {.english = "VOLUME", .french = "VOLUME", .german = "LAUTSTAERKE", .italian = "VOLUME", .spanish = "VOLUMEN"},
    // game_music
    {.english = "GAME MUSIC", .french = "MUSIQ. JEU", .german = "SPIELMUSIK", .italian = "MUSICA GIOCO", .spanish = "MUSICA JUEGO"},
    // mono_sfx
    {.english = "MONO SFX", .french = "EFFETS SON MONO", .german = "MONO", .italian = "EFFETTI SON. MONO", .spanish = "EFECTOS SON. MONO"},
    // stereo_sfx
    {.english = "STEREO SFX", .french = "EFFETS SON STEREO", .german = "STEREO", .italian = "EFFETTI SON. STEREO", .spanish = "EFECTOS SON. ESTEREO"},
    // on
    {.english = "ON", .french = "ON", .german = "EIN", .italian = "ACC.", .spanish = "CON"},
    // off
    {.english = "OFF", .french = "OFF", .german = "AUS", .italian = "SPENTO", .spanish = "SIN"},
    // adjust_the_sound_effects_volume
    {.english = "ADJUST THE SOUND EFFECTS VOLUME",
        .french = "REGLER VOLUME EFFETS SONORES",
        .german = "SOUNDEFFEKT-LAUTSTAERKE EINSTELLEN",
        .italian = "REGOLA VOLUME EFFETTI SONORI",
        .spanish = "AJUSTAR VOLUMEN EFECTOS"},
    // adjust_the_music_volume
    {.english = "ADJUST THE MUSIC VOLUME",
        .french = "REGLER VOLUME MUSICAL",
        .german = "MUSIK-LAUTSTAERKE EINSTELLEN",
        .italian = "REGOLA VOLUME MUSICA",
        .spanish = "AJUSTAR VOLUMEN MUSICA"},
    // select_stereo_or_mono_sound
    {.english = "SELECT STEREO OR MONO SOUND",
        .french = "SEL. SON STEREO OU MONO",
        .german = "STEREO- ODER MONO-SOUND WAEHLEN",
        .italian = "SELEZ. SONORO STEREO O MONO",
        .spanish = "SELECC. SONIDO MONO O ESTEREO"},
    // toggle_in_game_music
    {.english = "TOGGLE IN-GAME MUSIC", .french = "PERMUTER MUSIQ. DU JEU", .german = "SPIELMUSIK EIN/AUS", .italian = "ATTIVA MUSICA GIOCO", .spanish = "ENCEN./APAGAR MUSICA"},
    // low_detail
    {.english = "LOW DETAIL", .french = "DETAILS BAS", .german = "WENIG DETAILS", .italian = "DETT. RIDOTTI", .spanish = "POCOS DETALLES"},
    // medium_detail
    {.english = "MEDIUM DETAIL", .french = "DETAILS MOYENS", .german = "MITTLERE DETAILS", .italian = "DETTAGLI MEDI", .spanish = "DETALLES ESTANDAR"},
    // high_detail
    {.english = "HIGH DETAIL  ", .french = "DETAILS ELEVES", .german = "VIELE DETAILS", .italian = "DETT. ALTO", .spanish = "MUCHOS DETALLES"},
    // very_high_detail
    {.english = "VERY HIGH DETAIL", .french = "DETAILS SUPER-ELEVES", .german = "SEHR VIELE DETAILS", .italian = "DETT. MASS.", .spanish = "SUPERIOR DETALLES"},
    // split
    {.english = "SPLIT", .french = "DIVIS.", .german = "TEILUNG", .italian = "VIDEATA DIVISA", .spanish = "DIVISION"},
    // horizontal
    {.english = "HORIZONTAL", .french = "HORIZONTAL", .german = "HORIZONTAL", .italian = "ORIZ...", .spanish = "HORIZONTAL"},
    // vertical
    {.english = "VERTICAL", .french = "VERTICAL", .german = "VERTIKAL", .italian = "VERTICALE", .spanish = "VERTICAL"},
    // adjust_the_graphical_detail
    {.english = "ADJUST THE GRAPHICAL DETAIL",
        .french = "REGLER DETAILS GRAPHIQUES",
        .german = "GRAFIKDETAILS EINSTELLEN",
        .italian = "REGOLA DETTAGLI GRAFICA",
        .spanish = "AJUSTAR DETALLES GRAFICOS"},
    // change_the_split_screen_type_in_2p_mode
    {.english = "CHANGE THE SPLIT SCREEN TYPE IN 2P MODE",
        .french = "CHANGER LE TYPE ECRAN DIVISE EN MODE 2J",
        .german = "BILDSCHIRMTEILUNG IM 2S-MODUS AENDERN",
        .italian = "MODIFICA VIDEATA DIVISA IN MOD. A DUE GIOC.",
        .spanish = "CAMBIAR TIPO PANTALLA PARTIDA EN MODO 2J"},
    // player
    {.english = "PLAYER", .french = "JOUEUR", .german = "SPIELER", .italian = "GIOCAT.", .spanish = "JUGADOR"},
    // controls
    {.english = "CONTROLS", .french = "CONTROLES", .german = "STEUERUNG", .italian = "COMANDI", .spanish = "CONTROLES"},
    // keyboard
    {.english = "KEYBOARD", .french = "CLAVIER", .german = "TASTATUR", .italian = "TASTIERA", .spanish = "TECLADO"},
    // configure_keyboard
    {.english = "CONFIGURE KEYBOARD", .french = "CONFIGURER CLAVIER", .german = "TASTATUR EINRICHTEN", .italian = "CONFIGURA TASTIERA", .spanish = "CONFIGURAR TECLADO"},
    // configure_joystick
    {.english = "CONFIGURE JOYSTICK", .french = "CONFIGURER JOYSTICK", .german = "JOYSTICK EINRICHTEN", .italian = "CONFIGURA JOYSTICK", .spanish = "CONFIGURAR JOYSTICK"},
    // sensitivity
    {.english = "SENSITIVITY", .french = "SENSIBILITE", .german = "EMPFINDLICHKEIT", .italian = "SENSIBILITU'", .spanish = "SENSIBILIDAD"},
    // low
    {.english = "LOW", .french = "BAS", .german = "NIEDRIG", .italian = "BASSA", .spanish = "BAJO"},
    // medium
    {.english = "MEDIUM", .french = "MOYEN", .german = "MITTEL", .italian = "MEDIA", .spanish = "MEDIO"},
    // high
    {.english = "HIGH", .french = "HAUT", .german = "HOCH", .italian = "ALTA", .spanish = "ALTO"},
    // choose_button_or_key_for
    {.english = "CHOOSE BUTTON OR KEY FOR", .french = "CHOISIR TOUCHE POUR", .german = "TASTE WAEHLEN FUER", .italian = "SCEGLI TASTO PER", .spanish = "ESCOGE BOTON O TECLA PARA"},
    // choose_key_for
    {.english = "CHOOSE KEY FOR", .french = "CHOIS. TOUCHE PR", .german = "TASTE WAEHLEN:", .italian = "SEL. TASTO PER", .spanish = "ESCOGE TECLA PARA"},
    // accelerate
    {.english = "ACCELERATE", .french = "ACCELERER", .german = "BESCHLEUNIGEN", .italian = "ACCELERA", .spanish = "ACELERAR"},
    // brake
    {.english = "BRAKE", .french = "FREINER", .german = "BREMSEN", .italian = "FRENA", .spanish = "FRENAR"},
    // turn_left
    {.english = "TURN LEFT", .french = "TOURNER GCHE", .german = "NACH LINKS", .italian = "GIRA A SIN.", .spanish = "GIRAR IZQ."},
    // turn_right
    {.english = "TURN RIGHT", .french = "TOURNER DTE", .german = "NACH RECHTS", .italian = "GIRA A DESTRA", .spanish = "GIRAR DCHA."},
    // gear_up
    {.english = "GEAR UP", .french = "VIT. SUP.", .german = "RAUFSCHALTEN", .italian = "AUM. MAR.", .spanish = "AUMENTAR"},
    // gear_down
    {.english = "GEAR DOWN", .french = "VIT. INF.", .german = "RUNTERSCHALTEN", .italian = "SCALA MAR.", .spanish = "REDUCIR"},
    // rear_camera
    {.english = "REAR CAMERA", .french = "CAMERA ARR.", .german = "HECKKAMERA", .italian = "TELEC. POSTER.", .spanish = "CAMARA TRASERA"},
    // change_camera
    {.english = "CHANGE CAMERA", .french = "CHANGER CAMERA", .german = "KAMERA AENDERN", .italian = "MODIFICA TELECAMERA", .spanish = "CAMBIAR CAMARA"},
    // play_horn
    {.english = "PLAY HORN", .french = "KLAXONNER", .german = "HUPEN", .italian = "SUONA CLACSON", .spanish = "PITAR"},
    // configure_your_controller
    {.english = "CONFIGURE YOUR CONTROLLER",
        .french = "CONFIGURER MANETTE",
        .german = "CONTROLLER EINRICHTEN",
        .italian = "CONFIGURA PULSANTIERA",
        .spanish = "CONFIGURA TU CONTROLADOR"},
    // select_your_controller
    {.english = "SELECT YOUR CONTROLLER",
        .french = "SELECTIONNER MANETTE",
        .german = "CONTROLLER WAEHLEN",
        .italian = "SELEZIONA PULSANTIERA",
        .spanish = "SELECC. TU CONTROLADOR"},
    // set_the_sensitivity_for_this_controller
    {.english = "SET THE SENSITIVITY FOR THIS CONTROLLER",
        .french = "REGLEZ LA SENSIBILITE DE CETTE MANETTE",
        .german = "CONTROLLEREMPFINDLICHKEIT FESTLEGEN",
        .italian = "IMPOSTA LA SENSIBILITU' DEL CONTROLLER",
        .spanish = "AJUSTA LA SENSIBILIDAD DE ESTE CONTROLADOR"},
    // linkup_title
    {.english = "LINKUP GAME", .french = "JEU RESEAU", .german = "VERBINDUNGSSPIEL", .italian = "PART. IN COLL.", .spanish = "PARTIDA EN CONEX."},
    // host_a_new_game
    {.english = "HOST A NEW GAME", .french = "CREER NV JEU", .german = "HOST FUER NEUES SPIEL", .italian = "AVVIA NUOVA PART.", .spanish = "ORG. PARTIDA NUEVA"},
    // join_game
    {.english = "JOIN GAME", .french = "JOINDRE JEU", .german = "EINEM SPIEL BEITRETEN", .italian = "UNISCITI", .spanish = "UNIRSE A PARTIDA"},
    // host_new_game_help
    {.english = "HOST A NEW GAME", .french = "CREER NV JEU", .german = "HOST FUER NEUES SPIEL", .italian = "AVVIA NUOVA PART.", .spanish = "ORG. PARTIDA NUEVA"},
    // join_game_in_progress
    {.english = "JOIN GAME IN PROGRESS",
        .french = "JOINDRE JEU EN COURS",
        .german = "LAUFENDEM SPIEL BEITRETEN",
        .italian = "UNISC. A PART. ATTUALE",
        .spanish = "UNIRSE A PARTIDA EN CURSO"},
    // enter_session_name
    {.english = "ENTER SESSION NAME", .french = "ENTRER NOM SESSION", .german = "SPIELNAMEN EINGEBEN", .italian = "INVIA NOME SESSIONE", .spanish = "INTR. NOMBRE PARTIDA"},
    // enter_player_name
    {.english = "ENTER PLAYER NAME", .french = "ENTRER NOM JOUEUR", .german = "SPIELERNAMEN EINGEBEN", .italian = "INVIA NOME GIOCAT.", .spanish = "INTR. NOMBRE JUGADOR"},
    // waiting_room
    {.english = "WAITING ROOM", .french = "ATTENTE", .german = "WARTERAUM", .italian = "SALA DI ATTESA", .spanish = "SALA DE ESPERA"},
    // options_title
    {.english = "GAME OPTIONS", .french = "OPTIONS DE JEU", .german = "SPIELOPTIONEN", .italian = "OPZIONI GIOCO", .spanish = "OPCIONES"},
    // sound_setup_item
    {.english = "SOUND SETUP", .french = "CONF. SON", .german = "SOUND-SETUP", .italian = "IMP. SONORO", .spanish = "CONFIG. SONIDO"},
    // graphics_setup
    {.english = "GRAPHICS SETUP", .french = "CONF. GRAPHISMES", .german = "GRAFIK-SETUP", .italian = "IMP. GRAFICA", .spanish = "CONFIG. GRAFICOS"},
    // controller_setup
    {.english = "CONTROLLER SETUP", .french = "CONF. MANETTES", .german = "CONTROLLER-SETUP", .italian = "IMP. PULSANTIERA", .spanish = "CONFIG. CONTROLADOR"},
    // set_up_the_sound
    {.english = "SET UP THE SOUND", .french = "CONFIGURER SON", .german = "SOUND EINSTELLEN", .italian = "IMPOSTA SONORO", .spanish = "CONFIGURA EL SONIDO"},
    // set_up_the_graphics
    {.english = "SET UP THE GRAPHICS", .french = "CONF. GRAPHISMES", .german = "GRAFIKEN EINSTELLEN", .italian = "IMPOSTA GRAFICA", .spanish = "CONFIGURA LOS GRAFICOS"},
    // set_up_the_controller
    {.english = "SET UP THE CONTROLLER", .french = "CONFIGURER MANETTE", .german = "CONTROLLER EINSTELLEN", .italian = "IMPOSTA PULSANTIERA", .spanish = "CONFIG. CONTROLADOR"},
    // linkup_session_lost
    {.english = "LINKUP SESSION LOST",
        .french = "SESSION RESEAU PERDUE",
        .german = "VERB.-RUNDE VERLOREN",
        .italian = "COLLEG. DISATTIVATO",
        .spanish = "SESION DE ENLACE PERDIDA"},
    // the_race_has_started_without_you
    {.english = "THE RACE HAS STARTED WITHOUT YOU",
        .french = "LA COURSE A COMMENCE SANS VOUS",
        .german = "RENNEN HAT OHNE SIE BEGONNEN",
        .italian = "LA CORSA E' INIZIATA SENZA DI TE",
        .spanish = "LA CARRERA HA COMENZADO SIN TI"},
    // select_car
    {.english = "SELECT CAR", .french = "SEL. VOITURE", .german = "WAGEN WAEHLEN", .italian = "SEL. AUTO", .spanish = "SELECC. COCHE"},
    // enter_name
    {.english = "ENTER NAME", .french = "ENTRER NOM", .german = "NAMEN EINGEBEN", .italian = "INVIA NOME", .spanish = "INTR. NOMBRE"},
    // drive
    {.english = "DRIVE", .french = "CONDUITE", .german = "ANTRIEB", .italian = "UNITA'", .spanish = "CONDUCIR"},
    // fwd
    {.english = "FWD", .french = "R.AVT", .german = "V.-RAD", .italian = "T.A.", .spanish = "R. DLTRA."},
    // rwd
    {.english = "RWD", .french = "R.ARR", .german = "H.-RAD", .italian = "T.P.", .spanish = "R. ATRAS"},
    // four_wheel_drive
    {.english = "4WD", .french = "4X4", .german = "4RA", .italian = "4X4", .spanish = "T4R"},
    // valves
    {.english = "VALVES", .french = "VALVES", .german = "VENTILE", .italian = "VALVOLE", .spanish = "VALVULAS"},
    // bhp
    {.english = "BHP", .french = "CH.FR.", .german = "PS", .italian = "BHP", .spanish = "FR. CV"},
    // rpm
    {.english = "RPM", .french = "TR/MN", .german = "U/MIN", .italian = "GIRI/MIN", .spanish = "REV/MIN"},
    // press_up_to_toggle_transmission
    {.english = "PRESS 'UP' TO TOGGLE TRANSMISSION",
        .french = "APP. SUR 'HAUT' PR CHANGER VITESSE",
        .german = "MIT 'OBEN' GETRIEBE UMSCHALTEN",
        .italian = "PREMI IN SU PER ATTIVARE TRASMISSIONE",
        .spanish = "PULSA 'ARRIBA' PARA CAMBIAR TRANSMI."},
    // manual
    {.english = "MANUAL", .french = "MANUEL", .german = "SCHALT", .italian = "MANUALE", .spanish = "MANUAL"},
    // auto_label
    {.english = "AUTO", .french = "AUTO", .german = "AUTO", .italian = "AUTOM.", .spanish = "AUTO."},
    // unknown_vehicle
    {.english = "???", .french = "???", .german = "???", .italian = "???", .spanish = "???"},
    // you_cannot_select_this_car
    {.english = "YOU CANNOT SELECT THIS CAR",
        .french = "SEL. DE CETTE VOITURE",
        .german = "DIESER WAGEN NICHT WAEHLBAR",
        .italian = " NON PUOI SELEZ. QUEST'AUTO",
        .spanish = "IMPOS. SEL. ESTE COCHE"},
    // select_track
    {.english = "SELECT TRACK", .french = "SEL. CIRCUIT", .german = "STRECKE WAEHLEN", .italian = "SELEZ. PISTA", .spanish = "SELECC. PISTA"},
    // length
    {.english = "LENGTH", .french = "LONGUEUR", .german = "LAENGE", .italian = "LUNGH", .spanish = "LONGITUD"},
    // location
    {.english = "LOCATION", .french = "CIRCUIT", .german = "ORT", .italian = "POSIZIONE", .spanish = "UBICACION"},
    // select_your_track
    {.english = "SELECT YOUR TRACK", .french = "SEL. VOTRE CIRCUIT", .german = "BITTE STRECKE WAEHLEN", .italian = "SEL. LA TUA PISTA", .spanish = "SELECC. TU PISTA"},
    // you_cannot_select_this_track_yet
    {.english = "YOU CANNOT SELECT THIS TRACK YET",
        .french = "IMPOS. SEL. CE CIRCUIT PR L'INSTANT",
        .german = "STRECKE NOCH NICHT VERFUEGBAR",
        .italian = "NON PUOI SELEZION. QUESTA PISTA",
        .spanish = "NO PUEDES SELECC. ESTA PISTA"},
    // select_language
    {.english = "SELECT LANGUAGE", .french = "SEL. LANGUE", .german = "SPRACHE WAEHLEN", .italian = "SELEZIONA LINGUA", .spanish = "SELECC. IDIOMA"},
    // select_your_language
    {.english = "SELECT YOUR LANGUAGE", .french = "SEL. VOTRE LANGUE", .german = "BITTE SPRACHE WAEHLEN", .italian = "SELEZIONA LINGUA", .spanish = "SELECCIONA TU IDIOMA"},
    // championship_records_title
    {.english = "CHAMPIONSHIP", .french = "COURSE CHAMPIONNAT", .german = "MEISTERSCHAFTSRENNEN", .italian = "GARA DI CAMPIONATO", .spanish = "CARR. CAMPEONATO"},
    // track
    {.english = "TRACK", .french = "CIRCUIT", .german = "STRECKE", .italian = "PISTA", .spanish = "PISTA"},
    // time
    {.english = "TIME", .french = "TEMPS", .german = "ZEIT", .italian = "TEMPO", .spanish = "TIEMPO"},
    // points
    {.english = "POINTS", .french = "POINTS", .german = "PUNKTE", .italian = "PUNTI", .spanish = "PUNTOS"},
    // fastest_lap
    {.english = "FASTEST LAP", .french = "MEILL. TOUR", .german = "SCHNELLSTE RUNDE", .italian = "GIRO PIU VEL.", .spanish = "MEJOR VUELTA"},
    // by
    {.english = "BY", .french = "PAR", .german = "VON", .italian = "DI", .spanish = "DE"},
    // in
    {.english = "IN", .french = "DANS", .german = "IN", .italian = "IN", .spanish = "EN"},
}};
} // namespace

std::string translated(TextId text, std::uint32_t language) {
    const auto& row = translations.at(static_cast<std::size_t>(text));
    switch (language) {
        case 0:
            return std::string(row.english);
        case 1:
            return std::string(row.french);
        case 2:
            return std::string(row.german);
        case 3:
            return std::string(row.italian);
        case 4:
            return std::string(row.spanish);
        default:
            throw std::runtime_error("Frontend language must be original ID 0..4");
    }
}

const std::array<CarInformation, 10>& cars() {
    static const std::array<CarInformation, 10> data{{
        {.name = "HONDA ACCORD", .drive = DriveType::front_wheel_drive, .valves = 16, .rpm = 0, .power = "N/A"},
        {.name = "AUDI A4", .drive = DriveType::four_wheel_drive, .valves = 16, .rpm = 8250, .power = "296"},
        {.name = "VAUXHALL VECTRA", .drive = DriveType::front_wheel_drive, .valves = 16, .rpm = 8400, .power = "295"},
        {.name = "VOLVO S40", .drive = DriveType::front_wheel_drive, .valves = 20, .rpm = 8500, .power = "300"},
        {.name = "FORD MONDEO", .drive = DriveType::front_wheel_drive, .valves = 24, .rpm = 8500, .power = "295"},
        {.name = "NISSAN PRIMERA", .drive = DriveType::front_wheel_drive, .valves = 16, .rpm = 8300, .power = "297"},
        {.name = "PEUGEOT 406", .drive = DriveType::front_wheel_drive, .valves = 20, .rpm = 8300, .power = "300"},
        {.name = "RENAULT LAGUNA", .drive = DriveType::front_wheel_drive, .valves = 16, .rpm = 0, .power = "N/A"},
        {.name = "TANK", .drive = DriveType::four_wheel_drive, .valves = 16, .rpm = 0, .power = "N/A"},
        {.name = "FLEXMOBILE", .drive = DriveType::rear_wheel_drive, .valves = 20, .rpm = 0, .power = "N/A"},
    }};
    return data;
}

const std::array<TrackInformation, 10>& tracks() {
    static const std::array<TrackInformation, 10> data{{
        {.name = "DONINGTON GP", .location = "DERBY", .metres = 4020, .weather = Weather::cloudy, .lap_time = "9:59.99", .lap_driver = "ANDY", .lap_car = 0},
        {.name = "SILVERSTONE", .location = "NORTHANTS", .metres = 3624, .weather = Weather::storm, .lap_time = "9:59.99", .lap_driver = "LES", .lap_car = 1},
        {.name = "THRUXTON", .location = "HAMPSHIRE", .metres = 3790, .weather = Weather::cloudy, .lap_time = "9:59.99", .lap_driver = "RICH", .lap_car = 2},
        {.name = "BRANDS HATCH", .location = "KENT", .metres = 1930, .weather = Weather::cloudy, .lap_time = "9:59.99", .lap_driver = "SCOTT", .lap_car = 3},
        {.name = "OULTON PARK", .location = "CHESHIRE", .metres = 2660, .weather = Weather::sunny, .lap_time = "9:59.99", .lap_driver = "PAT", .lap_car = 4},
        {.name = "DONINGTON SHORT", .location = "DERBY", .metres = 3150, .weather = Weather::rain, .lap_time = "9:59.99", .lap_driver = "JON", .lap_car = 5},
        {.name = "CROFT", .location = "DARLINGTON", .metres = 3410, .weather = Weather::cloudy, .lap_time = "9:59.99", .lap_driver = "PHIL", .lap_car = 6},
        {.name = "KNOCKHILL", .location = "FIFE", .metres = 2090, .weather = Weather::sunny, .lap_time = "9:59.99", .lap_driver = "BRYAN", .lap_car = 7},
        {.name = "SNETTERTON", .location = "NORFOLK", .metres = 3140, .weather = Weather::cloudy, .lap_time = "9:59.99", .lap_driver = "IAIN", .lap_car = 0},
        {.name = "LAVALAND", .location = "HADES", .metres = 3150, .weather = Weather::sunny, .lap_time = "9:59.99", .lap_driver = "FIRELORD", .lap_car = 1},
    }};
    return data;
}

const LapRecords& lap_records() {
    static const LapRecords data{{
        {.lap_ms = 599990, .driver = "ANDY", .car_id = 0, .code = ""},
        {.lap_ms = 599990, .driver = "LES", .car_id = 1, .code = ""},
        {.lap_ms = 599990, .driver = "RICH", .car_id = 2, .code = ""},
        {.lap_ms = 599990, .driver = "SCOTT", .car_id = 3, .code = ""},
        {.lap_ms = 599990, .driver = "PAT", .car_id = 4, .code = ""},
        {.lap_ms = 599990, .driver = "JON", .car_id = 5, .code = ""},
        {.lap_ms = 599990, .driver = "PHIL", .car_id = 6, .code = ""},
        {.lap_ms = 599990, .driver = "BRYAN", .car_id = 7, .code = ""},
        {.lap_ms = 599990, .driver = "IAIN", .car_id = 0, .code = ""},
        {.lap_ms = 599990, .driver = "FIRELORD", .car_id = 1, .code = ""},
    }};
    return data;
}

const std::array<std::string_view, 10>& record_track_names() {
    static const std::array<std::string_view, 10> data{{
        "DON...",
        "SILV...",
        "THRU...",
        "BRAN...",
        "OULT...",
        "DON.S.",
        "CROF...",
        "KNOC...",
        "SNET...",
        "LAVA...",
    }};
    return data;
}

const std::array<RecordResources::Badge, 10>& record_badges() {
    static const std::array<RecordResources::Badge, 10> data{{
        {.x = 69, .y = 0, .width = 28, .height = 23},
        {.x = 43, .y = 47, .width = 39, .height = 15},
        {.x = 41, .y = 23, .width = 24, .height = 24},
        {.x = 0, .y = 51, .width = 43, .height = 11},
        {.x = 82, .y = 48, .width = 33, .height = 14},
        {.x = 42, .y = 0, .width = 27, .height = 19},
        {.x = 16, .y = 1, .width = 26, .height = 25},
        {.x = 17, .y = 26, .width = 18, .height = 25},
        {.x = 65, .y = 24, .width = 30, .height = 23},
        {.x = 0, .y = 0, .width = 16, .height = 43},
    }};
    return data;
}

const MenuTransitionResources& menu_transition() {
    // Fourteen frames, each with content/title/help quads in canonical corner order.
    // Coordinates include the original canvas translation and content seam correction.
    static const MenuTransitionResources data{
        .frames = {{
            // Frame 0.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 33, .y = 61}, MenuPoint{.x = 33, .y = 421}, MenuPoint{.x = 607, .y = 420}, MenuPoint{.x = 607, .y = 60}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 49, .y = 14}, MenuPoint{.x = 49, .y = 54}, MenuPoint{.x = 599, .y = 54}, MenuPoint{.x = 599, .y = 14}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 57, .y = 441}, MenuPoint{.x = 57, .y = 453}, MenuPoint{.x = 591, .y = 453}, MenuPoint{.x = 591, .y = 441}}}},
            }},
            // Frame 1.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 16, .y = 51}, MenuPoint{.x = 53, .y = 403}, MenuPoint{.x = 589, .y = 402}, MenuPoint{.x = 629, .y = 50}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 32, .y = 8}, MenuPoint{.x = 35, .y = 48}, MenuPoint{.x = 613, .y = 48}, MenuPoint{.x = 616, .y = 8}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 70, .y = 426}, MenuPoint{.x = 71, .y = 437}, MenuPoint{.x = 577, .y = 437}, MenuPoint{.x = 578, .y = 426}}}},
            }},
            // Frame 2.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = -8, .y = 61}, MenuPoint{.x = 71, .y = 382}, MenuPoint{.x = 573, .y = 381}, MenuPoint{.x = 650, .y = 60}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 12, .y = 19}, MenuPoint{.x = 20, .y = 58}, MenuPoint{.x = 628, .y = 59}, MenuPoint{.x = 636, .y = 19}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 83, .y = 397}, MenuPoint{.x = 85, .y = 406}, MenuPoint{.x = 563, .y = 406}, MenuPoint{.x = 565, .y = 397}}}},
            }},
            // Frame 3.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = -37, .y = 87}, MenuPoint{.x = 80, .y = 351}, MenuPoint{.x = 575, .y = 350}, MenuPoint{.x = 667, .y = 86}}}},
                MenuQuad{.corners = {{MenuPoint{.x = -8, .y = 49}, MenuPoint{.x = 4, .y = 89}, MenuPoint{.x = 644, .y = 89}, MenuPoint{.x = 656, .y = 49}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 94, .y = 365}, MenuPoint{.x = 96, .y = 371}, MenuPoint{.x = 552, .y = 371}, MenuPoint{.x = 553, .y = 365}}}},
            }},
            // Frame 4.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = -44, .y = 140}, MenuPoint{.x = 90, .y = 309}, MenuPoint{.x = 555, .y = 308}, MenuPoint{.x = 689, .y = 139}}}},
                MenuQuad{.corners = {{MenuPoint{.x = -24, .y = 113}, MenuPoint{.x = -8, .y = 140}, MenuPoint{.x = 656, .y = 140}, MenuPoint{.x = 672, .y = 113}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 103, .y = 323}, MenuPoint{.x = 104, .y = 327}, MenuPoint{.x = 543, .y = 327}, MenuPoint{.x = 545, .y = 323}}}},
            }},
            // Frame 5.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = -42, .y = 207}, MenuPoint{.x = 93, .y = 271}, MenuPoint{.x = 548, .y = 270}, MenuPoint{.x = 691, .y = 206}}}},
                MenuQuad{.corners = {{MenuPoint{.x = -32, .y = 202}, MenuPoint{.x = -15, .y = 212}, MenuPoint{.x = 663, .y = 212}, MenuPoint{.x = 680, .y = 202}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 108, .y = 281}, MenuPoint{.x = 109, .y = 282}, MenuPoint{.x = 538, .y = 282}, MenuPoint{.x = 540, .y = 281}}}},
            }},
            // Frame 6.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 81, .y = 231}, MenuPoint{.x = -41, .y = 265}, MenuPoint{.x = 690, .y = 264}, MenuPoint{.x = 567, .y = 230}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 104, .y = 210}, MenuPoint{.x = 100, .y = 211}, MenuPoint{.x = 547, .y = 211}, MenuPoint{.x = 544, .y = 210}}}},
                MenuQuad{.corners = {{MenuPoint{.x = -12, .y = 243}, MenuPoint{.x = -15, .y = 244}, MenuPoint{.x = 663, .y = 244}, MenuPoint{.x = 660, .y = 243}}}},
            }},
            // Frame 7.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 98, .y = 180}, MenuPoint{.x = -48, .y = 319}, MenuPoint{.x = 692, .y = 318}, MenuPoint{.x = 543, .y = 179}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 102, .y = 169}, MenuPoint{.x = 95, .y = 178}, MenuPoint{.x = 552, .y = 178}, MenuPoint{.x = 545, .y = 169}}}},
                MenuQuad{.corners = {{MenuPoint{.x = -6, .y = 319}, MenuPoint{.x = -11, .y = 323}, MenuPoint{.x = 659, .y = 323}, MenuPoint{.x = 654, .y = 319}}}},
            }},
            // Frame 8.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 86, .y = 143}, MenuPoint{.x = -44, .y = 374}, MenuPoint{.x = 680, .y = 373}, MenuPoint{.x = 553, .y = 142}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 95, .y = 125}, MenuPoint{.x = 89, .y = 141}, MenuPoint{.x = 559, .y = 141}, MenuPoint{.x = 552, .y = 125}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 4, .y = 384}, MenuPoint{.x = 0, .y = 392}, MenuPoint{.x = 648, .y = 392}, MenuPoint{.x = 644, .y = 384}}}},
            }},
            // Frame 9.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 73, .y = 117}, MenuPoint{.x = -19, .y = 409}, MenuPoint{.x = 659, .y = 408}, MenuPoint{.x = 561, .y = 116}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 86, .y = 87}, MenuPoint{.x = 81, .y = 109}, MenuPoint{.x = 567, .y = 109}, MenuPoint{.x = 561, .y = 87}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 17, .y = 423}, MenuPoint{.x = 14, .y = 434}, MenuPoint{.x = 634, .y = 434}, MenuPoint{.x = 631, .y = 423}}}},
            }},
            // Frame 10.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 71, .y = 94}, MenuPoint{.x = -11, .y = 424}, MenuPoint{.x = 649, .y = 423}, MenuPoint{.x = 565, .y = 93}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 76, .y = 71}, MenuPoint{.x = 72, .y = 99}, MenuPoint{.x = 576, .y = 99}, MenuPoint{.x = 572, .y = 71}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 31, .y = 437}, MenuPoint{.x = 28, .y = 449}, MenuPoint{.x = 620, .y = 449}, MenuPoint{.x = 617, .y = 437}}}},
            }},
            // Frame 11.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 52, .y = 77}, MenuPoint{.x = 4, .y = 428}, MenuPoint{.x = 630, .y = 427}, MenuPoint{.x = 586, .y = 76}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 66, .y = 42}, MenuPoint{.x = 63, .y = 74}, MenuPoint{.x = 585, .y = 74}, MenuPoint{.x = 582, .y = 42}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 41, .y = 447}, MenuPoint{.x = 40, .y = 458}, MenuPoint{.x = 608, .y = 458}, MenuPoint{.x = 607, .y = 447}}}},
            }},
            // Frame 12.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 40, .y = 67}, MenuPoint{.x = 19, .y = 425}, MenuPoint{.x = 617, .y = 424}, MenuPoint{.x = 598, .y = 66}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 57, .y = 24}, MenuPoint{.x = 56, .y = 61}, MenuPoint{.x = 592, .y = 61}, MenuPoint{.x = 591, .y = 24}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 50, .y = 443}, MenuPoint{.x = 49, .y = 455}, MenuPoint{.x = 599, .y = 455}, MenuPoint{.x = 598, .y = 443}}}},
            }},
            // Frame 13.
            std::array<MenuQuad, 3>{{
                MenuQuad{.corners = {{MenuPoint{.x = 36, .y = 60}, MenuPoint{.x = 25, .y = 422}, MenuPoint{.x = 612, .y = 421}, MenuPoint{.x = 604, .y = 59}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 51, .y = 17}, MenuPoint{.x = 51, .y = 56}, MenuPoint{.x = 597, .y = 56}, MenuPoint{.x = 597, .y = 16}}}},
                MenuQuad{.corners = {{MenuPoint{.x = 55, .y = 441}, MenuPoint{.x = 55, .y = 453}, MenuPoint{.x = 593, .y = 453}, MenuPoint{.x = 593, .y = 441}}}},
            }},
        }}};
    return data;
}
} // namespace toca::frontend::builtin
