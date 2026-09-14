/* stubs for image formats LUC does not ship (AVIF/JPEG-XL/TIFF/WebP) */
#include "SDL.h"
#include "SDL_image.h"

int IMG_InitAVIF(void) { return -1; }
void IMG_QuitAVIF(void) {}
SDL_Surface *IMG_LoadAVIF_RW(SDL_RWops *src) { (void)src; return NULL; }
int IMG_isAVIF(SDL_RWops *src) { (void)src; return 0; }

int IMG_InitJXL(void) { return -1; }
void IMG_QuitJXL(void) {}
SDL_Surface *IMG_LoadJXL_RW(SDL_RWops *src) { (void)src; return NULL; }
int IMG_isJXL(SDL_RWops *src) { (void)src; return 0; }

int IMG_InitTIF(void) { return -1; }
void IMG_QuitTIF(void) {}
SDL_Surface *IMG_LoadTIF_RW(SDL_RWops *src) { (void)src; return NULL; }
int IMG_isTIF(SDL_RWops *src) { (void)src; return 0; }

int IMG_InitWEBP(void) { return -1; }
void IMG_QuitWEBP(void) {}
SDL_Surface *IMG_LoadWEBP_RW(SDL_RWops *src) { (void)src; return NULL; }
IMG_Animation *IMG_LoadWEBPAnimation_RW(SDL_RWops *src) { (void)src; return NULL; }
int IMG_isWEBP(SDL_RWops *src) { (void)src; return 0; }
