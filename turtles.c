#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

enum {
  genome_length   = 100,	/* genes */
  mutation_rate   = 3,		/* percent */

  tile_width      = 256,	/* in pixels */
  tile_height     = 256,
  cols            = grid_width / tile_width,
  rows            = grid_height / tile_height,

  max_turtles = 131072,
  max_nesting = 20
};

typedef struct Turtle Turtle;
struct Turtle {
  float x, y;			/* Offset from the playfield's center. */
  float heading;		/* Direction in radians from the x-axis. */
  float r, g, b;		/* Color components; the values may stray
				   outside 0..1, but they're clipped to
				   that range when applied. */
};

/* One patch for each pixel on the playfield, indexed by the same
   (x,y) coordinates we plot on a screen tile.  Each patch holds RGB
   color values. */
static float patches[tile_width][tile_height][3];

/* turtles[0..num_turtles-1] is the set of all turtles now alive. */
static Turtle turtles[max_turtles];
static int num_turtles = 1;

/* turtles[first_active_turtle..num_turtles-1] are the turtles now
   active; that is, the ones that commands are currently directed to. */
static int first_active_turtle = 0;

/* Stack of saved values of first_active_turtle.  Grows upwards. */
static int stack[max_nesting];
static int sp = -1;

/* Reset the turtle state to one active turtle at the origin. */
static void
reset (void)
{
  first_active_turtle = 0;
  num_turtles = 1;
  turtles[0].x = 0;
  turtles[0].y = 0;
  turtles[0].heading = 0;
  turtles[0].r = 1;
  turtles[0].g = 1;
  turtles[0].b = 1;
  sp = -1;
}

/* Clear the screen tile and its patches, and draw grid lines on the border. */
static void
clear_tile (int g)
{			
  int x, y;
  int corner_x = (g % cols) * tile_width;
  int corner_y = (g / cols) * tile_height;
  for (y = 0; y != tile_height; ++y)
    for (x = 0; x != tile_width; ++x)
      {
	patches[x][y][0] = 0;
	patches[x][y][1] = 0;
	patches[x][y][2] = 0;
	put (corner_x + x, corner_y + y, x == 0 || y == 0 ? blue : black);
      }
}

/* Convert a 0..1 intensity value into an 8-bit color component. */
static INLINE Uint8
color_value (float intensity)
{
  int i = (int) floor (256 * intensity);
  return i < 0 ? 0 : 255 < i ? 255 : i;
}

/* Make a tile of the screen buffer display the state of the patches. */
static void
display (int g)
{
  int x, y;
  int corner_x = (g % cols) * tile_width;
  int corner_y = (g / cols) * tile_height;
  for (y = 0; y != tile_height; ++y)
    for (x = 0; x != tile_width; ++x)
      {
	Uint32 c = make_rgb (color_value (patches[x][y][0]),
			     color_value (patches[x][y][1]),
			     color_value (patches[x][y][2]));
	put (corner_x + x, corner_y + y, x == 0 || y == 0 ? blue : c);
      }
}

/* Ask each active turtle to plot a point at its current position. */
static void
plot (void)
{
  int i;
  for (i = first_active_turtle; i != num_turtles; ++i)
    {
      unsigned ix = ((unsigned) (tile_width/2 + turtles[i].x)) % tile_width;
      unsigned iy = ((unsigned) (tile_height/2 - turtles[i].y)) % tile_height;
      patches[ix][iy][0] = turtles[i].r;
      patches[ix][iy][1] = turtles[i].g;
      patches[ix][iy][2] = turtles[i].b;
    }
}

/* Ask each active turtle to move distance d along its heading. */
static void
forward (double d)
{
  int i;
  for (i = first_active_turtle; i != num_turtles; ++i)
    {
      turtles[i].x += d * cos (turtles[i].heading);
      turtles[i].y += d * sin (turtles[i].heading);
    }
}

/* Ask each active turtle to turn its heading by 'angle' radians. */
static void
left (double angle)
{
  int i;
  for (i = first_active_turtle; i != num_turtles; ++i)
    turtles[i].heading += angle;
}

