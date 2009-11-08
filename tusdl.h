#ifndef TUSDL_H
#define TUSDL_H

#include "SDL.h"
#include "tusl.h"

enum { 
#if 0
  grid_width  = 2048,
  grid_height = 2048,
#elif 0
  grid_width  = 2400,
  grid_height = 1600,
#elif 0
  grid_width  = 4320,
  grid_height = 2880,
#elif 0
  grid_width  = 1200,
  grid_height =  800,
#else
  grid_width  = 1024,
  grid_height =  768,
#endif
  grid_size   = grid_width * grid_height
};

typedef Uint32 Pixel;


void die (const char *message, ...);


extern int frame;

extern SDL_Surface *screen;
extern Pixel *grid;
extern Uint8 *grid8;

static INLINE int
at (int x, int y)
{
  return y * grid_width + x;
}

/* Set the grid location (x,y) to `color'. */
static INLINE void
put (int x, int y, Pixel color)
{
  grid[at (x, y)] = color;
}

static INLINE Pixel
get (int x, int y)
{
  return grid[at (x, y)];
}

/* Set the 8-bpp grid location (x,y) to `color'. */
static INLINE void
put8 (int x, int y, Uint8 color)
{
  grid8[at (x, y)] = color;
}

static INLINE Pixel
get8 (int x, int y)
{
  return grid8[at (x, y)];
}


/* We need the macro so we can use it in constant expressions. */
#define MAKE_RGB(r, g, b) (((r) << 16) + ((g) << 8) + (b))

static INLINE Pixel 
make_rgb (Uint8 r, Uint8 g, Uint8 b)
{
  return MAKE_RGB (r, g, b);
}

enum {
  red    = MAKE_RGB (255, 0, 0),
  green  = MAKE_RGB (0, 255, 0),
  blue   = MAKE_RGB (0, 0, 255),
  yellow = red|green,
  black  = 0,
  white  = red|green|blue
};


ts_VM *make_sdl_vm (void);

void start_sdl (int bits_per_pixel);

void install_ants_words (ts_VM *vm);
void install_casdl_words (ts_VM *vm);
void install_evo_words (ts_VM *vm);
void install_orbit_words (ts_VM *vm);
void install_slime_words (ts_VM *vm);
void install_termite_words (ts_VM *vm);
void install_turtle_words (ts_VM *vm);
void install_wator_words (ts_VM *vm);


#endif
