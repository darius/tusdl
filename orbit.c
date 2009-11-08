#include <math.h>
#include <stdio.h>
#include "sim.h"

const double G  = 1.0e-6;
const double dt = 0.001;

typedef struct Particle Particle;
struct Particle {
  double m;			/* Mass */
  double rx, ry;		/* Position(t) */
  double vx, vy;		/* Velocity(t-dt/2) */
};

enum { max_particles = 1024 };

static Particle particles[max_particles];
static int num_particles = 0;


/* SDL stuff */

/* Boxes bounding changes to the grid since the last multishow() (inclusive). */
static SDL_Rect bounds[max_particles];

/* Redisplay the screen. */
static void
multishow (void)
{
  SDL_UpdateRects (screen, num_particles, bounds);
  ++frame;
}

/* Plot a point on the screen-grid.  This assumes we get called twice
   per frame for each index i, first with color==black to unplot the
   old position, and then with some other color to plot the new
   position. */
static INLINE void
put_point (unsigned gx, unsigned gy, Pixel color, int i)
{
  put (gx, gy, color);

  if (color == black)
    {
      bounds[i].x = gx;
      bounds[i].y = gy;
    }
  else
    {
      int w = gx - bounds[i].x;
      int h = gy - bounds[i].y;
      if (w < 0)
	bounds[i].x += w, w = -w;
      if (h < 0)
	bounds[i].y += h, h = -h;

      bounds[i].w = w + 1;
      bounds[i].h = h + 1;
    }
}

#define scale (1/3.0)

/* Plot a particle on the screen. */
static INLINE void
put_particle (int i, Pixel color)
{
  double x = particles[i].rx;
  double y = particles[i].ry;
  unsigned gx = (unsigned) (grid_width * (scale * x + 0.5)) % grid_width;
  unsigned gy = (unsigned) (grid_height * (0.5 - scale * y)) % grid_height;
  put_point (gx, gy, color, i);
}


/* The simulation */

/* Compute the force particle j exerts on particle i. */
static INLINE void
compute_force (double *Fx, double *Fy, int i, int j)
{
  double x = particles[j].rx - particles[i].rx;
  double y = particles[j].ry - particles[i].ry;
  double r = hypot (x, y);
  double F = (G * particles[i].m * particles[j].m) / (r*r*r);

  *Fx = F * x;
  *Fy = F * y;
}

/* Update the state variables by one time-step. */
static void 
update_state (void)
{
  int i, j;
  for (i = 0; i < num_particles; ++i)
    for (j = 0; j < i; ++j)
      {
	double Fx, Fy;
	compute_force (&Fx, &Fy, i, j);
	particles[i].vx += Fx * (dt / particles[i].m);
	particles[i].vy += Fy * (dt / particles[i].m);
	particles[j].vx -= Fx * (dt / particles[j].m);
	particles[j].vy -= Fy * (dt / particles[j].m);
      }

  for (i = 0; i < num_particles; ++i)
    {
      particles[i].rx += particles[i].vx * dt;
      particles[i].ry += particles[i].vy * dt;
    }
}

/* Advance the simulation by one time-step. */
static void 
tick (void)
{
  int i;
  for (i = 0; i < num_particles; ++i)
    put_particle (i, black);
  update_state ();
  for (i = 0; i < num_particles; ++i)
    put_particle (i, white);
}

static void
make_particle (int m, int rx, int ry, int vx, int vy)
{
  if (max_particles <= num_particles)
    die ("Too many particles");
  particles[num_particles].m  = m/100.0;
  particles[num_particles].rx = rx/100.0;
  particles[num_particles].ry = ry/100.0;
  particles[num_particles].vx = vx/100.0;
  particles[num_particles].vy = vy/100.0;
  ++num_particles;
}


/* Main */

void
install_orbit_words (ts_VM *vm)
{
  ts_install (vm, "make-particle",    ts_run_void_5, (int) make_particle);
  ts_install (vm, "orbit-multishow",  ts_run_void_0, (int) multishow);
  ts_install (vm, "orbit-tick",       ts_run_void_0, (int) tick);
}
