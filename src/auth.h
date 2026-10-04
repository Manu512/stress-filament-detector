// auth.h - Verrou des ecritures par defi-reponse.
//
// L'interface web est servie en HTTP : une cle envoyee telle quelle serait
// lisible par quiconque ecoute le reseau. Le module envoie donc un defi
// aleatoire, et le client prouve qu'il connait la cle en renvoyant
// HMAC-SHA256(cle, defi). La cle elle-meme ne circule jamais.
//
// Ce module ne connait ni le WebSocket ni le WiFi : il fabrique un defi et
// verifie une reponse, rien d'autre.

#ifndef AUTH_H
#define AUTH_H

#include <Arduino.h>

namespace auth {

// Longueurs en caracteres hexadecimaux, hors zero final.
constexpr size_t kNonceLen    = 32;
constexpr size_t kResponseLen = 64;

// Ecrit un defi aleatoire dans `out`.
void newNonce(char out[kNonceLen + 1]);

// Vrai si `response` vaut HMAC-SHA256(key, nonce), en hexadecimal minuscule.
// Une cle vide est toujours refusee. La comparaison est a temps constant.
bool verify(const String& key, const char* nonce, const char* response);

} // namespace auth

#endif // AUTH_H