static void
fd (int d)
{
  forward ((double) d);
}

static void
lt (int degrees)
{
  left ((3.14159265358979323846/180.0) * degrees);
}

/* Ask each active turtle to duplicate itself; then push the current
   active-turtle set and replace it with the newly hatched turtles.
   (If the number of turtles would exceed the limit, not all of them
   will hatch.) */
static void
hatch_start (void)
{
  int d = num_turtles - first_active_turtle;
  if (max_turtles < num_turtles + d)
    d = max_turtles - num_turtles;

  if (sp < max_nesting)
    stack[++sp] = first_active_turtle;

  memcpy (&turtles[num_turtles], &turtles[first_active_turtle], 
	  d * sizeof turtles[0]);
  first_active_turtle = num_turtles;
  num_turtles += d;
}

/* Pop the active-turtle set.  This is like the close-bracket to
   hatch_start. */
static void
end (void)
{
  if (0 <= sp)
    first_active_turtle = stack[sp--];
}

/* Make colors diffuse out between adjacent patches. */
static void
diffuse (void)
{
  unsigned x, y;
  for (y = 0; y != tile_height; ++y)
    for (x = 0; x != tile_width; ++x)
      {
	/* FIXME: this isn't a proper blur convolution, because it
	 feeds newly blurred pixels back into the input for subsequent
	 pixels.  Need to buffer them more carefully.  Doesn't seem to
	 make a visible difference so far, though, so I'm doing it the
	 sleazy way for now. */
	unsigned x_1 = (x-1) % tile_width;
	unsigned x1  = (x+1) % tile_width;
	unsigned y_1 = (y-1) % tile_height;
	unsigned y1  = (y+1) % tile_height;
	patches[x][y][0] = (patches[x_1][y][0] +
			      patches[x1][y][0] + 
			      patches[x][y][0] + 
			      patches[x][y_1][0] + 
			      patches[x][y1][0]) / 5.0;
	patches[x][y][1] = (patches[x_1][y][1] +
			      patches[x1][y][1] + 
			      patches[x][y][1] + 
			      patches[x][y_1][1] + 
			      patches[x][y1][1]) / 5.0;
	patches[x][y][2] = (patches[x_1][y][2] +
			      patches[x1][y][2] + 
			      patches[x][y][2] + 
			      patches[x][y_1][2] + 
			      patches[x][y1][2]) / 5.0;
      }
}

/* Ask each active turtle to become redder by r (less red if r<0). */
static void 
add_r (int r)
{
  int i;
  for (i = first_active_turtle; i != num_turtles; ++i)
    turtles[i].r += r/100.0;
}

/* Ask each active turtle to become greener by g. */
static void 
add_g (int g)
{
  int i;
  for (i = first_active_turtle; i != num_turtles; ++i)
    turtles[i].g += g/100.0;
}

/* Ask each active turtle to become bluer by b. */
static void 
add_b (int b)
{
  int i;
  for (i = first_active_turtle; i != num_turtles; ++i)
    turtles[i].b += b/100.0;
}


/* Genotypes */

/* Handlers for ops that ignore their argument: */
static void plot_op (int a)    { plot (); }
static void hatch_op (int a)   { hatch_start (); }
static void end_op (int a)     { end (); }
static void diffuse_op (int a) { diffuse (); }

typedef struct Instruc_type Instruc_type;
struct Instruc_type {
  int frequency;		/* Unused for now */
  int num_arguments;
  const char *name;
  void (*handler)(int);
};

static Instruc_type op_types[] = {
  { 1, 0, "plot",    plot_op },
  { 1, 1, "fd",      fd },
  { 1, 1, "lt",      lt },
  { 1, 0, "hatch[",  hatch_op },
  { 1, 0, "]",       end_op },
  { 1, 0, "diffuse", diffuse_op },
  { 1, 1, "+r",      add_r },
  { 1, 1, "+g",      add_g },
  { 1, 1, "+b",      add_b },
};

typedef struct Instruc Instruc;
struct Instruc {
  int type;			/* Index into op_types[] */
  int argument;
};

