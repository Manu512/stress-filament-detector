// Detecteur de stress filament - ESP32 Wemos D1 Mini 32
//
// Deux capteurs Hall SS49E lus en differentiel donnent la position du buffer
// entre le MMU et l'extrudeur. Le module expose cette position de deux facons,
// simultanement :
//
//   - deux sorties tout ou rien      -> Happy Hare type D (tension/compression)
//   - une sortie analogique (PWM filtre) -> Happy Hare type P (proportionnel)
//
// Les deux coexistent : on bascule cote Klipper en changeant la config, sans
// reflasher. Type P donne a Happy Hare un AutoTune par EKF au lieu du mode
// "deux niveaux" qui fait osciller en permanence la vitesse du gear, et c'est
// le seul mode qui arme la prevention d'emmelement de FlowGuard en v4.
//
// Principes qui ont guide la reecriture :
//   1. La mesure ne depend jamais du reseau. Voir net.h pour le detail.
//   2. Rien ne bloque dans loop() : ni delay(), ni attente de connexion.
//   3. Tout ce qui decide quelque chose vit dans lib/stress_core, sans Arduino,
//      et se teste sur PC (pio test -e native).

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <LittleFS.h>

#include "net.h"
#include "stress_core.h"

using stress::State;

// --------------------------------------------------------------------------
// Materiel
// --------------------------------------------------------------------------

#define SENSOR_1_PIN 32   // ADC1, capteur Hall S1
#define SENSOR_2_PIN 33   // ADC1, capteur Hall S2
#define OUTPUT_1_PIN 26   // tout ou rien (compression)
#define OUTPUT_2_PIN 25   // tout ou rien (tension) OU sortie analogique

// OUTPUT_2_PIN (GPIO 25) porte l'analogique, OUTPUT_1_PIN reste numerique.
// Ainsi une seule des deux fonctions est perdue, et le mode type D reste
// utilisable en secours sur la sortie restante.
//
// L'analogique est un PWM, et non plus le DAC : il EXIGE le filtre RC (1 kOhm
// en serie, 10 uF vers la masse) entre GPIO 25 et le cable. Sans lui, la MMB
// echantillonnerait un signal carre. Voir stress_core.h pour le pourquoi.
#define ANALOG_OUT_PIN OUTPUT_2_PIN

// Canal LEDC de la sortie analogique. Le 0 est libre : rien d'autre dans ce
// firmware n'utilise LEDC.
constexpr uint8_t kCanalPwm = 0;

static const char* kHostname   = "stress-filament";
static const char* kApPassword = "";   // point d'acces ouvert ; renseigner pour proteger

// --------------------------------------------------------------------------
// Etat
// --------------------------------------------------------------------------

static stress::Calibration cal;
static bool     calValide   = false;
static int32_t  deltaFiltre = 0;
static State    etatCourant = State::Neutral;
static int32_t  dernierRaw1 = 0;
static int32_t  dernierRaw2 = 0;
static uint16_t dernierDuty = stress::kPwmNeutral;

// Sortie analogique active. Quand elle l'est, OUTPUT_2_PIN porte une tension
// continue et ne peut plus servir de sortie logique.
static bool     sortieAnalogique = true;

// Constante de filtrage, en 1/256. 32 donne une reponse douce sans latence
// perceptible a 25 Hz de mesure (200 Hz de lecture brute, moyennee par 8).
static uint16_t alphaQ8 = 32;

static Preferences prefs;
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static bool otaDemarre = false;

// Cadences, toutes non bloquantes
static uint32_t dernierEchantillon = 0;
static uint32_t dernierEnvoi       = 0;
static uint32_t intervalleEnvoiMs  = 200;
static bool     envoyerMaj         = true;
constexpr uint32_t kPeriodeEchantillonMs = 5;   // 200 Hz de lecture brute

