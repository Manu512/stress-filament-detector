// Tests du retour automatique en station, joues sur PC (pio test -e native).
// Aucune dependance materielle : ni ESP32, ni WiFi, ni horloge reelle.

#include <unity.h>
#include "net_core.h"

using namespace netcore;

void setUp(void) {}
void tearDown(void) {}

static const uint32_t T0 = 1000000;

static Observation obs(uint32_t maintenant) {
    Observation o;
    o.maintenant = maintenant;
    return o;
}

static Retour arme(uint32_t maintenant = T0) {
    Retour r;
    armer(r, maintenant);
    return r;
}

// Amene un retour arme a T0 jusqu'a l'etape Tentative, et rend l'instant.
static uint32_t jusquaTentative(Retour& r) {
    uint32_t t = r.debut + r.ecart;
    avancer(r, obs(t));
    Observation o = obs(t + 2000);
    o.rechercheFinie = true;
    o.reseauVu       = true;
    avancer(r, o);
    return t + 2000;
}

// --------------------------------------------------------------------------
// echu
// --------------------------------------------------------------------------

void test_echu_a_la_borne_exacte() {
    TEST_ASSERT_FALSE(echu(T0 + 29999, T0, 30000));
    TEST_ASSERT_TRUE(echu(T0 + 30000, T0, 30000));
}

void test_echu_a_travers_le_retour_a_zero_de_millis() {
    const uint32_t avant = 0xFFFFF000u;
    TEST_ASSERT_FALSE(echu(avant + 1000, avant, 30000));
    // 0xFFFFF000 + 30000 deborde : c'est le cas des 49,7 jours.
    TEST_ASSERT_TRUE(echu(avant + 30000, avant, 30000));
    TEST_ASSERT_TRUE(echu(0x00010000u, avant, 30000));
}

// --------------------------------------------------------------------------
// Attente
// --------------------------------------------------------------------------

void test_armer_met_en_attente_a_l_ecart_minimal() {
    Retour r;
    r.etape = Etape::Tentative;
    r.ecart = 120000;
    armer(r, T0);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Attente, (uint8_t)r.etape);
    TEST_ASSERT_EQUAL_UINT32(kEcartInitialMs, r.ecart);
    TEST_ASSERT_EQUAL_UINT32(T0, r.debut);
}

void test_attente_ne_fait_rien_avant_l_ecart() {
    Retour r = arme();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::Rien, (uint8_t)avancer(r, obs(T0 + kEcartInitialMs - 1)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Attente, (uint8_t)r.etape);
}

void test_attente_lance_la_recherche_a_l_ecart() {
    Retour r = arme();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::LancerRecherche, (uint8_t)avancer(r, obs(T0 + kEcartInitialMs)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Recherche, (uint8_t)r.etape);
}

void test_attente_a_travers_le_retour_a_zero_de_millis() {
    Retour r = arme(0xFFFF0000u);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::Rien, (uint8_t)avancer(r, obs(0xFFFF0000u + 1000)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::LancerRecherche,
                            (uint8_t)avancer(r, obs(0xFFFF0000u + kEcartInitialMs)));
}

// --------------------------------------------------------------------------
// Recherche
// --------------------------------------------------------------------------

void test_reseau_vu_lance_la_tentative() {
    Retour r = arme();
    avancer(r, obs(T0 + kEcartInitialMs));
    Observation o = obs(T0 + kEcartInitialMs + 2000);
    o.rechercheFinie = true;
    o.reseauVu       = true;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::LancerTentative, (uint8_t)avancer(r, o));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Tentative, (uint8_t)r.etape);
}

void test_reseau_absent_ramene_l_ecart_au_minimum() {
    Retour r = arme();
    r.ecart = 240000;   // apres plusieurs refus
    avancer(r, obs(T0 + 240000));
    Observation o = obs(T0 + 242000);
    o.rechercheFinie = true;   // reseauVu reste faux : absent, ou scan en echec
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::Rien, (uint8_t)avancer(r, o));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Attente, (uint8_t)r.etape);
    TEST_ASSERT_EQUAL_UINT32(kEcartInitialMs, r.ecart);
    TEST_ASSERT_EQUAL_UINT32(T0 + 242000, r.debut);
}

void test_recherche_en_cours_attend() {
    Retour r = arme();
    avancer(r, obs(T0 + kEcartInitialMs));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::Rien,
                            (uint8_t)avancer(r, obs(T0 + kEcartInitialMs + kRechercheMaxMs - 1)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Recherche, (uint8_t)r.etape);
}

void test_recherche_bloquee_abandonnee_sans_toucher_l_ecart() {
    Retour r = arme();
    r.ecart = 60000;
    avancer(r, obs(T0 + 60000));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::AbandonnerRecherche,
                            (uint8_t)avancer(r, obs(T0 + 60000 + kRechercheMaxMs)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Attente, (uint8_t)r.etape);
    TEST_ASSERT_EQUAL_UINT32(60000, r.ecart);
}

// --------------------------------------------------------------------------
// Tentative
// --------------------------------------------------------------------------

void test_connexion_reussie_ramene_en_station() {
    Retour r = arme();
    const uint32_t t = jusquaTentative(r);
    Observation o = obs(t + 3000);
    o.connecte = true;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::RevenirEnStation, (uint8_t)avancer(r, o));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Attente, (uint8_t)r.etape);
    TEST_ASSERT_EQUAL_UINT32(kEcartInitialMs, r.ecart);
}

