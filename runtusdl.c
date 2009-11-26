#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sim.h"

/* Complain and terminate. */
void
die (const char *message, ...)
{
  va_list args;

  fprintf (stderr, "Error: ");
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  fprintf(stderr, "\n");

  exit (1);
}

int
main (int argc, char **argv)
{
  ts_VM *vm = make_sdl_vm ();
  if (NULL == vm)
    die ("%s", strerror (errno));

  seed_rand ((int) time (NULL));
  srand ((unsigned long) time (NULL));

  ts_set_output_file_stream (vm, stdout, NULL);
  ts_set_input_file_stream (vm, stdin, NULL);

  ts_install (vm, "exit", ts_run_void_1, (int) exit);
  install_ants_words (vm);
  install_casdl_words (vm);
  install_evo_words (vm);
  install_orbit_words (vm);
  install_slime_words (vm);
  install_termite_words (vm);
  install_turtle_words (vm);
  install_wator_words (vm);

  if (1 == argc)
    ts_load_interactive (vm, stdin);
  else
    {
      int i;
      for (i = 1; i < argc; ++i)
	ts_load_string (vm, argv[i]);
    }

  ts_vm_unmake (vm);
  return 0;
}