// Accumulateur d'echantillons : on moyenne sur N lectures sans jamais bloquer,
// la ou la version d'origine faisait 10 x delay(2), soit 20 ms de blocage.
constexpr uint8_t kEchantillonsParMesure = 8;
static uint32_t accu1 = 0, accu2 = 0;
static uint8_t  nbAccu = 0;

// --------------------------------------------------------------------------
// Persistance
// --------------------------------------------------------------------------

static void chargerCalibration() {
    prefs.begin("hall", true);
    cal.neutral1     = prefs.getInt("cal_raw1",  cal.neutral1);
    cal.neutral2     = prefs.getInt("cal_raw2",  cal.neutral2);
    cal.span         = prefs.getInt("span",      cal.span);
    cal.neutral_zone = prefs.getInt("n_zone",    cal.neutral_zone);
    cal.hysteresis   = prefs.getInt("hyst",      cal.hysteresis);
    alphaQ8          = prefs.getUShort("alpha",  alphaQ8);
    sortieAnalogique = prefs.getBool("analog",   sortieAnalogique);
    calValide        = prefs.getBool("cal_valid", false);
    prefs.end();

    if (!cal.isValid()) {
        // Une calibration incoherente en NVS ne doit pas empecher le module de
        // demarrer : on repart des valeurs par defaut et on le signale.
        cal = stress::Calibration();
        calValide = false;
    }
}

static void enregistrerCalibration() {
    prefs.begin("hall", false);
    prefs.putInt("cal_raw1",  cal.neutral1);
    prefs.putInt("cal_raw2",  cal.neutral2);
    prefs.putInt("span",      cal.span);
    prefs.putInt("n_zone",    cal.neutral_zone);
    prefs.putInt("hyst",      cal.hysteresis);
    prefs.putUShort("alpha",  alphaQ8);
    prefs.putBool("analog",   sortieAnalogique);
    prefs.putBool("cal_valid", calValide);
    prefs.end();
}

// --------------------------------------------------------------------------
// Journalisation vers l'interface web
// --------------------------------------------------------------------------

enum LogCategory { CAT_ALL = 0, CAT_VOLTAGE = 1, CAT_RAW = 2, CAT_OUTPUT = 3, CAT_DBG = 4 };
static LogCategory filtreConsole = CAT_ALL;

static void logToClients(const String& msg, LogCategory cat = CAT_DBG) {
    if (filtreConsole != CAT_ALL && cat != filtreConsole) return;
    JsonDocument d;
    d["type"]     = "log";
    d["category"] = (int)cat;
    d["msg"]      = msg;
    String out;
    serializeJson(d, out);
    ws.textAll(out);
}

// --------------------------------------------------------------------------
// Sorties
// --------------------------------------------------------------------------

static void appliquerSorties(int32_t delta, State etat) {
    if (sortieAnalogique) {
        dernierDuty = stress::deltaToDuty(delta, cal);
        ledcWrite(kCanalPwm, dernierDuty);
        // La sortie restante continue de signaler la compression, ce qui
        // permet de garder un endstop de homing extrudeur cote Klipper.
        digitalWrite(OUTPUT_1_PIN, etat == State::Compression ? HIGH : LOW);
    } else {
        digitalWrite(OUTPUT_1_PIN, etat == State::Compression ? HIGH : LOW);
        digitalWrite(OUTPUT_2_PIN, etat == State::Tension     ? HIGH : LOW);
        dernierDuty = stress::kPwmNeutral;
    }
}

