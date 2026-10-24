#include "net_core.h"

namespace netcore {

namespace {

void passer(Retour& r, Etape e, uint32_t maintenant) {
    r.etape = e;
    r.debut = maintenant;
}

uint32_t doubler(uint32_t ecart) {
    // Compare avant de multiplier : pas de debordement, meme sur un ecart
    // corrompu.
    return ecart >= kEcartMaxMs / 2 ? kEcartMaxMs : ecart * 2;
}

} // namespace

bool echu(uint32_t maintenant, uint32_t depuis, uint32_t delai) {
    // La soustraction non signee reste juste quand millis() repasse par zero.
    return maintenant - depuis >= delai;
}

void armer(Retour& r, uint32_t maintenant) {
    r.ecart = kEcartInitialMs;
    passer(r, Etape::Attente, maintenant);
}

Ordre avancer(Retour& r, const Observation& o) {
    if (o.connecte) {
        armer(r, o.maintenant);
        return Ordre::RevenirEnStation;
    }

    switch (r.etape) {

    case Etape::Attente:
        if (!echu(o.maintenant, r.debut, r.ecart)) return Ordre::Rien;
        passer(r, Etape::Recherche, o.maintenant);
        return Ordre::LancerRecherche;

    case Etape::Recherche:
        if (o.rechercheFinie) {
            if (o.reseauVu) {
                passer(r, Etape::Tentative, o.maintenant);
                return Ordre::LancerTentative;
            }
            r.ecart = kEcartInitialMs;
            passer(r, Etape::Attente, o.maintenant);
            return Ordre::Rien;
        }
        if (!echu(o.maintenant, r.debut, kRechercheMaxMs)) return Ordre::Rien;
        passer(r, Etape::Attente, o.maintenant);
        return Ordre::AbandonnerRecherche;

    case Etape::Tentative:
        if (!echu(o.maintenant, r.debut, kTentativeMaxMs)) return Ordre::Rien;
        r.ecart = doubler(r.ecart);
        passer(r, Etape::Attente, o.maintenant);
        return Ordre::AbandonnerTentative;
    }

    // Etape corrompue : on repart proprement plutot que de rester bloque.
    armer(r, o.maintenant);
    return Ordre::Rien;
}

} // namespace netcore
