#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sim.h"

/* These ants suck.  They're not much smarter about finding food than
   just wandering randomly. */

enum {
  empty         = black,
  emptyhanded   = green*3/4,
  carrying      = yellow,
  food          = MAKE_RGB (192, 192, 0),

  food_center_x = 192,
  food_center_y = 192,
  food_radius   = 15,

  nest_x        = grid_width / 2,
  nest_y        = grid_height / 2,
  nest_radius   = 15,
};

static unsigned heading[grid_size];
static int      gland[grid_size];
static unsigned scent[grid_size];

static INLINE int
in_nest (unsigned x, unsigned y)
{
  int dx = x - nest_x;
  int dy = y - nest_y;
  return dx*dx + dy*dy < nest_radius * nest_radius;
}

static INLINE double
nest_distance_squared (unsigned patch)
{
  int dx = patch % grid_width - nest_x;
  int dy = patch / grid_width - nest_y;
  return dx*dx + dy*dy;
}

static void
make_ant (int i)
{
  grid[i] = emptyhanded;
  heading[i] = fast_rand () % 8;  
}

static void
genesis (int ants, int foods)
{
  int i;
  memset (scent, 0, sizeof scent);
  for (i = 0; i < foods; ++i)
    {
      int p = pick_empty_patch (grid, empty);
      int x = p % grid_width;
      int y = p / grid_width;
      if (hypot (x - food_center_x, y - food_center_y) < food_radius)
	grid[p] = food;
      if (hypot (x - nest_x, y - (nest_y - 40)) < food_radius)
	grid[p] = food;
    }
  for (i = 0; i < ants; ++i)
    make_ant (pick_empty_patch (grid, empty));
}

static void
emptyhanded_move (int ant, unsigned x, unsigned y)
{
  heading[ant] =
    ((15 < scent[ant] ? follow_gradient_unsigned (scent, heading[ant], x, y) 
                      : heading[ant])
     + (fast_rand () % 3) - 1) % 8;
  {
    unsigned neighbor = move2 (x, y, heading[ant]);
    if (grid[neighbor] == food)
      {
	grid[ant] = carrying;
	gland[ant] = 16000;
      }
    else if (grid[neighbor] != empty)
      {
	heading[ant] = fast_rand () % 8;
	return;
      }

    grid[neighbor]    = grid[ant];
    heading[neighbor] = heading[ant];

    grid[ant]         = empty;
  }  
}

static INLINE unsigned
pick_lesser (unsigned v0, unsigned u0, unsigned v1, unsigned u1)
{
  if (v0 < v1 || (v0 == v1 && fast_rand () & 1))
    return u0;
  else 
    return u1;
}

static INLINE unsigned
uphill (int x, int y, int dir)
{
  unsigned dir1 = dir;
  unsigned dir0 = (dir1 - 1) % 8;
  unsigned dir2 = (dir1 + 1) % 8;
  unsigned d0 = nest_distance_squared (move2 (x, y, dir0));
  unsigned d1 = nest_distance_squared (move2 (x, y, dir1));
  unsigned d2 = nest_distance_squared (move2 (x, y, dir2));

  return pick_lesser (d0, pick_lesser (d0, dir0, d2, dir2),
		      d1, pick_lesser (d1, dir1, d2, dir2));
}

static void
carrying_move (int ant, unsigned x, unsigned y)
{
  unsigned dir = uphill (x, y, heading[ant]);
  dir = (dir + fast_rand () % 3 - 1) % 8;

  heading[ant] = dir;
  {
    unsigned neighbor = move2 (x, y, dir);
    if (grid[neighbor] != empty)
      heading[ant] = fast_rand () % 8;
    else if (in_nest (x, y) && fast_rand () % 4 == 0)
      {
	grid[neighbor] = food;
	grid[ant]      = emptyhanded;
	heading[ant]   = (dir + 4) % 8; /* turn around */
      }
    else
      {
	if (!in_nest (x, y))
	  {
	    int gl = gland[ant];
	    if (0 < gl)
	      {
		scent[ant] += gl;
		gland[ant] -= 128;
	      }
	  }

	grid[neighbor]    = carrying;
	heading[neighbor] = dir;
	
	grid[ant]         = empty;
      }
  }  
}

static INLINE void
diffuse8_unsigned (unsigned *array, unsigned x, unsigned y, unsigned fraction)
{
  double droplet = array[at (x, y)] / fraction;
  int i;
  for (i = 0; i < 8; ++i)
    array[move2 (x, y, i)] += droplet;
  array[at (x, y)] -= 8 * droplet;
}

static INLINE void
update_patch (int cell, unsigned x, unsigned y)
{
  if (0 < scent[cell])
    {
      scent[cell] = (scent[cell] * 511) / 512;
      diffuse8_unsigned (scent, x, y, 64);
    }
}

static void
tick (void)
{
  FOR_ALL_PATCHES (update_patch);
  FOR_ALL_TURTLES (grid, emptyhanded, emptyhanded_move);
  FOR_ALL_TURTLES (grid, carrying, carrying_move);
}

void
install_ants_words (ts_VM *vm)
{
  ts_install (vm, "ants-genesis", ts_run_void_2, (int) genesis);
  ts_install (vm, "ants-tick",    ts_run_void_0, (int) tick);
}
