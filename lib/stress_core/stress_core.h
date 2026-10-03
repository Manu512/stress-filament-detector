// stress_core.h - Logique de mesure du detecteur de stress filament.
//
// Volontairement sans aucune dependance Arduino : tout ce qui decide quelque
// chose vit ici et se teste sur PC (env:native). main.cpp ne fait que lire les
// ADC, ecrire les sorties et servir l'interface web.
//
// Convention de signe, valable partout dans ce fichier :
//     delta > 0  ->  COMPRESSION  (buffer comprime, le filament pousse)
//     delta < 0  ->  TENSION      (buffer etire, le filament tire)
//     delta = 0  ->  NEUTRE
//
#ifndef STRESS_CORE_H
#define STRESS_CORE_H

#include <stdint.h>

namespace stress {

// Etat discret, pour les deux sorties tout ou rien conservees en parallele de
// la sortie analogique (permet de basculer type D / type P cote Klipper sans
// reflasher l'ESP32).
enum class State : uint8_t {
    Neutral     = 0,
    Compression = 1,
    Tension     = 2,
};

// Calibration, persistee en NVS.
//
// ATTENTION A L'UNITE : le firmware lit desormais en MILLIVOLTS, via
// analogReadMilliVolts(), et non plus en comptes ADC bruts. Les valeurs par
// defaut ci-dessous sont donc en mV. Reprendre telles quelles les anciennes
// valeurs en comptes bruts (2000 / 1993) donnerait un delta d'une centaine
// d'unites au repos, et un etat COMPRESSION permanent.
//
// Ces defauts ne sont que des ordres de grandeur relevés sur la machine le
// 2026-09-24. Ils NE REMPLACENT PAS une calibration : tant que `cal_valid`
// est faux, la mesure n'a pas de reference.
struct Calibration {
    int32_t neutral1     = 1751;  // lecture de S1 au repos, en mV
    int32_t neutral2     = 1639;  // lecture de S2 au repos, en mV
    int32_t span         = 150;   // amplitude du delta a pleine echelle (> 0)
    int32_t neutral_zone = 16;    // demi-largeur de la zone neutre, en delta
    int32_t hysteresis   = 8;     // marge supplementaire pour quitter un etat

    // Une calibration incoherente ferait diverger la conversion analogique ou
    // bloquerait la machine a etats. On la valide avant de s'en servir.
    bool isValid() const;
};

// Sortie analogique : rapport cyclique d'un PWM, lisse par un filtre RC
// (1 kOhm + 10 uF) soude cote module, entre GPIO 25 et le cable vers la MMB.
//
// Pourquoi plus le DAC. Son buffer de sortie sait fournir du courant, pas en
// absorber. L'entree STP8 de la MMB CAN porte un tirage vers le haut (5 a 8
// kOhm, deduit d'une mesure), et le DAC ne parvenait pas a descendre le noeud
// sous environ 1,9 V : la courbe s'inversait sous dac 144, ce qui limitait la
// sortie a la plage 160..255, soit 96 pas. Une sortie logique est push-pull :
// la MEME broche pilotee en numerique atteint 0,017 V et 3,300 V. Le PWM en
// herite, et le filtre en fait une tension continue.
//
// 11 bits a 20 kHz : l'horloge du peripherique LEDC est a 80 MHz, soit 4000
// pas par periode. 11 bits (2048 pas) est la plus grande resolution qui tient.
constexpr uint8_t  kPwmBits   = 11;
constexpr uint32_t kPwmFreqHz = 20000;
constexpr uint16_t kPwmFull   = (1u << kPwmBits) - 1;   // 2047, soit 100 %

// On reste a 5 % des deux rails. A 0 % ou 100 % la broche ne commute plus, et
// rien ne distinguerait alors une butee d'une sortie figee ou d'un fil coupe.
//
// Les tensions reellement lues par la MMB dependent du pont forme par la
// resistance du filtre et le tirage de l'entree : elles se MESURENT (bornes
// sync_feedback_analog_* de Happy Hare), elles ne se deduisent pas d'ici.
constexpr uint16_t kPwmMin = 102;    //  5 % de 2047
constexpr uint16_t kPwmMax = 1945;   // 95 % de 2047

// Neutre au milieu de la plage utile. De 102 a 1945 il y a 1844 valeurs, dont
// le centre exact tombe a 1023,5 : on prend 1024, et deltaToDuty arrondit de
// meme.
constexpr uint16_t kPwmNeutral = kPwmMin + (kPwmMax - kPwmMin + 1) / 2;

// Delta signe a partir des deux lectures brutes.
//
// La soustraction des deux ecarts au neutre annule le mode commun : derive
// thermique des SS49E, variation d'alimentation, offset. C'est ce qui rend la
// mesure differentielle plus robuste qu'un capteur unique, et c'est deja le
// principe de la version d'origine - simplement, elle jetait la valeur.
int32_t computeDelta(int32_t raw1, int32_t raw2, const Calibration& cal);

// Moyenne glissante exponentielle en entier.
//   alpha_q8 = 256 -> aucun filtrage (sortie = echantillon)
//   alpha_q8 = 1   -> filtrage tres fort
// Pas de flottant, pas de division : l'operation tourne a chaque tour de boucle.
int32_t emaUpdate(int32_t previous, int32_t sample, uint16_t alpha_q8);

// Conversion du delta en rapport cyclique PWM, lineaire et saturee sur la plage
// utile. -span tombe exactement sur kPwmMin, +span sur kPwmMax, 0 sur
// kPwmNeutral. La sortie ne quitte JAMAIS [kPwmMin, kPwmMax] (voir le
// commentaire de kPwmMin).
uint16_t deltaToDuty(int32_t delta, const Calibration& cal);

// Transition d'etat avec hysteresis : il faut depasser neutral_zone pour
// entrer dans un etat, et repasser sous (neutral_zone - hysteresis) pour en
// sortir. Evite le battement quand le buffer flotte autour d'un seuil.
State nextState(State current, int32_t delta, const Calibration& cal);

// Niveau de tension normalise 0..1000 (millieme), tel que Happy Hare le
// presente dans ses seuils tangle_prevention_* (exprimes en 0.2 .. 0.9).
// Renvoie 0 au neutre et croit avec |delta|, quel que soit le signe.
int32_t tensionLevelPermille(int32_t delta, const Calibration& cal);

} // namespace stress

#endif // STRESS_CORE_H
