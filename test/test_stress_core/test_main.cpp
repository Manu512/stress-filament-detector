// Tests de la logique de mesure, joues sur PC (pio test -e native).
// Aucune dependance materielle : ni ESP32, ni capteur, ni reseau.

#include <unity.h>
#include "stress_core.h"

using namespace stress;

// Unity les reclame, y compris quand elles ne servent a rien : la logique
// testee est sans etat, chaque cas construit sa propre calibration.
void setUp(void) {}
void tearDown(void) {}

static Calibration cal() {
    Calibration c;
    c.neutral1     = 2000;
    c.neutral2     = 1993;
    c.span         = 400;
    c.neutral_zone = 16;
    c.hysteresis   = 8;
    return c;
}

// --------------------------------------------------------------------------
// computeDelta
// --------------------------------------------------------------------------

void test_delta_nul_au_repos() {
    const Calibration c = cal();
    TEST_ASSERT_EQUAL_INT32(0, computeDelta(c.neutral1, c.neutral2, c));
}

void test_delta_positif_en_compression() {
    const Calibration c = cal();
    // S1 monte, S2 descend : c'est la signature de la compression dans le
    // montage differentiel d'origine.
    TEST_ASSERT_GREATER_THAN_INT32(0, computeDelta(c.neutral1 + 50, c.neutral2 - 50, c));
}

void test_delta_negatif_en_tension() {
    const Calibration c = cal();
    TEST_ASSERT_LESS_THAN_INT32(0, computeDelta(c.neutral1 - 50, c.neutral2 + 50, c));
}

void test_delta_rejette_le_mode_commun() {
    const Calibration c = cal();
    const int32_t ref = computeDelta(c.neutral1 + 30, c.neutral2 - 30, c);
    // Les deux capteurs derivent ensemble de +120 (chaleur, tension d'alim) :
    // le differentiel doit etre strictement inchange. C'est tout l'interet du
    // montage a deux capteurs.
    const int32_t derive = computeDelta(c.neutral1 + 30 + 120, c.neutral2 - 30 + 120, c);
    TEST_ASSERT_EQUAL_INT32(ref, derive);
}

// --------------------------------------------------------------------------
// emaUpdate
// --------------------------------------------------------------------------

void test_ema_sans_filtrage() {
    TEST_ASSERT_EQUAL_INT32(500, emaUpdate(100, 500, 256));
}

void test_ema_fige_si_alpha_nul() {
    TEST_ASSERT_EQUAL_INT32(100, emaUpdate(100, 500, 0));
}

void test_ema_converge_sans_depasser() {
    int32_t v = 0;
    for (int i = 0; i < 200; ++i) v = emaUpdate(v, 1000, 32);
    // Converge vers la consigne sans jamais la depasser (filtre du 1er ordre).
    TEST_ASSERT_INT32_WITHIN(5, 1000, v);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(1000, v);
}

void test_ema_supporte_les_valeurs_negatives() {
    int32_t v = 0;
    for (int i = 0; i < 200; ++i) v = emaUpdate(v, -800, 32);
    TEST_ASSERT_INT32_WITHIN(5, -800, v);
}

// --------------------------------------------------------------------------
// deltaToDuty
// --------------------------------------------------------------------------

void test_duty_neutre_au_centre() {
    TEST_ASSERT_EQUAL_UINT16(kPwmNeutral, deltaToDuty(0, cal()));
}

void test_duty_sature_aux_deux_bouts() {
    const Calibration c = cal();
    TEST_ASSERT_EQUAL_UINT16(kPwmMax, deltaToDuty(c.span, c));
    TEST_ASSERT_EQUAL_UINT16(kPwmMin, deltaToDuty(-c.span, c));
}

void test_duty_sature_au_dela_de_span() {
    const Calibration c = cal();
    // Au-dela de la pleine echelle, on sature au lieu de reboucler : un
    // debordement ferait passer la compression pour de la tension.
    TEST_ASSERT_EQUAL_UINT16(kPwmMax, deltaToDuty(c.span * 10, c));
    TEST_ASSERT_EQUAL_UINT16(kPwmMin, deltaToDuty(-c.span * 10, c));
}

void test_duty_est_monotone() {
    const Calibration c = cal();
    uint16_t precedent = deltaToDuty(-c.span, c);
    for (int32_t d = -c.span + 1; d <= c.span; ++d) {
        const uint16_t v = deltaToDuty(d, c);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT16(precedent, v);
        precedent = v;
    }
}

// Ce que le PWM apporte par rapport au DAC, qui n'offrait que 96 pas : avec la
// pleine echelle relevee sur la machine (134), chaque unite de delta doit
// produire un rapport cyclique distinct. Sinon la resolution du capteur serait
// encore limitee par la sortie et non par la mesure.
void test_duty_distingue_chaque_unite_de_delta() {
    Calibration c = cal();
    c.span = 134;
    uint16_t precedent = deltaToDuty(-c.span, c);
    for (int32_t d = -c.span + 1; d <= c.span; ++d) {
        const uint16_t v = deltaToDuty(d, c);
        TEST_ASSERT_GREATER_THAN_UINT16(precedent, v);
        precedent = v;
    }
}