void test_tentative_en_cours_attend() {
    Retour r = arme();
    const uint32_t t = jusquaTentative(r);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::Rien, (uint8_t)avancer(r, obs(t + kTentativeMaxMs - 1)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Tentative, (uint8_t)r.etape);
}

void test_tentative_trop_longue_abandonnee_et_ecart_double() {
    Retour r = arme();
    const uint32_t t = jusquaTentative(r);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::AbandonnerTentative, (uint8_t)avancer(r, obs(t + kTentativeMaxMs)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Attente, (uint8_t)r.etape);
    TEST_ASSERT_EQUAL_UINT32(2 * kEcartInitialMs, r.ecart);
}

void test_succes_l_emporte_sur_le_delai_depasse() {
    Retour r = arme();
    const uint32_t t = jusquaTentative(r);
    Observation o = obs(t + kTentativeMaxMs + 5000);
    o.connecte = true;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::RevenirEnStation, (uint8_t)avancer(r, o));
}

void test_ecart_plafonne_apres_refus_repetes() {
    Retour r = arme();
    const uint32_t attendus[] = {60000, 120000, 240000, kEcartMaxMs, kEcartMaxMs};
    for (uint32_t attendu : attendus) {
        const uint32_t t = jusquaTentative(r);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::AbandonnerTentative,
                                (uint8_t)avancer(r, obs(t + kTentativeMaxMs)));
        TEST_ASSERT_EQUAL_UINT32(attendu, r.ecart);
    }
}

void test_ecart_corrompu_ne_deborde_pas() {
    Retour r = arme();
    r.ecart = 0xFFFFFFF0u;
    r.etape = Etape::Tentative;
    r.debut = T0;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::AbandonnerTentative, (uint8_t)avancer(r, obs(T0 + kTentativeMaxMs)));
    TEST_ASSERT_EQUAL_UINT32(kEcartMaxMs, r.ecart);
}

// --------------------------------------------------------------------------
// Cas transverses
// --------------------------------------------------------------------------

void test_station_connectee_ramene_en_station_a_toute_etape() {
    const Etape etapes[] = {Etape::Attente, Etape::Recherche};
    for (Etape e : etapes) {
        Retour r = arme();
        r.etape = e;
        Observation o = obs(T0 + 1);
        o.connecte = true;
        TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::RevenirEnStation, (uint8_t)avancer(r, o));
    }
}

void test_etape_corrompue_repart_en_attente() {
    Retour r = arme();
    r.etape = static_cast<Etape>(7);
    r.ecart = 0;
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Ordre::Rien, (uint8_t)avancer(r, obs(T0 + 5)));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)Etape::Attente, (uint8_t)r.etape);
    TEST_ASSERT_EQUAL_UINT32(kEcartInitialMs, r.ecart);
}

// Panne du pont du 24/10 rejouee : le reseau revient 50 s apres l'entree en
// point d'acces. Le module doit etre de retour en station des la recherche
// suivante, soit 67 s apres l'entree, au lieu de jamais.
void test_scenario_retour_du_reseau() {
    Retour r = arme(T0);
    const uint32_t retourReseau = T0 + 50000;
    uint32_t enStation = 0;
    bool scanEnCours = false;
    uint32_t finScan = 0;

    for (uint32_t t = T0; t < T0 + 600000 && enStation == 0; t += 100) {
        Observation o = obs(t);
        if (scanEnCours && t >= finScan) {
            o.rechercheFinie = true;
            o.reseauVu       = t >= retourReseau;
        }
        o.connecte = r.etape == Etape::Tentative && t >= r.debut + 3000;

        switch (avancer(r, o)) {
        case Ordre::LancerRecherche:  scanEnCours = true; finScan = t + 2000; break;
        case Ordre::LancerTentative:  scanEnCours = false; break;
        case Ordre::RevenirEnStation: enStation = t; break;
        case Ordre::Rien:
            if (o.rechercheFinie) scanEnCours = false;
            break;
        default: break;
        }
    }

    // Recherches a T0+30 s (absent) puis T0+62 s (vu), connexion 3 s apres.
    TEST_ASSERT_EQUAL_UINT32(T0 + 30000 + 2000 + 30000 + 2000 + 3000, enStation);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_echu_a_la_borne_exacte);
    RUN_TEST(test_echu_a_travers_le_retour_a_zero_de_millis);

    RUN_TEST(test_armer_met_en_attente_a_l_ecart_minimal);
    RUN_TEST(test_attente_ne_fait_rien_avant_l_ecart);
    RUN_TEST(test_attente_lance_la_recherche_a_l_ecart);
    RUN_TEST(test_attente_a_travers_le_retour_a_zero_de_millis);

    RUN_TEST(test_reseau_vu_lance_la_tentative);
    RUN_TEST(test_reseau_absent_ramene_l_ecart_au_minimum);
    RUN_TEST(test_recherche_en_cours_attend);
    RUN_TEST(test_recherche_bloquee_abandonnee_sans_toucher_l_ecart);

    RUN_TEST(test_connexion_reussie_ramene_en_station);
    RUN_TEST(test_tentative_en_cours_attend);
    RUN_TEST(test_tentative_trop_longue_abandonnee_et_ecart_double);
    RUN_TEST(test_succes_l_emporte_sur_le_delai_depasse);
    RUN_TEST(test_ecart_plafonne_apres_refus_repetes);
    RUN_TEST(test_ecart_corrompu_ne_deborde_pas);

    RUN_TEST(test_station_connectee_ramene_en_station_a_toute_etape);
    RUN_TEST(test_etape_corrompue_repart_en_attente);
    RUN_TEST(test_scenario_retour_du_reseau);

    return UNITY_END();
}
