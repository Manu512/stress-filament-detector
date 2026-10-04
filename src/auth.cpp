#include "auth.h"

#include <esp_system.h>
#include <mbedtls/md.h>

namespace auth {

namespace {

void enHexa(const uint8_t* octets, size_t n, char* out) {
    static const char kChiffres[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = kChiffres[octets[i] >> 4];
        out[2 * i + 1] = kChiffres[octets[i] & 0x0F];
    }
    out[2 * n] = '\0';
}

} // namespace

void newNonce(char out[kNonceLen + 1]) {
    uint8_t brut[kNonceLen / 2];
    // Alea materiel veritable tant que la radio est active, ce qui est toujours
    // le cas quand un client WebSocket est connecte.
    esp_fill_random(brut, sizeof(brut));
    enHexa(brut, sizeof(brut), out);
}

bool verify(const String& key, const char* nonce, const char* response) {
    if (key.isEmpty() || !nonce || !response) return false;
    if (strlen(nonce) != kNonceLen || strlen(response) != kResponseLen) return false;

    uint8_t brut[kResponseLen / 2];
    const mbedtls_md_info_t* algo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!algo || mbedtls_md_hmac(algo,
                                 reinterpret_cast<const uint8_t*>(key.c_str()), key.length(),
                                 reinterpret_cast<const uint8_t*>(nonce), kNonceLen,
                                 brut) != 0) {
        return false;
    }
    char attendu[kResponseLen + 1];
    enHexa(brut, sizeof(brut), attendu);

    // Temps constant : toute la chaine est parcourue, ou que soit le premier
    // ecart, pour ne rien reveler par la duree de la comparaison.
    uint8_t ecart = 0;
    for (size_t i = 0; i < kResponseLen; i++) {
        ecart |= static_cast<uint8_t>(attendu[i] ^ response[i]);
    }
    return ecart == 0;
}

} // namespace auth
