#include <stdio.h>
#include "sim.h"

enum {
  empty         = black,
  emptyhanded   = green*3/4,
  carrying      = yellow,
  sand          = MAKE_RGB (192, 192, 0)
};

static unsigned heading[grid_size];

static void
make_termite (int i)
{
  grid[i] = emptyhanded;
  heading[i] = fast_rand () % 8;  
}

static void
genesis (int termites, int sands)
{
  int i;
  for (i = 0; i < sands; ++i)
    grid[pick_empty_patch (grid, empty)] = sand;
  for (i = 0; i < termites; ++i)
    make_termite (pick_empty_patch (grid, empty));
}

static void
emptyhanded_move (int termite, unsigned x, unsigned y)
{
  heading[termite] = (heading[termite] + fast_rand () % 3 - 1) % 8;
  {
    unsigned neighbor = move2 (x, y, heading[termite]);
    if (grid[neighbor] == sand)
      grid[termite] = carrying;
    else if (grid[neighbor] != empty)
      {
	heading[termite] = fast_rand () % 8;
	return;
      }

    grid[neighbor]    = grid[termite];
    heading[neighbor] = heading[termite];

    grid[termite]     = empty;
  }  
}

static void
carrying_move (int termite, unsigned x, unsigned y)
{
  heading[termite] = (heading[termite] + fast_rand () % 3 - 1) % 8;
  {
    unsigned neighbor = move2 (x, y, heading[termite]);
    if (grid[neighbor] != empty)
      heading[termite] = fast_rand () % 8;
    else
      {
	int neigh[4];
	Pixel me = carrying, behind = empty;
	if (0 < find_neighbors4 (neigh, grid, x, y, sand))
	  {
	    me = emptyhanded;
	    behind = sand;
	  }

	grid[neighbor]    = me;
	heading[neighbor] = heading[termite];

	/* FIXME: this isn't necessarily adjacent to the sand */
	grid[termite]     = behind;
      }
  }  
}

static void
tick (void)
{
  FOR_ALL_TURTLES (grid, emptyhanded, emptyhanded_move);
  FOR_ALL_TURTLES (grid, carrying, carrying_move);
}

void
install_termite_words (ts_VM *vm)
{
  ts_install (vm, "termite-genesis", ts_run_void_2, (int) genesis);
  ts_install (vm, "termite-tick",    ts_run_void_0, (int) tick);
}
