#include "net.h"
#include "net_core.h"

#include <WiFi.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <string.h>

namespace net {

namespace {

Preferences prefs;
Status      etat;

String   cfgSsid;
String   cfgPass;
String   apPass;
String   host;
// SSID du reseau enregistre, guette depuis le point d'acces de repli. Vide pour
// un module neuf : le point d'acces est alors sa seule porte d'entree.
String   cible;

uint32_t phaseDebut   = 0;
uint8_t  echecsSuite  = 0;

netcore::Retour retour;
bool            retourPossible = false;
uint32_t        debutScan      = 0;

// L'espace NVS est distinct de celui de la calibration ("hall") pour qu'un
// effacement des identifiants ne touche pas aux reglages capteurs.
constexpr const char* kNvsSpace = "net";

// Pas de mDNS, donc pas de publication en .local : le module s'annonce par son
// nom d'hote dans le bail DHCP, et c'est dnsmasq sur le routeur qui le resout en
// stress-filament.lan. C'est delibere : .local est reserve au mDNS et n'est pas
// le domaine de ce reseau.

void passerEn(Phase p) {
    etat.phase = p;
    phaseDebut = millis();
}

// Connexion station, sans changer de mode : sert en station seule comme en
// AP+STA, depuis le point d'acces de repli.
void connecterStation() {
    if (!cfgSsid.isEmpty()) {
        // Identifiants connus de notre espace NVS.
        WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
    } else {
        // Aucun identifiant chez nous, mais le pilote en a peut-etre. L'IDF
        // conserve la configuration station dans sa propre NVS (namespace
        // nvs.net80211) : WiFi.begin() sans argument la reutilise. C'est ce qui
        // permet a un module deja configure de se reconnecter apres une mise a
        // jour du firmware, sans qu'on ait a lire ni transporter le mot de passe.
        WiFi.begin();
    }
}

// SSID du reseau enregistre : le notre, sinon celui que le pilote a garde.
// WiFi.SSID() ne convient pas : il ne donne que le reseau auquel la station est
// deja associee, donc rien pendant une tentative. Station active requise.
String reseauEnregistre() {
    if (!cfgSsid.isEmpty()) return cfgSsid;
    wifi_config_t conf;
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return String();
    // 32 octets sans zero final quand le SSID fait 32 caracteres.
    char ssid[sizeof(conf.sta.ssid) + 1];
    memcpy(ssid, conf.sta.ssid, sizeof(conf.sta.ssid));
    ssid[sizeof(conf.sta.ssid)] = '\0';
    return String(ssid);
}

// Interrompt une recherche du repli et libere ses resultats.
void arreterRecherche() {
    esp_wifi_scan_stop();
    WiFi.scanDelete();
}

void lancerTentative() {
    // Appel possible depuis le point d'acces (nouveaux identifiants) : une
    // recherche du retour automatique ne doit pas gener la connexion.
    if (etat.phase == Phase::AccessPoint) arreterRecherche();
    WiFi.setAutoReconnect(true);   // coupee en point d'acces, voir basculerEnAp()
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(host.c_str());

    connecterStation();
    cible     = reseauEnregistre();
    etat.ssid = cible;

    etat.attempts++;
    passerEn(Phase::Connecting);
}

void basculerEnAp() {
    WiFi.disconnect(true);
    // Un reseau est enregistre : la station reste active a cote du point
    // d'acces pour guetter son retour (net_core.h). La reconnexion automatique
    // du coeur Arduino est coupee : elle relancerait la station en boucle sur
    // un reseau absent, et chaque essai balaie les canaux au detriment du point
    // d'acces. C'est net_core qui decide quand chercher et quand se connecter.
    retourPossible = !cible.isEmpty();
    etat.retour.actif = retourPossible;
    WiFi.setAutoReconnect(false);
    WiFi.mode(retourPossible ? WIFI_AP_STA : WIFI_AP);
    WiFi.setHostname(host.c_str());
    // SSID lisible et stable, pour retrouver le module sans scanner.
    const String apSsid = String("stress-filament-") +
                          String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
    WiFi.softAP(apSsid.c_str(), apPass.isEmpty() ? nullptr : apPass.c_str());
    etat.ssid = apSsid;
    etat.ip   = WiFi.softAPIP().toString();
    etat.rssi = 0;
    netcore::armer(retour, millis());
    passerEn(Phase::AccessPoint);
}

// Retour automatique en station : net_core decide, on execute. Rien ici ne
// bloque : scan et connexion sont asynchrones, la mesure continue.
void suivreRetour(uint32_t maintenant) {
    netcore::Observation o;
    o.maintenant = maintenant;
    o.connecte   = WiFi.status() == WL_CONNECTED;

    if (retour.etape == netcore::Etape::Recherche &&
        (WiFi.getStatusBits() & WIFI_SCAN_DONE_BIT)) {
        // Fin du scan jugee sur le bit pose par l'evenement SCAN_DONE, pas sur
        // scanComplete() seul : le coeur declare un scan en echec au bout de
        // 20 x 300 ms = 6 s, or en AP+STA un scan complet dure plus longtemps.
        // Constat du 24/10 : chaque recherche rendait -2 alors que le scan
        // aboutissait ensuite, et le reseau n'etait jamais vu. Le bit leve,
        // _scanDone() a remis le chrono du coeur a zero : scanComplete() rend
        // alors le nombre de reseaux. net_core borne l'attente.
        const int16_t n = WiFi.scanComplete();
        o.rechercheFinie = true;
        for (int16_t i = 0; i < n && !o.reseauVu; ++i) {
            o.reseauVu = WiFi.SSID(i) == cible;
        }
        WiFi.scanDelete();
        etat.retour.scanFini = n;
        etat.retour.scanMs   = maintenant - debutScan;
        if (o.reseauVu) etat.retour.vu++;
    }

    const netcore::Ordre ordre = netcore::avancer(retour, o);
    etat.retour.etape = (uint8_t)retour.etape;
    etat.retour.mode  = (uint8_t)WiFi.getMode();
    etat.retour.bits  = (uint32_t)WiFi.getStatusBits();

    switch (ordre) {
    case netcore::Ordre::LancerRecherche:
        // asynchrone, 300 ms au plus par canal
        debutScan = maintenant;
        etat.retour.scanLance = WiFi.scanNetworks(true);
        etat.retour.recherches++;
        break;
    case netcore::Ordre::AbandonnerRecherche:
        arreterRecherche();
        break;
    case netcore::Ordre::LancerTentative:
        connecterStation();
        etat.retour.tentatives++;
        break;
    case netcore::Ordre::AbandonnerTentative:
        WiFi.disconnect();         // la station seule : le point d'acces reste
        break;
    case netcore::Ordre::RevenirEnStation:
        WiFi.softAPdisconnect(true);
        WiFi.setAutoReconnect(true);
        etat.retour.actif = false;
        etat.retour.mode  = (uint8_t)WiFi.getMode();
        etat.retour.bits  = (uint32_t)WiFi.getStatusBits();
        etat.ssid   = cible;
        etat.ip     = WiFi.localIP().toString();
        etat.rssi   = WiFi.RSSI();
        echecsSuite = 0;
        passerEn(Phase::Connected);
        break;
    case netcore::Ordre::Rien:
        break;
    }
}

} // namespace

bool loadCredentials() {
    prefs.begin(kNvsSpace, true);
    cfgSsid = prefs.getString("ssid", "");
    cfgPass = prefs.getString("pass", "");
    prefs.end();
    return !cfgSsid.isEmpty();
}

void saveCredentials(const String& ssid, const String& password) {
    prefs.begin(kNvsSpace, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
    cfgSsid = ssid;
    cfgPass = password;
    echecsSuite = 0;
    lancerTentative();
}

void clearCredentials() {
    prefs.begin(kNvsSpace, false);
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.end();
    cfgSsid = "";
    cfgPass = "";
}

String accessKey() {
    if (!cfgSsid.isEmpty()) return cfgPass;
    // Identifiants connus du seul pilote WiFi : voir lancerTentative().
    return WiFi.psk();
}

void begin(const char* hostname, const char* apPassword) {
    host   = hostname ? hostname : "stress-filament";
    apPass = apPassword ? apPassword : "";
    loadCredentials();
    // On tente toujours : soit avec nos identifiants, soit avec ceux que le
    // pilote a deja en memoire. Le repli en point d'acces n'intervient qu'apres
    // kFailuresBeforeAp echecs, donc jamais au prix d'une coupure inutile.
    lancerTentative();
}

void loop() {
    const uint32_t maintenant = millis();

    switch (etat.phase) {

    case Phase::Connecting:
        if (WiFi.status() == WL_CONNECTED) {
            etat.ip   = WiFi.localIP().toString();
            etat.rssi = WiFi.RSSI();
            echecsSuite = 0;
            passerEn(Phase::Connected);
        } else if (maintenant - phaseDebut >= kConnectTimeoutMs) {
            WiFi.disconnect(true);
            if (++echecsSuite >= kFailuresBeforeAp) {
                basculerEnAp();
            } else {
                passerEn(Phase::Retrying);
            }
        }
        break;

    case Phase::Connected:
        if (WiFi.status() != WL_CONNECTED) {
            // Perte de lien : on repart en tentative, sans jamais bloquer la
            // mesure. C'est le scenario d'un redemarrage de box ou du pont.
            etat.ip   = "";
            etat.rssi = 0;
            passerEn(Phase::Retrying);
        } else {
            // Rafraichi en continu : c'est ce qui permet de regler la puissance
            // d'emission de l'AP en la mesurant au lieu de la deduire.
            etat.rssi = WiFi.RSSI();
        }
        break;

    case Phase::Retrying:
        if (maintenant - phaseDebut >= kRetryDelayMs) lancerTentative();
        break;

    case Phase::AccessPoint:
        // Reseau enregistre : on guette son retour. Module neuf : on reste en
        // point d'acces jusqu'a ce que saveCredentials() relance une tentative.
        if (retourPossible) suivreRetour(maintenant);
        break;

    case Phase::Idle:
    default:
        break;
    }
}

const Status& status() { return etat; }

bool servicesUp() {
    return etat.phase == Phase::Connected || etat.phase == Phase::AccessPoint;
}

} // namespace net
