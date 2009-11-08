#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tusdl.h"

enum { 
  WIDTH  = grid_width,
  HEIGHT = grid_height
};


static inline void
toggle (int x, int y)
{
  grid8[at (x, y)] ^= 1;
}

static inline Uint8
get_clipped (int x, int y)
{
  return get8 (x < 0 ? x + WIDTH : WIDTH <= x ? x - WIDTH : x,
	       y < 0 ? y + HEIGHT : HEIGHT <= y ? y - HEIGHT : y);
}

static inline void
put_clipped (int x, int y, Uint8 value)
{
  put8 (x < 0 ? x + WIDTH : WIDTH <= x ? x - WIDTH : x,
	y < 0 ? y + HEIGHT : HEIGHT <= y ? y - HEIGHT : y,
	value);
}

static void
sprinkle (void)
{
  int x, y;

  for (y = 0; y < HEIGHT; ++y)
    for (x = 0; x < WIDTH; ++x)
      if (rand() / (RAND_MAX+1.0) < 0.10)
	put8 (x, y, 1);
}

static void
munch_step (void)
{
  unsigned n = frame & 255;
  unsigned x;
  for (x = 0; x < WIDTH; ++x)
    put_clipped (x, x ^ n, n);
}


static void
sierp_step (int ox, int oy, int numer, int denom)
{
  double param = (double)numer / denom;
  int x, y;
  for (x = 0; x < WIDTH; ++x)
    for (y = 0; y < HEIGHT; ++y)
      put8 (x, y, 128 + ((x-ox)|(y-oy)) * param);
}


static inline void
copy_row (Uint8 *out, int row)
{
  memcpy (out, grid8 + row * WIDTH, WIDTH * sizeof out[0]);
}


static inline Uint8
life_update_cell (int a1, int a2, int a3, 
		  int b1, int b2, int b3, 
		  int c1, int c2, int c3)
{
  int sum = (a1&1) + (a2&1) + (a3&1) + (b1&1) + (b3&1) + (c1&1) + (c2&1) + (c3&1);
  return ((b2&1)<<1) | ((sum | (b2&1)) == 3);
  /*  return (b2&2) | ((b2&1)<<1) | ((sum | (b2&1)) == 3);*/
}

static inline void
life_update_row (Uint8 *out, 
		 const Uint8 *in1, const Uint8 *in2, const Uint8 *in3)
{
  int x;
  int i11, i10, i21, i20, i31, i30;

  i11 = in1[0], i10 = in1[1];
  i21 = in2[0], i20 = in2[1];
  i31 = in3[0], i30 = in3[1];
  out[0] = life_update_cell (in1[WIDTH-1], i11, i10,
			     in2[WIDTH-1], i21, i20,
			     in3[WIDTH-1], i31, i30);

  for (x = 1; x < WIDTH-1; ++x)
    {
      int j1 = in1[x+1];
      int j2 = in2[x+1];
      int j3 = in3[x+1];
      out[x] = life_update_cell (i11, i10, j1,
				 i21, i20, j2,
				 i31, i30, j3);
      i11 = i10, i10 = j1;
      i21 = i20, i20 = j2;
      i31 = i30, i30 = j3;
    }

  out[x] = life_update_cell (i11, i10, in1[0],
			     i21, i20, in2[0],
			     i31, i30, in3[0]);
}

static void
life_step (void)
{
  static Uint8 row[2][WIDTH];
  static Uint8 top_row[WIDTH];
  int y;

  copy_row (top_row, 0);	/* save for the end */

  copy_row (row[1], HEIGHT-1);	/* prime the pump */
  for (y = 0; y < HEIGHT; ++y)
    {
      copy_row (row[y & 1], y);
      life_update_row (grid8 + y * WIDTH,
		       row[(y-1) & 1], 
		       row[y & 1], 
		       y == HEIGHT-1 ? top_row : grid8 + (y+1) * WIDTH);
    }
}


static inline void
margolus_update_square (int parity, Uint8 *nw, Uint8 *ne, Uint8 *sw, Uint8 *se)
{
  Uint8 nwv = *nw, nev = *ne, swv = *sw, sev = *se;
  int sum = nwv + nev + swv + sev;
  if ((unsigned)(sum - 1) < 3)
    *nw = 1 - nwv, *ne = 1 - nev, *sw = 1 - swv, *se = 1 - sev;
}

static void
margolus_update_row (int parity, Uint8 *top, Uint8 *bot)
{
  int x;

  if (parity)
    margolus_update_square (parity, 
			    top + WIDTH-1, top,
			    bot + WIDTH-1, bot);

  for (x = parity; x < WIDTH-1; x += 2)
    margolus_update_square (parity, 
			    top + x, top + x + 1,
			    bot + x, bot + x + 1);
}

/* Pre: WIDTH and HEIGHT are multiples of 2 */
static void
margolus_step (void)
{
  int y, p = frame & 1;

  if (p)
    margolus_update_row (p, grid8 + (HEIGHT-1) * WIDTH, grid8);

  for (y = p; y < HEIGHT-1; y += 2)
    {
      int offset = y * WIDTH;
      margolus_update_row (p, grid8 + offset, grid8 + offset + WIDTH);
    }
}


static SDL_Color colors[256];

static void
wipe_colors (void)
{
  int i;
  for (i = 0; i <= 255; ++i)
    {
      colors[i].r = 0;
      colors[i].g = 0;
      colors[i].b = 0;
    }

  SDL_SetColors (screen, colors, 0, 256);	/* XXX */
}

static void
decay_colors (void)
{
  int i;

  for (i = 0; i < 256; ++i)
    {
      colors[i].r = colors[i].r * 31/32;
      colors[i].g = colors[i].g * 63/64;
      colors[i].b = colors[i].b * 15/16;
    }

  i = frame & 255;
  colors[i].r = 255;
  colors[i].g = 255;
  colors[i].b = 255;

  SDL_SetColors (screen, colors, 0, 256);	/* XXX */
}

static void
four_colors (void)
{
  colors[0].r = 0;
  colors[0].g = 0;
  colors[0].b = 0;

  colors[1].r = 0;
  colors[1].g = 0;
  colors[1].b = 255;

  colors[2].r = 0;
  colors[2].g = 255;
  colors[2].b = 0;

  colors[3].r = 255;
  colors[3].g = 0;
  colors[3].b = 0;

  SDL_SetColors (screen, colors, 0, 4);
}

void
install_casdl_words (ts_VM *vm)
{
  ts_install (vm, "4-colors",        ts_run_void_0, (int) four_colors);
  ts_install (vm, "wipe-colors",     ts_run_void_0, (int) wipe_colors);
  ts_install (vm, "decay-colors",    ts_run_void_0, (int) decay_colors);

  ts_install (vm, "sprinkle",        ts_run_void_0, (int) sprinkle);
  ts_install (vm, "grid8@",          ts_run_int_2,  (int) get_clipped);
  ts_install (vm, "grid8!",          ts_run_void_3, (int) put_clipped);

  ts_install (vm, "life-step",       ts_run_void_0, (int) life_step);
  ts_install (vm, "margolus-step",   ts_run_void_0, (int) margolus_step);
  ts_install (vm, "munch-step",      ts_run_void_0, (int) munch_step);
  ts_install (vm, "sierp-step",      ts_run_void_4, (int) sierp_step);

  /* gcc complains about this in cygwin, argh */
  /*  ts_install (vm, "sleep",           ts_run_int_1,  (int) sleep); */
}
