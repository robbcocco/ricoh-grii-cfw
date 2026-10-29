#ifndef FILMS_H
#define FILMS_H

#include <stdint.h>

typedef struct {
    const char *id;             /* Unique identifier */
    const char *name;           /* ASCII display name */
    const char *target_slot;    /* Default assigned slot: posi, bleach, retro, cross */
    int16_t col0[3];            /* 3x3 color matrix col 0 (x128 scale) */
    int16_t col1[3];            /* 3x3 color matrix col 1 (x128 scale) */
    int16_t col2[3];            /* 3x3 color matrix col 2 (x128 scale) */
    const char *dcsve;          /* 5-digit parameter curve */
    const char *tint;           /* Color tint argument */
} FilmProfile;

/* Film Catalog Count */
#define FILM_CATALOG_SIZE 8

/* Master Film Simulation Catalog */
extern const FilmProfile g_film_catalog[FILM_CATALOG_SIZE];

/* Apply a film simulation profile to the active camera pipeline */
int film_apply(const FilmProfile *film, const char *slot_override);

#endif /* FILMS_H */
