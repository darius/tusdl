#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "sim.h"


/* Random numbers */
/* We use this instead of the standard rand() because it's a 
   time bottleneck and, at least with glibc, this is faster. */

randctx ctx;

void
seed_rand (int seed)
{
  int i;
  ctx.randrsl[0] = (ub4) seed;
  for (i = 1; i < RANDSIZ; ++i) 
    ctx.randrsl[i] = (ub4) 0;
  randinit (&ctx, TRUE);
}


int
pick_empty_patch (int *array, int empty)
{
  for (;;)
    {
      int i = fast_rand () % grid_size;
      if (array[i] == empty)
	return i;
    }
}

int for_all_turtles_list[grid_size];

int
list_patches (int *array, int value)
{
  int i, n = 0;
  for (i = 0; i < grid_size; ++i)
    if (array[i] == value)
      for_all_turtles_list[n++] = i;
  return n;
}

static INLINE int *
check_neighbor (int *neighbors, 
		int *array, int x, int y, int dx, int dy, int color)
{
  int nx = move (x, dx, grid_width);
  int ny = move (y, dy, grid_height);
  if (get (nx, ny) == color)
    *neighbors++ = at (nx, ny);
  return neighbors;
}

int 
find_neighbors4 (int *neighbors, int *array, int x, int y, int color)
{
  int *neighbors_ptr = neighbors;
  neighbors_ptr = check_neighbor (neighbors_ptr, array, x, y, -1,  0, color);
  neighbors_ptr = check_neighbor (neighbors_ptr, array, x, y,  0, -1, color);
  neighbors_ptr = check_neighbor (neighbors_ptr, array, x, y,  1,  0, color);
  neighbors_ptr = check_neighbor (neighbors_ptr, array, x, y,  0,  1, color);
  return neighbors_ptr - neighbors;
}
