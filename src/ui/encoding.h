#ifndef UI_ENCODING_H
#define UI_ENCODING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Convertit une chaîne UTF-8 vers CP437 affichable
// - utf8 : chaîne source UTF-8
// - cp437 : buffer de sortie
// - cp437_size : taille du buffer (incluant '\0')
// Retourne le nombre de caractères écrits (hors '\0')
size_t utf8_to_cp437(const char *utf8, char *cp437, size_t cp437_size);

#ifdef __cplusplus
}
#endif

#endif /* UI_ENCODING_H */
