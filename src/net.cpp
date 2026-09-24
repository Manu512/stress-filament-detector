#include "net.h"

#include <WiFi.h>
#include <Preferences.h>

namespace net {

namespace {

Preferences prefs;
Status      etat;

String   cfgSsid;
String   cfgPass;
String   apPass;
String   host;

uint32_t phaseDebut   = 0;
uint8_t  echecsSuite  = 0;

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

void lancerTentative() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(host.c_str());

    if (!cfgSsid.isEmpty()) {
        // Identifiants connus de notre espace NVS.
        WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
        etat.ssid = cfgSsid;
    } else {
        // Aucun identifiant chez nous, mais le pilote en a peut-etre. L'IDF
        // conserve la configuration station dans sa propre NVS (namespace
        // nvs.net80211) : WiFi.begin() sans argument la reutilise. C'est ce qui
        // permet a un module deja configure de se reconnecter apres une mise a
        // jour du firmware, sans qu'on ait a lire ni transporter le mot de passe.
        WiFi.begin();
        etat.ssid = WiFi.SSID();
    }

    etat.attempts++;
    passerEn(Phase::Connecting);
}

void basculerEnAp() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    // SSID lisible et stable, pour retrouver le module sans scanner.
    const String apSsid = String("stress-filament-") +
                          String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
    WiFi.softAP(apSsid.c_str(), apPass.isEmpty() ? nullptr : apPass.c_str());
    etat.ssid = apSsid;
    etat.ip   = WiFi.softAPIP().toString();
    passerEn(Phase::AccessPoint);
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
            etat.ip = WiFi.localIP().toString();
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
            etat.ip = "";
            passerEn(Phase::Retrying);
        }
        break;

    case Phase::Retrying:
        if (maintenant - phaseDebut >= kRetryDelayMs) lancerTentative();
        break;

    case Phase::AccessPoint:
        // On reste en point d'acces jusqu'a ce que de nouveaux identifiants
        // soient enregistres : saveCredentials() relance une tentative.
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