void test_duty_replie_sur_neutre_si_calibration_invalide() {
    Calibration c = cal();
    c.span = 0; // division par zero si on ne s'en protege pas
    TEST_ASSERT_FALSE(c.isValid());
    TEST_ASSERT_EQUAL_UINT16(kPwmNeutral, deltaToDuty(1234, c));
}

// Aucune entree, meme aberrante, ne doit faire sortir le rapport cyclique de la
// plage utile : a 0 % ou 100 % la broche ne commute plus, et la MMB ne
// distinguerait plus une butee d'une sortie figee.
void test_duty_ne_sort_jamais_de_la_plage() {
    const Calibration c = cal();
    const int32_t extremes[] = {0, 1, -1, c.span, -c.span, c.span * 100,
                                -c.span * 100, 2147483647, -2147483647};
    for (int32_t d : extremes) {
        TEST_ASSERT_GREATER_OR_EQUAL_UINT16(kPwmMin, deltaToDuty(d, c));
        TEST_ASSERT_LESS_OR_EQUAL_UINT16(kPwmMax, deltaToDuty(d, c));
    }
    Calibration invalide = cal();
    invalide.span = 0;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(kPwmMin, deltaToDuty(0, invalide));
}

// Le span maximal accepte entre dans un produit : il ne doit pas deborder, et
// les bornes doivent rester exactes meme a cette extremite.
void test_duty_sans_debordement_au_span_maximal() {
    Calibration c = cal();
    c.span = 4095;
    TEST_ASSERT_TRUE(c.isValid());
    TEST_ASSERT_EQUAL_UINT16(kPwmMax, deltaToDuty(2147483647, c));
    TEST_ASSERT_EQUAL_UINT16(kPwmMin, deltaToDuty(-2147483647, c));
}

// Fige les decisions sur la plage plutot que de les laisser a un futur
// ajustement distrait : jamais 0 % ni 100 %, neutre entre les deux, et une
// resolution que le peripherique sait reellement tenir a cette frequence.
void test_plage_pwm_coherente() {
    TEST_ASSERT_GREATER_THAN_UINT16(0, kPwmMin);          // la broche commute
    TEST_ASSERT_LESS_THAN_UINT16(kPwmFull, kPwmMax);      // idem en haut
    TEST_ASSERT_GREATER_THAN_UINT16(kPwmMin, kPwmMax);
    // Le neutre doit tomber entre les deux, sinon la moitie de la course
    // saturerait.
    TEST_ASSERT_GREATER_THAN_UINT16(kPwmMin, kPwmNeutral);
    TEST_ASSERT_LESS_THAN_UINT16(kPwmMax, kPwmNeutral);
    // Horloge LEDC a 80 MHz : le nombre de pas par periode borne la resolution.
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(80000000u / kPwmFreqHz, 1u << kPwmBits);
}

// Les deux extremites doivent tomber EXACTEMENT sur les bornes, quel que soit
// le span : c'est ce que l'ancienne forme signee ne garantissait pas lorsque la
// plage compte un nombre impair de pas, et 102..1945 en compte 1843.
void test_duty_atteint_exactement_les_bornes_quel_que_soit_le_span() {
    const int32_t spans[] = {1, 2, 3, 7, 16, 95, 134, 150, 1000, 4095};
    for (int32_t span : spans) {
        Calibration c = cal();
        c.span = span;
        TEST_ASSERT_TRUE(c.isValid());
        TEST_ASSERT_EQUAL_UINT16(kPwmMin, deltaToDuty(-span, c));
        TEST_ASSERT_EQUAL_UINT16(kPwmMax, deltaToDuty(span, c));
        TEST_ASSERT_EQUAL_UINT16(kPwmNeutral, deltaToDuty(0, c));
    }
}

void test_span_aberrant_refuse() {
    Calibration c = cal();
    c.span = 4096; // entre dans un produit : un span delirant deborderait
    TEST_ASSERT_FALSE(c.isValid());
    c.span = 4095;
    TEST_ASSERT_TRUE(c.isValid());
}

// --------------------------------------------------------------------------
// nextState et hysteresis
// --------------------------------------------------------------------------

void test_etat_neutre_dans_la_zone() {
    const Calibration c = cal();
    TEST_ASSERT_TRUE(nextState(State::Neutral, 0, c) == State::Neutral);
    TEST_ASSERT_TRUE(nextState(State::Neutral, c.neutral_zone, c) == State::Neutral);
}

void test_etat_bascule_au_dela_du_seuil() {
    const Calibration c = cal();
    TEST_ASSERT_TRUE(nextState(State::Neutral, c.neutral_zone + 1, c) == State::Compression);
    TEST_ASSERT_TRUE(nextState(State::Neutral, -c.neutral_zone - 1, c) == State::Tension);
}

