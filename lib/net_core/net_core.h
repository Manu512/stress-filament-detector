// net_core.h - Retour automatique en station depuis le point d'acces de repli.
//
// Meme principe que stress_core : aucune dependance Arduino, tout ce qui
// decide se teste sur PC (env:native). net.cpp ne fait qu'executer les ordres
// (scan, connexion, arret du point d'acces) et rapporter ce qu'il observe.
//
// Le defaut corrige : apres kFailuresBeforeAp echecs, le module basculait en
// point d'acces et n'en sortait plus. Une coupure du reseau de plus d'une
// minute trente (radio 2,4 GHz du pont WiFi figee, 24/10/2026) le laissait
// hors ligne jusqu'a une coupure d'alimentation, alors que le reseau etait
// revenu depuis longtemps.
//
// Deroule en point d'acces, quand un reseau est enregistre :
//   Attente    au bout de `ecart`, recherche du reseau (scan asynchrone)
//   Recherche  reseau vu : tentative de connexion
//              reseau absent ou scan en echec : attente, ecart ramene au
//              minimum, pour revenir vite quand le reseau reapparait
//   Tentative  connecte : retour en station, le point d'acces est coupe
//              delai depasse : attente, ecart double jusqu'a
//              kEcartMaxMs. Reseau visible mais refus (cle changee) : le point
//              d'acces reste ainsi utilisable pour reconfigurer le module.
// Le point d'acces reste actif pendant la recherche et la tentative. Une
// station connectee, quelle que soit l'etape, ramene en station.

#ifndef NET_CORE_H
#define NET_CORE_H

#include <stdint.h>

namespace netcore {

// Ecart entre deux recherches : minimum (reseau absent) et plafond (reseau
// visible mais connexion refusee, ecart double a chaque refus).
constexpr uint32_t kEcartInitialMs = 30000;
constexpr uint32_t kEcartMaxMs     = 300000;
// En AP+STA, un scan complet depasse les 6 s au bout desquelles le coeur
// Arduino le declare en echec (constat du 24/10) : net.cpp guette donc la fin
// reelle du scan, et c'est ce delai-ci qui le declare bloque.
constexpr uint32_t kRechercheMaxMs = 25000;
// Meme duree qu'une tentative ordinaire (net::kConnectTimeoutMs).
constexpr uint32_t kTentativeMaxMs = 15000;

enum class Etape : uint8_t {
    Attente,
    Recherche,
    Tentative,
};

enum class Ordre : uint8_t {
    Rien,
    LancerRecherche,
    AbandonnerRecherche,  // scan bloque : liberer ses resultats
    LancerTentative,      // connexion station, point d'acces maintenu
    AbandonnerTentative,  // deconnecter la station, garder le point d'acces
    RevenirEnStation,     // couper le point d'acces
};

struct Retour {
    Etape    etape = Etape::Attente;
    uint32_t debut = 0;                // debut de l'etape, en ms (millis())
    uint32_t ecart = kEcartInitialMs;  // attente avant la prochaine recherche
};

// Ce que net.cpp observe a chaque tour de loop().
//
// Pas d'indicateur d'echec de tentative : WiFi.status() garde la valeur de
// l'echec precedent (WL_NO_SSID_AVAIL, celui qui a mene au point d'acces)
// jusqu'au prochain evenement. S'y fier abandonnerait chaque tentative des son
// lancement. Une tentative se juge donc au seul delai, comme en station.
struct Observation {
    uint32_t maintenant     = 0;
    bool     rechercheFinie = false;  // scan termine, avec ou sans resultat
    bool     reseauVu       = false;  // le SSID enregistre est dans les resultats
    bool     connecte       = false;  // station associee, IP obtenue
};

// A l'entree en point d'acces : premiere recherche dans kEcartInitialMs.
void armer(Retour& r, uint32_t maintenant);

// Fait avancer le retour d'un pas et dit quoi faire. Un seul ordre par appel.
Ordre avancer(Retour& r, const Observation& o);

// Vrai si `delai` ms se sont ecoulees depuis `depuis`, y compris a travers le
// retour a zero de millis(), tous les 49,7 jours.
bool echu(uint32_t maintenant, uint32_t depuis, uint32_t delai);

} // namespace netcore

#endif // NET_CORE_H