static void mettreSortiesAuRepos() {
    pinMode(OUTPUT_1_PIN, OUTPUT);
    digitalWrite(OUTPUT_1_PIN, LOW);

    if (sortieAnalogique) {
        // Reprendre le pad au DAC avant d'y attacher le PWM. Releve du
        // 2026-10-03 : apres un flash OTA depuis la version DAC, la MMB lisait
        // 0,958 quel que soit le rapport cyclique, et la sortie n'a suivi qu'une
        // fois GPIO 25 repasse par un pinMode. Explication retenue : l'etat du
        // DAC vit dans le domaine RTC, qu'un redemarrage logiciel ne remet pas a
        // zero, et ledcAttachPin ne rend pas le pad au numerique. Sans ces deux
        // lignes, passer d'un firmware DAC a celui-ci sans couper l'alimentation
        // laisse la sortie figee.
        dacDisable(ANALOG_OUT_PIN);
        pinMode(ANALOG_OUT_PIN, OUTPUT);

        // Le canal est attache avec un rapport cyclique nul, corrige a l'appel
        // suivant : le filtre RC absorbe cet instant.
        if (ledcSetup(kCanalPwm, stress::kPwmFreqHz, stress::kPwmBits) == 0) {
            Serial.println("ERREUR: LEDC refuse la frequence/resolution PWM");
        }
        ledcAttachPin(ANALOG_OUT_PIN, kCanalPwm);
        ledcWrite(kCanalPwm, stress::kPwmNeutral);
        dernierDuty = stress::kPwmNeutral;
    } else {
        // Retour en tout ou rien : on rend explicitement le pad a la logique
        // avant de l'ecrire, pour que GPIO 25 ne reste pas sur le PWM.
        ledcDetachPin(ANALOG_OUT_PIN);
        pinMode(OUTPUT_2_PIN, OUTPUT);
        digitalWrite(OUTPUT_2_PIN, LOW);
    }
}

// --------------------------------------------------------------------------
// Mesure, entierement non bloquante
// --------------------------------------------------------------------------

static void echantillonner() {
    const uint32_t maintenant = millis();
    if (maintenant - dernierEchantillon < kPeriodeEchantillonMs) return;
    dernierEchantillon = maintenant;

    // analogReadMilliVolts applique la courbe de calibration gravee en eFuse.
    // L'ADC brut de l'ESP32 est nettement non lineaire, ce qui importait peu
    // pour un simple franchissement de seuil mais fausserait une position
    // continue.
    accu1 += analogReadMilliVolts(SENSOR_1_PIN);
    accu2 += analogReadMilliVolts(SENSOR_2_PIN);
    if (++nbAccu < kEchantillonsParMesure) return;

    dernierRaw1 = accu1 / kEchantillonsParMesure;
    dernierRaw2 = accu2 / kEchantillonsParMesure;
    accu1 = accu2 = 0;
    nbAccu = 0;

    const int32_t brut = stress::computeDelta(dernierRaw1, dernierRaw2, cal);
    deltaFiltre = stress::emaUpdate(deltaFiltre, brut, alphaQ8);

    const State nouvel = stress::nextState(etatCourant, deltaFiltre, cal);
    if (nouvel != etatCourant) {
        etatCourant = nouvel;
        logToClients(etatCourant == State::Compression ? "Position: COMPRESSION"
                   : etatCourant == State::Tension     ? "Position: TENSION"
                                                       : "Position: NEUTRE",
                     CAT_OUTPUT);
    }
    appliquerSorties(deltaFiltre, etatCourant);
}

// --------------------------------------------------------------------------
// Diffusion vers l'interface web
// --------------------------------------------------------------------------