void test_hysteresis_empeche_le_battement() {
    const Calibration c = cal();
    // Une fois en compression, une valeur entre le seuil de sortie et le seuil
    // d'entree ne doit PAS faire retomber en neutre : sans cela, un buffer qui
    // flotte juste au seuil ferait claquer la sortie en permanence.
    const int32_t entre_deux = c.neutral_zone - c.hysteresis + 1; // 9
    TEST_ASSERT_TRUE(nextState(State::Compression, entre_deux, c) == State::Compression);
    TEST_ASSERT_TRUE(nextState(State::Neutral, entre_deux, c) == State::Neutral);
}

void test_sortie_effective_sous_le_seuil_bas() {
    const Calibration c = cal();
    const int32_t sous_seuil = c.neutral_zone - c.hysteresis; // 8
    TEST_ASSERT_TRUE(nextState(State::Compression, sous_seuil, c) == State::Neutral);
    TEST_ASSERT_TRUE(nextState(State::Tension, -sous_seuil, c) == State::Neutral);
}

void test_pas_de_transition_directe_compression_tension() {
    const Calibration c = cal();
    // Un saut brutal doit passer par le neutre : la sortie ne doit jamais
    // inverser en un seul pas, Happy Hare verrait une inversion impossible.
    TEST_ASSERT_TRUE(nextState(State::Compression, -c.span, c) == State::Neutral);
    TEST_ASSERT_TRUE(nextState(State::Tension, c.span, c) == State::Neutral);
}

// --------------------------------------------------------------------------
// tensionLevelPermille
// --------------------------------------------------------------------------

void test_niveau_tension_symetrique() {
    const Calibration c = cal();
    TEST_ASSERT_EQUAL_INT32(0, tensionLevelPermille(0, c));
    TEST_ASSERT_EQUAL_INT32(1000, tensionLevelPermille(c.span, c));
    TEST_ASSERT_EQUAL_INT32(1000, tensionLevelPermille(-c.span, c));
    TEST_ASSERT_EQUAL_INT32(500, tensionLevelPermille(c.span / 2, c));
}

void test_niveau_tension_sature() {
    const Calibration c = cal();
    TEST_ASSERT_EQUAL_INT32(1000, tensionLevelPermille(c.span * 3, c));
}

// --------------------------------------------------------------------------
// Validation de la calibration
// --------------------------------------------------------------------------

void test_calibration_refuse_les_valeurs_incoherentes() {
    Calibration c = cal();
    TEST_ASSERT_TRUE(c.isValid());

    c = cal(); c.span = 0;              TEST_ASSERT_FALSE(c.isValid());
    c = cal(); c.span = -10;            TEST_ASSERT_FALSE(c.isValid());
    c = cal(); c.neutral_zone = -1;     TEST_ASSERT_FALSE(c.isValid());
    // hysteresis > neutral_zone rendrait le seuil de sortie negatif, donc on
    // ne quitterait jamais un etat une fois entre dedans.
    c = cal(); c.hysteresis = 20;       TEST_ASSERT_FALSE(c.isValid());
    c = cal(); c.neutral1 = 5000;       TEST_ASSERT_FALSE(c.isValid());
    c = cal(); c.neutral2 = -1;         TEST_ASSERT_FALSE(c.isValid());
}

// --------------------------------------------------------------------------

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_delta_nul_au_repos);
    RUN_TEST(test_delta_positif_en_compression);
    RUN_TEST(test_delta_negatif_en_tension);
    RUN_TEST(test_delta_rejette_le_mode_commun);

    RUN_TEST(test_ema_sans_filtrage);
    RUN_TEST(test_ema_fige_si_alpha_nul);
    RUN_TEST(test_ema_converge_sans_depasser);
    RUN_TEST(test_ema_supporte_les_valeurs_negatives);

    RUN_TEST(test_duty_neutre_au_centre);
    RUN_TEST(test_duty_sature_aux_deux_bouts);
    RUN_TEST(test_duty_sature_au_dela_de_span);
    RUN_TEST(test_duty_est_monotone);
    RUN_TEST(test_duty_distingue_chaque_unite_de_delta);
    RUN_TEST(test_duty_replie_sur_neutre_si_calibration_invalide);
    RUN_TEST(test_duty_ne_sort_jamais_de_la_plage);
    RUN_TEST(test_duty_sans_debordement_au_span_maximal);
    RUN_TEST(test_plage_pwm_coherente);
    RUN_TEST(test_duty_atteint_exactement_les_bornes_quel_que_soit_le_span);
    RUN_TEST(test_span_aberrant_refuse);

    RUN_TEST(test_etat_neutre_dans_la_zone);
    RUN_TEST(test_etat_bascule_au_dela_du_seuil);
    RUN_TEST(test_hysteresis_empeche_le_battement);
    RUN_TEST(test_sortie_effective_sous_le_seuil_bas);
    RUN_TEST(test_pas_de_transition_directe_compression_tension);

    RUN_TEST(test_niveau_tension_symetrique);
    RUN_TEST(test_niveau_tension_sature);

    RUN_TEST(test_calibration_refuse_les_valeurs_incoherentes);

    return UNITY_END();
}
