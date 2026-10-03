#include "stress_core.h"

namespace stress {

namespace {

int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

} // namespace

bool Calibration::isValid() const {
    // span nul ou negatif : la conversion DAC diviserait par zero ou
    // s'inverserait. neutral_zone negative : la machine a etats n'aurait plus
    // de zone neutre. hysteresis > neutral_zone : le seuil de sortie
    // deviendrait negatif, donc on ne quitterait jamais un etat.
    if (span <= 0) return false;
    // Borne haute : le delta est une difference de deux lectures en mV sur une
    // alimentation 3,3 V, il ne peut pas depasser quelques milliers. Au-dela, la
    // valeur est corrompue - et elle entre dans un produit au sein de
    // deltaToDac, ou un span aberrant ferait deborder l'entier signe.
    if (span > 4095) return false;
    if (neutral_zone < 0) return false;
    if (hysteresis < 0 || hysteresis >= neutral_zone + 1) return false;
    // Les points neutres doivent rester dans la plage ADC 12 bits plausible.
    if (neutral1 < 0 || neutral1 > 4095) return false;
    if (neutral2 < 0 || neutral2 > 4095) return false;
    return true;
}

bool assignIfValid(Calibration& target, const Calibration& candidate) {
    if (!candidate.isValid()) return false;
    target = candidate;
    return true;
}

bool isValidAlpha(int32_t alpha_q8) {
    return alpha_q8 >= 1 && alpha_q8 <= 256;
}

int32_t computeDelta(int32_t raw1, int32_t raw2, const Calibration& cal) {
    return (raw1 - cal.neutral1) - (raw2 - cal.neutral2);
}

int32_t emaUpdate(int32_t previous, int32_t sample, uint16_t alpha_q8) {
    if (alpha_q8 >= 256) return sample;   // pas de filtrage
    if (alpha_q8 == 0)   return previous; // fige

    const int32_t ecart = sample - previous;
    if (ecart == 0) return previous;

    // previous + alpha * ecart, en virgule fixe 8 bits.
    // Arrondi au plus proche, en s'ecartant de zero : une simple division
    // entiere tronque vers zero et laisse un biais permanent.
    const int32_t num = ecart * static_cast<int32_t>(alpha_q8);
    int32_t increment = (num >= 0 ? num + 128 : num - 128) / 256;

    // Zone morte : sous 256/alpha unites d'ecart, l'increment arrondi retombe
    // a zero et le filtre se figerait avant d'atteindre la consigne. On force
    // alors le plus petit pas possible pour garantir la convergence.
    if (increment == 0) increment = (ecart > 0) ? 1 : -1;

    return previous + increment;
}

uint8_t deltaToDac(int32_t delta, const Calibration& cal) {
    if (!cal.isValid()) return kDacNeutral; // repli sur, jamais de saturation
    const int32_t borne = clamp(delta, -cal.span, cal.span);
    // Echelle : -span -> kDacMin, 0 -> kDacNeutral, +span -> kDacMax.
    //
    // On compte a partir de kDacMin, et non en ecart signe autour du neutre : le
    // numerateur reste alors positif, un seul arrondi au plus proche suffit, et
    // la question du sens de troncature ne se pose plus. Surtout, les deux
    // extremites tombent exactement sur les bornes, ce que la forme signee ne
    // garantit pas quand la plage compte un nombre impair de pas - et 160..255
    // en compte 95.
    const int32_t plage = static_cast<int32_t>(kDacMax) - static_cast<int32_t>(kDacMin);
    const int32_t denom = 2 * cal.span;
    const int32_t num   = (borne + cal.span) * plage;  // >= 0 par construction
    const int32_t v     = static_cast<int32_t>(kDacMin) + (num + denom / 2) / denom;
    return static_cast<uint8_t>(clamp(v, static_cast<int32_t>(kDacMin),
                                      static_cast<int32_t>(kDacMax)));
}

State nextState(State current, int32_t delta, const Calibration& cal) {
    if (!cal.isValid()) return State::Neutral;

    const int32_t entree = cal.neutral_zone;                   // seuil d'entree
    const int32_t sortie = cal.neutral_zone - cal.hysteresis;  // seuil de sortie

    switch (current) {
    case State::Compression:
        // On reste en compression tant qu'on n'est pas retombe sous le seuil
        // de sortie, plus bas que le seuil d'entree : c'est l'hysteresis.
        return (delta > sortie) ? State::Compression : State::Neutral;

    case State::Tension:
        return (delta < -sortie) ? State::Tension : State::Neutral;

    case State::Neutral:
    default:
        if (delta > entree)  return State::Compression;
        if (delta < -entree) return State::Tension;
        return State::Neutral;
    }
}

int32_t tensionLevelPermille(int32_t delta, const Calibration& cal) {
    if (!cal.isValid()) return 0;
    const int32_t amplitude = delta < 0 ? -delta : delta;
    const int32_t borne = clamp(amplitude, 0, cal.span);
    return (borne * 1000) / cal.span;
}

} // namespace stress