/* One genome for each tile on the screen: */
static Instruc genome[rows*cols][genome_length];

static void
check_coord (int g)
{
  if (g < 0 || rows*cols <= g)
    die ("Bad coord: %d\n", g);
}

static INLINE unsigned 
choose (unsigned n)
{
  return rand () % n;
}

#define NELEMS(array) ( sizeof(array) / sizeof(array[0]) )

static Instruc
random_instruc (void)
{
  Instruc result;
  result.type = choose (NELEMS (op_types));
  result.argument = choose (200) - 100;
  return result;
}

static void
randomize (int g)
{
  int i;
  check_coord (g);
  for (i = 0; i != genome_length; ++i)
    genome[g][i] = random_instruc ();
}

static void
point_mutation (Instruc *ins)
{
  *ins = random_instruc ();
}

static void
mutate (int g)
{
  int i;
  check_coord (g);
  for (i = 0; i != genome_length; ++i)
    if (choose (100) < mutation_rate)
      point_mutation (&genome[g][i]);
}

/* Build the phenotype for genome #g and display it on its tile in the
   screen buffer. */
static void
evaluate (int g)
{
  int i;
  check_coord (g);
  reset ();
  clear_tile (g);
  for (i = 0; i != genome_length; ++i)
    {
      int t = genome[g][i].type;
      int a = genome[g][i].argument;
      op_types[t].handler (a);
    }
  display (g);
}

static void
copy (int g, int h)
{
  check_coord (g);
  check_coord (h);
  memcpy (genome[g], genome[h], sizeof genome[g]);
}

/* Return true iff tile images g and h are identical. */
static int
tsame (int g, int h)
{
  check_coord (g);
  check_coord (h);
  {
    int gx0 = (g % cols) * tile_width;
    int gy0 = (g / cols) * tile_height;

    int hx0 = (h % cols) * tile_width;
    int hy0 = (h / cols) * tile_height;

    int x, y;
    for (y = 0; y != tile_height; ++y)
      for (x = 0; x != tile_width; ++x)
	if (get (gx0 + x, gy0 + y) != get (hx0 + x, hy0 + y))
	  return 0;
    return -1;
  }
}


/* Genotype I/O */

static void
write_instruc (FILE *out, Instruc p)
{
  if (1 == op_types[p.type].num_arguments)
    fprintf (out, " %d", p.argument);
  fprintf (out, " %s", op_types[p.type].name);
}

static void
write_genome (FILE *out, int g)
{
  int i;
  for (i = 0; i != genome_length; ++i)
    write_instruc (out, genome[g][i]);
  fprintf (out, "\n");
}

static void
dump_genome (int g)
{
  check_coord (g);
  write_genome (stdout, g);
}


/* Main */

void
install_turtle_words (ts_VM *vm)
{
  ts_install (vm, "tile-width",     ts_do_push, tile_width);
  ts_install (vm, "tile-height",    ts_do_push, tile_height);
  ts_install (vm, "tcols",          ts_do_push, cols);
  ts_install (vm, "trows",          ts_do_push, rows);

  ts_install (vm, "plot", ts_run_void_0, (int) plot);
  ts_install (vm, "fd", ts_run_void_1, (int) fd);
  ts_install (vm, "lt", ts_run_void_1, (int) lt);
  ts_install (vm, "hatch[", ts_run_void_0, (int) hatch_start);
  ts_install (vm, "]", ts_run_void_0, (int) end);
  ts_install (vm, "diffuse", ts_run_void_0, (int) diffuse);

  ts_install (vm, "display", ts_run_void_1, (int) display);
  ts_install (vm, "tcopy", ts_run_void_2, (int) copy);
  ts_install (vm, "tsame?", ts_run_int_2, (int) tsame);
  ts_install (vm, "dump-genome", ts_run_void_1, (int) dump_genome);
  ts_install (vm, "randomize", ts_run_void_1, (int) randomize);
  ts_install (vm, "evaluate", ts_run_void_1, (int) evaluate);
  /* oops, we were using the same word for evo's mutate: */
  ts_install (vm, "fuck", ts_run_void_1, (int) mutate);

  reset ();
}
