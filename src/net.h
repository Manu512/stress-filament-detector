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
    AccessPoint, // repli : point d'acces de configuration
};

struct Status {
    Phase   phase       = Phase::Idle;
    uint32_t attempts   = 0;
    String  ip;
    String  ssid;
};

// Duree d'une tentative avant abandon, et attente entre deux tentatives.
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kRetryDelayMs     = 20000;
// Nombre d'echecs consecutifs avant de basculer en point d'acces.
constexpr uint8_t  kFailuresBeforeAp = 3;

// Charge les identifiants depuis la NVS. Renvoie false si aucun n'est stocke,
// auquel cas begin() partira directement en point d'acces.
bool loadCredentials();

// Enregistre de nouveaux identifiants en NVS et relance la connexion.
void saveCredentials(const String& ssid, const String& password);

// Efface les identifiants stockes.
void clearCredentials();

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
