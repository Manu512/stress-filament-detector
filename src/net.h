// net.h - Reseau non bloquant pour le detecteur de stress filament.
//
// Regle de conception : le reseau ne conditionne JAMAIS la mesure.
// La version d'origine appelait WiFi.begin() dans setup() puis bouclait sur
// `while (WiFi.status() != WL_CONNECTED) delay(500);` sans sortie. Tant que le
// WiFi ne repondait pas, loop() n'etait jamais atteint : pas de lecture des
// capteurs, pas de sorties vers la MMB, et pas d'OTA pour se rattraper. Les
// sorties restaient a l'etat laisse par setupHardware(), donc "neutre" en
// permanence, et Happy Hare croyait le buffer au repos. Panne silencieuse.
//
// Ici la connexion est une machine a etats appelee depuis loop(), et les
// identifiants vivent en NVS : plus de recompilation pour changer de SSID.

#ifndef NET_H
#define NET_H

#include <Arduino.h>

namespace net {

enum class Phase : uint8_t {
    Idle,        // pas encore demarre
    Connecting,  // tentative en cours
    Connected,   // associe, IP obtenue
    Retrying,    // echec, attente avant nouvel essai
    AccessPoint, // repli : point d'acces de configuration, qui guette le
                 // retour du reseau enregistre et y revient seul (net_core.h)
};

// Compteurs du retour automatique en station, publies dans le JSON : sans
// liaison serie, c'est le seul moyen de voir ou il en est.
struct DiagRetour {
    bool     actif       = false;  // un reseau enregistre est guette
    uint8_t  etape       = 0;      // netcore::Etape
    uint8_t  mode        = 0;      // WiFi.getMode() : 1 STA, 2 AP, 3 AP+STA
    uint32_t recherches  = 0;      // scans demandes
    int16_t  scanLance   = 0;      // retour du dernier scanNetworks() (-1 lance, -2 refuse)
    int16_t  scanFini    = 0;      // dernier scanComplete() termine (>= 0 : reseaux vus)
    uint32_t scanMs      = 0;      // duree du dernier scan termine
    uint32_t vu          = 0;      // recherches ou le reseau enregistre figurait
    uint32_t tentatives  = 0;
    uint32_t bits        = 0;      // bits d'etat WiFi du coeur, au dernier tour
};

struct Status {
    Phase   phase       = Phase::Idle;
    uint32_t attempts   = 0;
    String  ip;
    String  ssid;
    // Signal recu, en dBm. Rafraichi a chaque loop() tant que la connexion
    // tient. Vaut 0 hors connexion station : en point d'acces la notion n'a
    // pas de sens, et WiFi.RSSI() y renvoie une valeur arbitraire.
    int8_t  rssi        = 0;
    DiagRetour retour;
};

// Duree d'une tentative avant abandon, et attente entre deux tentatives.
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kRetryDelayMs     = 20000;
// Nombre d'echecs consecutifs avant de basculer en point d'acces.
constexpr uint8_t  kFailuresBeforeAp = 3;

// Charge les identifiants depuis la NVS. Renvoie false si aucun n'est stocke,
// auquel cas begin() tente quand meme une connexion, avec ceux que le pilote
// WiFi a conserves. Le point d'acces n'arrive qu'apres kFailuresBeforeAp echecs.
bool loadCredentials();

// Enregistre de nouveaux identifiants en NVS et relance la connexion.
void saveCredentials(const String& ssid, const String& password);

// Efface les identifiants stockes.
void clearCredentials();

// Cle qui protege les ecritures et l'OTA : celle du reseau WiFi enregistre.
// Vide tant que le module n'a aucun reseau (module neuf) ou si ce reseau est
// ouvert, auquel cas rien n'est verrouille : sinon un module neuf, joignable
// seulement par son point d'acces, ne pourrait plus etre configure.
String accessKey();

// A appeler une fois, APRES l'initialisation des capteurs et des sorties.
// Ne bloque pas : se contente d'armer la machine a etats.
void begin(const char* hostname, const char* apPassword);

// A appeler a chaque tour de loop(). Ne bloque jamais.
void loop();

const Status& status();

// Vrai des que le module est joignable, en station comme en point d'acces.
bool servicesUp();

} // namespace net

#endif // NET_H
