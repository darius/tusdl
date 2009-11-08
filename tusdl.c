#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tusdl.h"

/* The screen and its grid of pixel values. */
SDL_Surface *screen = NULL;
Pixel *grid;
Uint8 *grid8;

/* Clear the screen grid. */
static void
clear (void)
{
  memset (grid, 0, grid_width * grid_height * sizeof grid[0]);
}

static void
clear8 (void)
{
  memset (grid8, 0, grid_width * grid_height * sizeof grid8[0]);
}

static void
event_adapter (ts_VM *vm, SDL_Event *eventp)
{
  ts_INPUT_0 (vm);
  if (eventp != NULL)
    {
      switch (eventp->type) 
	{
	case SDL_KEYDOWN:
	  ts_OUTPUT_2 (eventp->key.keysym.sym, 1);
	  return;
	case SDL_MOUSEBUTTONDOWN:
	  ts_OUTPUT_2 ((eventp->button.y << 16) | eventp->button.x, 2);
	  return;
	case SDL_QUIT:
	  ts_OUTPUT_2 ('q', 1);
	  return;
	}
    }
  ts_OUTPUT_2 (0, 0);
}

/* Poll for an SDL event and push its info on the stack. */
static void
listen (ts_VM *vm, ts_Word *pw)
{
  SDL_Event event;
  event_adapter (vm, SDL_PollEvent (&event) ? &event : NULL);
}

/* Wait for an SDL event and push its info on the stack. */
static void
blocking_listen (ts_VM *vm, ts_Word *pw)
{
  SDL_Event event;
  SDL_WaitEvent (&event);
  event_adapter (vm, &event);
}

int frame;

/* Redisplay the screen. */
static void
show (void)
{
  SDL_UpdateRect (screen, 0, 0, 0, 0);
  ++frame;
}

static void
report_frames (ts_VM *vm, ts_Word *pw)
{
  clock_t end = clock ();
  clock_t start = (clock_t) vm->words[ts_lookup (vm, "starting-clocks")].datum;
  double seconds = (end - start) / (double)CLOCKS_PER_SEC;
  printf ("%d frames\n", frame);
  printf ("%.3g per second\n", frame / seconds);
  printf ("%.3g megapixels/second\n", 
	  (grid_width * grid_height * (frame / 1e6)) / seconds);
}


void
start_sdl (int bits_per_pixel)
{
  if (SDL_Init (SDL_INIT_VIDEO) < 0)
    die ("No init possible: %s\n", SDL_GetError ());
  atexit (SDL_Quit);

  screen = SDL_SetVideoMode (grid_width, grid_height, bits_per_pixel, 
			     SDL_SWSURFACE | SDL_HWPALETTE);
  if (screen == NULL)
    die ("Couldn't set video mode: %s\n", SDL_GetError ());

  grid = NULL;
  grid8 = NULL;
  if (32 == bits_per_pixel)
    grid = (Uint32 *) screen->pixels;
  else if (8 == bits_per_pixel)
    grid8 = (Uint8 *) screen->pixels;
}

static void
install_sdl_words (ts_VM *vm)
{
  ts_install (vm, "start-sdl",       ts_run_void_1,   (int) start_sdl);

  ts_install (vm, "listen",          listen,          0);
  ts_install (vm, "wait",            blocking_listen, 0);
  ts_install (vm, "clear",           ts_run_void_0,   (int) clear);
  ts_install (vm, "clear8",          ts_run_void_0,   (int) clear8);
  ts_install (vm, "show",            ts_run_void_0,   (int) show);

  /* FIXME: needs bounds-check */
  ts_install (vm, "grid@",           ts_run_int_2,    (int) get);
  ts_install (vm, "grid!",           ts_run_void_3,   (int) put);

  ts_install (vm, "frames",          ts_do_push,      (int) &frame);

  ts_install (vm, "width",           ts_do_push,      grid_width);
  ts_install (vm, "height",          ts_do_push,      grid_height);

  ts_install (vm, "red",             ts_do_push,      red);
  ts_install (vm, "green",           ts_do_push,      green);
  ts_install (vm, "blue",            ts_do_push,      blue);

  ts_load (vm, "sim.ts");

  ts_install (vm, "starting-clocks", ts_do_push,      (int) clock ());
  ts_install (vm, "report-frames",   report_frames,   0);
}

ts_VM *
make_sdl_vm (void)
{
  ts_VM *vm = ts_vm_make ();
  ts_install_standard_words (vm);
  ts_install_unsafe_words (vm);
  install_sdl_words (vm);
  return vm;
}
