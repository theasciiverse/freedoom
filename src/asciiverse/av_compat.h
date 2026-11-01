/*
 * Asciiverse: what the files taken from Chocolate Doom 3.1.1 (opl*.c,
 * i_oplmusic.c, midifile.c, mus2mid.c) expect from its headers, mapped onto
 * doom-ascii's. Included before each of them (see the Makefile).
 *
 * This file is in the public domain.
 */
#ifndef AV_COMPAT_H
#define AV_COMPAT_H

#include <stdint.h>
#include <stdlib.h>
#include "doomtype.h"

/* Chocolate Doom 3 calls doomgeneric's bool "boolean" */
typedef bool boolean;

/* midifile.c reads big-endian chunk headers through SDL's swappers */
static inline uint16_t SDL_SwapBE16(uint16_t x)
{
    const uint8_t *b = (const uint8_t *) &x;
    return (uint16_t) ((b[0] << 8) | b[1]);
}

static inline uint32_t SDL_SwapBE32(uint32_t x)
{
    const uint8_t *b = (const uint8_t *) &x;
    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16)
         | ((uint32_t) b[2] << 8) | b[3];
}

/* packed structures, as Chocolate Doom 3 declares them */
#ifndef PACKEDPREFIX
#define PACKEDPREFIX
#endif
#define PACKED_STRUCT(...) PACKEDPREFIX struct __VA_ARGS__ PACKEDATTR

/* the DMX driver version the OPL player imitates; Chocolate Doom sets an
   older one only when emulating an older executable, which doom-ascii
   does not, so it stays at Doom 1.9's */
typedef enum
{
    opl_doom1_1_666,    // Doom 1 v1.666
    opl_doom2_1_666,    // Doom 2 v1.666, Hexen, Heretic
    opl_doom_1_9        // Doom v1.9, Strife
} opl_driver_ver_t;

/* Chocolate Doom's file wrappers (UTF-8 names on Windows) */
#define M_fopen fopen
#define M_remove remove

/* realloc that ends the game when memory runs out */
void I_Error(char *error, ...);
static inline void *I_Realloc(void *ptr, size_t size)
{
    void *new_ptr = realloc(ptr, size);

    if (size != 0 && new_ptr == NULL)
        I_Error("I_Realloc: failed on reallocation of %zu bytes", size);
    return new_ptr;
}

#endif
