#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sim.h"

/* Cell state */
static int occupied[grid_size];
static unsigned heading[grid_size];

/* Patch state */
static float scent[grid_size];

static void
make_cell (int cell)
{
  occupied[cell] = 1;
  heading[cell] = fast_rand () % 8;
}

static void
cell_move (int cell, unsigned x, unsigned y)
{
  scent[cell] += 1.0;
  heading[cell] = 
    ((1.5 < scent[cell] ? follow_gradient_float (scent, heading[cell], x, y) 
                        : heading[cell])
     + (fast_rand () % 3) - 1) % 8;
  {
    unsigned neighbor = move2 (x, y, heading[cell]);
    if (occupied[neighbor])
      heading[cell] = fast_rand () % 8;
    else
      {
	if (0) scent[neighbor] = scent[cell]; /* interesting bug */
	occupied[neighbor] = occupied[cell];
	heading[neighbor]  = heading[cell];

	occupied[cell]  = 0;
      }
  }
}

static void
update_patch (int cell, unsigned x, unsigned y)
{
  scent[cell] *= 0.95;
  diffuse8_float (scent, x, y, 0.025);
}

static INLINE Uint8
color_scent (double scent)
{
  return scale_color (scent, 3.0);
    /*scale_color (scent == 0 ? 0 : log (scent) + 3, 
      5.0);*/
}

static INLINE Pixel
patch_color (int patch)
{
  return make_rgb (occupied[patch] ? 255 : 0,
		   color_scent (scent[patch]),
		   0);
}

static void
update_grid (void)
{
  int i;
  for (i = 0; i < grid_size; ++i)
    grid[i] = patch_color (i);
}

static void
tick (void)
{
  FOR_ALL_PATCHES (update_patch);
  FOR_ALL_TURTLES (occupied, 1, cell_move);
  update_grid ();
}

static void
genesis (int population)
{
  int i;
  memset (occupied, 0, sizeof occupied);
  memset (heading, 0, sizeof heading);
  memset (scent, 0, sizeof scent);
  for (i = 0; i < population; ++i)
    make_cell (pick_empty_patch (occupied, 0));
  update_grid ();
}

void
install_slime_words (ts_VM *vm)
{
  ts_install (vm, "slime-genesis", ts_run_void_1, (tsint) genesis);
  ts_install (vm, "slime-tick",    ts_run_void_0, (tsint) tick);
}
