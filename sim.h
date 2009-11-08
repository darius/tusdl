#ifndef SIM_H
#define SIM_H

#include "tusdl.h"
#include "rand.h"

extern randctx ctx;

extern void seed_rand (int seed);

static INLINE unsigned
fast_rand (void)
{
  return RAND (&ctx);
}


static INLINE int
move (int z, int dz, int limit)
{
  if (dz == 0)
    return z;
  z += dz;
  if (dz < 0)
    { if (z < 0) z += limit; }
  else
    { if (limit <= z) z -= limit; }
  return z;
}

static const int dx[] = { 1, 1, 0, -1, -1, -1, 0, 1 };
static const int dy[] = { 0, 1, 1, 1, 0, -1, -1, -1 };
enum { east = 0, north = 2, west = 4, south = 6 };

static INLINE unsigned
move_x (unsigned x, int direction)
{
  return (x + dx[direction]) % grid_width;
}

static INLINE unsigned
move_y (unsigned y, int direction)
{
  return (y + dy[direction]) % grid_height;
}

static INLINE unsigned
move2 (unsigned x, unsigned y, int direction)
{
  return at (move_x (x, direction), 
	     move_y (y, direction));
}

static INLINE Uint8
scale_color (double value, double clamp)
{
  if (value < 0)
    value = 0;
  if (clamp < value)
    value = clamp;
  return (Uint8)(value * (255/clamp));
}


static INLINE void
diffuse8_float (float *array, unsigned x, unsigned y, double fraction)
{
  double droplet = array[at (x, y)] * fraction;
  int i;
  for (i = 0; i < 8; ++i)
    array[move2 (x, y, i)] += droplet;
  array[at (x, y)] -= 8 * droplet;
}

static INLINE unsigned
pick_greater_double (double v0, unsigned u0, double v1, unsigned u1)
{
  if (v0 < v1 || (v0 == v1 && fast_rand () & 1))
    return u1;
  else 
    return u0;
}

static INLINE unsigned
follow_gradient_float (float *array, unsigned heading, unsigned x, unsigned y)
{
  unsigned dir1 = heading;
  unsigned dir0 = (dir1 - 1) % 8;
  unsigned dir2 = (dir1 + 1) % 8;
  double array0 = array[move2 (x, y, dir0)];
  double array1 = array[move2 (x, y, dir1)];
  double array2 = array[move2 (x, y, dir2)];

  return
    pick_greater_double (array0, 
			 pick_greater_double (array0, dir0, array2, dir2),
			 array1, 
			 pick_greater_double (array1, dir1, array2, dir2));
}

static INLINE unsigned
pick_greater_unsigned (unsigned v0, unsigned u0, unsigned v1, unsigned u1)
{
  if (v0 < v1 || (v0 == v1 && fast_rand () & 1))
    return u1;
  else 
    return u0;
}

static INLINE unsigned
follow_gradient_unsigned (unsigned *array, 
			  unsigned heading, unsigned x, unsigned y)
{
  unsigned dir1 = heading;
  unsigned dir0 = (dir1 - 1) % 8;
  unsigned dir2 = (dir1 + 1) % 8;
  unsigned array0 = array[move2 (x, y, dir0)];
  unsigned array1 = array[move2 (x, y, dir1)];
  unsigned array2 = array[move2 (x, y, dir2)];

  return
    pick_greater_unsigned (array0, 
			   pick_greater_unsigned (array0, dir0, array2, dir2),
			   array1, 
			   pick_greater_unsigned (array1, dir1, array2, dir2));
}


int pick_empty_patch (int *array, int empty);

int list_patches (int *array, int value);

int find_neighbors4 (int *neighbors, int *array, int x, int y, int color);

static INLINE int 
pick_neighbor4 (int *array, int x, int y, int color)
{
  int neighbors[4];
  int n_neighbors = find_neighbors4 (neighbors, array, x, y, color);
  if (0 < n_neighbors)
    return neighbors[fast_rand () % n_neighbors];
  else
    return -1;
}


#define FOR_ALL_PATCHES(proc)          \
  do {                                 \
    int x, y;                          \
    for (y = 0; y < grid_height; ++y)  \
      for (x = 0; x < grid_width; ++x) \
	proc (at (x, y), x, y);        \
  } while (0)

/* Goddamn Windows seems to blow up this program whenever it has a
   stack-allocated array that's too large.  So let's make it an extern
   array instead: */
extern int for_all_turtles_list[grid_size];

#define FOR_ALL_TURTLES(array, value, proc)       \
  {                                               \
    int i;                                        \
    int n = list_patches (array, value);          \
    for (i = 0; i < n; i += 2)                    \
      {                                           \
	int j = for_all_turtles_list[i];          \
	proc (j, j % grid_width, j / grid_width); \
      }                                           \
    for (i = 1; i < n; i += 2)                    \
      {                                           \
	int j = for_all_turtles_list[i];          \
	proc (j, j % grid_width, j / grid_width); \
      }                                           \
  }


#endif
