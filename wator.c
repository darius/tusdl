#include <stdio.h>
#include <string.h>

#include "sim.h"

enum {
  max_critters = grid_size,
  empty        = black,
  fish_color   = green,
  shark_color  = red
};

static int
  fish_breeding_age,
  shark_breeding_age,
  shark_starve_time;

static short health[max_critters];
static short breeding_countdown[max_critters];

static void
make_fish (int i)
{
  breeding_countdown[i] = fast_rand () % fish_breeding_age;

  grid[i] = fish_color;
}

static void
make_shark (int i)
{
  health[i] = fast_rand () % shark_starve_time;
  breeding_countdown[i] = fast_rand () % shark_breeding_age;

  grid[i] = shark_color;
}

static void
genesis (int initial_fish_population, int initial_shark_population)
{
  int i;
  memset (health, 0, sizeof health);
  memset (breeding_countdown, 0, sizeof breeding_countdown);
  for (i = 0; i < initial_fish_population; ++i)
    make_fish (pick_empty_patch (grid, empty));
  for (i = 0; i < initial_shark_population; ++i)
    make_shark (pick_empty_patch (grid, empty));
}

static INLINE void
bear_fish (int baby)
{
  breeding_countdown[baby] = fish_breeding_age - fast_rand () % 5;
}

static INLINE void
bear_shark (int baby)
{
  health[baby] = shark_starve_time;
  breeding_countdown[baby] = shark_breeding_age - fast_rand () % 5;
}

static void
move_fish (int fish, int x, int y)
{
  int countdown = --breeding_countdown[fish];
  int neighbor = pick_neighbor4 (grid, x, y, empty);
  if (-1 != neighbor)
    {
      grid[neighbor] = fish_color;
      breeding_countdown[neighbor] = countdown;
      
      if (0 < countdown)
	grid[fish] = empty;
      else 
	{
	  bear_fish (fish);
	  breeding_countdown[neighbor] = fish_breeding_age;
	}
    }
}

static void
move_shark (int shark, int x, int y)
{
  if (--health[shark] < 0)
    grid[shark] = empty;
  else
    {
      int countdown = --breeding_countdown[shark];
      int neighbor = pick_neighbor4 (grid, x, y, fish_color);
      if (-1 != neighbor)
	health[shark] = shark_starve_time; /* neighbor is a fish -- eat it */
      else
	neighbor = pick_neighbor4 (grid, x, y, empty);

      if (-1 != neighbor)
	{      
	  grid[neighbor] = shark_color;
	  health[neighbor] = health[shark];
	  if (0 < countdown)
	    {
	      grid[shark] = empty;
	      breeding_countdown[neighbor] = breeding_countdown[shark];
	    }
	  else
	    {
	      bear_shark (shark);
	      breeding_countdown[neighbor] = shark_breeding_age;
	    }
	}
    }
}

static void
tick (void)
{
  FOR_ALL_TURTLES (grid, fish_color, move_fish);
  FOR_ALL_TURTLES (grid, shark_color, move_shark);
}

void
install_wator_words (ts_VM *vm)
{
  ts_install (vm, "wator-genesis",      ts_run_void_2, (tsint) genesis);
  ts_install (vm, "wator-tick",         ts_run_void_0, (tsint) tick);
  ts_install (vm, "fish-breeding-age",  ts_do_push, (tsint) &fish_breeding_age);
  ts_install (vm, "shark-breeding-age", ts_do_push, (tsint) &shark_breeding_age);
  ts_install (vm, "shark-starve-time",  ts_do_push, (tsint) &shark_starve_time);
}