static void notifierClients() {
    if (!envoyerMaj) return;
    const uint32_t maintenant = millis();
    if (maintenant - dernierEnvoi < intervalleEnvoiMs) return;
    dernierEnvoi = maintenant;
    if (ws.count() == 0) return;

    JsonDocument doc;
    doc["type"] = "data";
    // Champs historiques, conserves pour ne pas casser l'interface existante
    doc["raw1"]             = dernierRaw1;
    doc["raw2"]             = dernierRaw2;
    doc["output1"]          = digitalRead(OUTPUT_1_PIN);
    doc["output2"]          = sortieAnalogique ? 0 : digitalRead(OUTPUT_2_PIN);
    doc["neutral_zone"]     = cal.neutral_zone;
    doc["cal_valid"]        = calValide;
    doc["cal_neutral_raw1"] = cal.neutral1;
    doc["cal_neutral_raw2"] = cal.neutral2;
    doc["position"]         = (int)etatCourant;
    // Nouveaux champs
    doc["delta"]            = deltaFiltre;
    doc["span"]             = cal.span;
    doc["hysteresis"]       = cal.hysteresis;
    doc["alpha"]            = alphaQ8;
    doc["pwm"]              = dernierDuty;
    doc["pwm_full"]         = stress::kPwmFull;
    doc["analog_out"]       = sortieAnalogique;
    doc["tension_permille"] = stress::tensionLevelPermille(deltaFiltre, cal);
    const net::Status& n = net::status();
    doc["wifi_phase"]       = (int)n.phase;
    doc["wifi_ip"]          = n.ip;
    doc["wifi_ssid"]        = n.ssid;
    doc["wifi_rssi"]        = n.rssi;

    String out;
    serializeJson(doc, out);
    ws.textAll(out);
}

// --------------------------------------------------------------------------
// Commandes WebSocket
// --------------------------------------------------------------------------

static void traiterCommande(JsonDocument& doc) {
    if (doc["cmd"].is<const char*>()) {
        const String cmd = doc["cmd"].as<String>();

        if (cmd == "capture_neutral") {
            cal.neutral1 = dernierRaw1;
            cal.neutral2 = dernierRaw2;
            deltaFiltre  = 0;
            calValide    = cal.isValid();
            enregistrerCalibration();
            logToClients("Point neutre capture", CAT_OUTPUT);

        } else if (cmd == "set_neutral") {
            // Neutre impose, calcule hors du module a partir des deux butees.
            // Le point neutre de ce buffer n'est PAS une position de repos : le
            // ressort pousse vers la compression, et c'est l'AutoTune de Happy
            // Hare qui maintient le bras au neutre pendant l'impression. On le
            // determine donc comme le milieu mecanique des deux butees, ce qui
            // est objectif et reproductible, au lieu de le capturer a la main.
            if (doc["n1"].is<int>() && doc["n2"].is<int>()) {
                cal.neutral1 = doc["n1"].as<int>();
                cal.neutral2 = doc["n2"].as<int>();
                if (doc["span"].is<int>()) cal.span = doc["span"].as<int>();
                deltaFiltre = 0;
                etatCourant = State::Neutral;
                calValide = cal.isValid();
                if (calValide) {
                    enregistrerCalibration();
                    logToClients("Neutre impose: " + String(cal.neutral1) + "/" +
                                 String(cal.neutral2) + ", span " + String(cal.span), CAT_OUTPUT);
                } else {
                    logToClients("Neutre refuse: valeurs incoherentes", CAT_OUTPUT);
                }
            }

        } else if (cmd == "capture_span") {
            // A executer buffer pousse a fond d'un cote : l'amplitude mesuree
            // devient la pleine echelle. Sans cela, la conversion analogique
            // n'a pas d'echelle de reference.
            const int32_t amplitude = deltaFiltre < 0 ? -deltaFiltre : deltaFiltre;
            if (amplitude > cal.neutral_zone) {
                cal.span  = amplitude;
                calValide = cal.isValid();
                enregistrerCalibration();
                logToClients("Pleine echelle capturee: " + String(cal.span), CAT_OUTPUT);
            } else {
                logToClients("Amplitude trop faible, buffer pas en butee", CAT_OUTPUT);
            }

        } else if (cmd == "save_simple_calibration") {
            if (doc["deadband_points"].is<int>()) cal.neutral_zone = doc["deadband_points"].as<int>();
            if (doc["hysteresis"].is<int>())      cal.hysteresis   = doc["hysteresis"].as<int>();
            if (doc["span"].is<int>())            cal.span         = doc["span"].as<int>();
            if (doc["alpha"].is<int>())           alphaQ8          = doc["alpha"].as<int>();
            calValide = cal.isValid();
            if (!calValide) logToClients("Calibration refusee: valeurs incoherentes", CAT_OUTPUT);
            else            enregistrerCalibration();

        } else if (cmd == "set_analog_output") {
            sortieAnalogique = doc["enabled"].as<bool>();
            mettreSortiesAuRepos();
            enregistrerCalibration();
            logToClients(sortieAnalogique ? "Sortie analogique ACTIVE (type P)"
                                          : "Sorties tout ou rien (type D)", CAT_OUTPUT);

        } else if (cmd == "reset_calibration") {
            cal = stress::Calibration();
            calValide = false;
            enregistrerCalibration();
            logToClients("Calibration reinitialisee", CAT_OUTPUT);

        } else if (cmd == "set_wifi") {
            // Saisie des identifiants depuis le point d'acces de repli : plus
            // besoin de recompiler pour changer de reseau.
            net::saveCredentials(doc["ssid"].as<String>(), doc["password"].as<String>());
            logToClients("Identifiants WiFi enregistres, connexion en cours", CAT_OUTPUT);

        } else if (cmd == "forget_wifi") {
            net::clearCredentials();
            logToClients("Identifiants WiFi effaces", CAT_OUTPUT);
        }
    }

    if (doc["send_updates"].is<bool>())    envoyerMaj        = doc["send_updates"].as<bool>();
    if (doc["interval_ms"].is<uint32_t>()) intervalleEnvoiMs = doc["interval_ms"].as<uint32_t>();
    if (doc["log_category"].is<int>())     filtreConsole     = (LogCategory)doc["log_category"].as<int>();
}

static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType type,
                      void*, uint8_t* data, size_t len) {
    if (type != WS_EVT_DATA) return;
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) return;  // JSON invalide : on ignore
    traiterCommande(doc);
}

// --------------------------------------------------------------------------
// Demarrage
// --------------------------------------------------------------------------

static void demarrerServicesReseau() {
    if (otaDemarre) return;

    ArduinoOTA.setHostname(kHostname);
    // ArduinoOTA demarre mDNS de lui-meme, ce qui republierait un nom en .local.
    // On le desactive : le televersement reseau se fait par IP ou par le nom
    // .lan resolu par le routeur, pas par decouverte mDNS.
    ArduinoOTA.setMdnsEnabled(false);
    ArduinoOTA.begin();

    if (LittleFS.begin(true)) {
        // no-cache : sans cela le navigateur ressert un script.js perime
        // apres une mise a jour, et l'interface reste figee sur les valeurs
        // par defaut du HTML sans qu'aucune erreur ne soit visible.
        server.serveStatic("/", LittleFS, "/")
              .setDefaultFile("index.html")
              .setCacheControl("no-cache");
    }
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();

    otaDemarre = true;
}

void setup() {
    Serial.begin(115200);

    // 1. Le materiel d'abord, toujours. Les sorties partent dans un etat connu
    //    avant meme qu'il soit question de reseau.
    analogReadResolution(12);
    pinMode(SENSOR_1_PIN, INPUT);
    pinMode(SENSOR_2_PIN, INPUT);
    chargerCalibration();
    mettreSortiesAuRepos();

    // 2. Le reseau ensuite, et sans bloquer.
    net::begin(kHostname, kApPassword);

    Serial.printf("Detecteur pret. Calibration %s, sortie %s\n",
                  calValide ? "valide" : "par defaut",
                  sortieAnalogique ? "analogique (type P)" : "tout ou rien (type D)");
}

void loop() {
    echantillonner();     // jamais bloquant
    net::loop();          // machine a etats, jamais bloquante

    if (net::servicesUp()) {
        demarrerServicesReseau();
        ArduinoOTA.handle();
    }

    notifierClients();
    ws.cleanupClients();
}
