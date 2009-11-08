/* TUSL -- the ultimate scripting language.
   Copyright 2003 Darius Bacon under the terms of the MIT X license
   found at http://www.opensource.org/licenses/mit-license.html */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tusl.h"

/* Boolean values.  We use these unusual names because true and false
   may be already taken, yet we can't rely on that either. */
enum { no = 0, yes = 1 };


/* Source locations */

/* Represent the beginning of a file. */
static ts_Place
make_origin_place (const char *opt_filename)
{
  ts_Place place = { 1, 1, opt_filename };
  return place;
}

/* Update place to reflect reading one character, c. */
static void
advance (ts_Place *place, char c)
{
  if ('\n' == c)
    ++(place->line), place->column = 0;
  else
    ++(place->column);
}

/* Print a place the way Emacs likes to see them in error messages. */
static void
print_place (const ts_Place *place)
{
  if (NULL != place->opt_filename && '\0' != place->opt_filename[0])
    fprintf (stderr, "%s:", place->opt_filename);
  fprintf (stderr, "%d.%d: ", place->line, place->column);
}


/* Exceptions */

/* Pop the current exception handler and jump to it. */
static void 
escape (ts_VM *vm)
{
  ts_Handler_frame *frame = vm->handler_stack;
  if (NULL == frame)
    exit (1);
  vm->handler_stack = frame->next;
  longjmp (frame->state, 1);
}

/* The default error action: complain to stderr. */
static void
default_error (ts_VM *vm, const char *message, va_list args)
{
  print_place (&vm->token_place);
  vfprintf (stderr, message, args);
  fprintf (stderr, "\n");
}

/* Perform vm's current error action on `message' and the following
   (printf-style) arguments, then either escape to the current
   exception handler. */
void
ts_error (ts_VM *vm, const char *message, ...)
{
  va_list args;

  /* TODO:
     It would be nice to flush output here, but that could raise
     another error.  How best to handle this?  Maybe we should just
     forget about buffering, for now. */

  va_start (args, message);
  vm->error (vm, message, args);
  va_end (args);

  escape (vm);
}


/* Memory management */

/* Return a newly malloc'd block of `size' bytes.  Raise an error if
   out of space. */
static void *
xmalloc (ts_VM *vm, size_t size)
{
  void *p = malloc (size);
  if (NULL == p && 0 != size)
    ts_error (vm, "%s", strerror (errno));
  return p;
}

/* Return a newly malloc'd copy of s. */
static char *
save_string (ts_VM *vm, const char *s)
{
  char *t = xmalloc (vm, strlen (s) + 1);
  strcpy (t, s);
  return t;
}


/* Misc VM operations */

/* Return the index of the top of vm's stack. */
static INLINE int
stack_pointer (ts_VM *vm)
{
  return vm->sp / (int)sizeof vm->stack[0];
}

/* Return a native pointer to byte i in vm's data space. */
static INLINE unsigned char *
data_byte (ts_VM *vm, int i)
{
  if (ts_data_size <= (unsigned)i)
    ts_error (vm, "Data reference out of range: %d", i);
  return (unsigned char *)(vm->data + i);
}

/* Return a native pointer to cell i in vm's data space. */
static INLINE int *
data_cell (ts_VM *vm, int i)
{
  return (int *)data_byte (vm, i);
}

/* Print the stack to `out'. */
static void
print_stack (ts_VM *vm, FILE *out)
{
  int i;
  for (i = 0; i <= stack_pointer (vm); ++i)
    fprintf (out, " %d", vm->stack[i]);
}

/* Return the first cell boundary at or after n. */
static INLINE int
cell_align (int n)
{
  return (n + sizeof(int) - 1) & ~(sizeof(int) - 1);
}

/* Append an int to vm's data area. */
static void
compile (ts_VM *vm, int c)
{
  vm->here = cell_align (vm->here);
  if (vm->there <= vm->here)
    ts_error (vm, "Out of data space");
  *data_cell (vm, vm->here) = c;
  vm->here += sizeof c;
}

/* Prepend string to vm's string area, returning its index in data space. */
static int
compile_string (ts_VM *vm, const char *string)
{
  int size = strlen (string) + 1;
  if (vm->there - size < vm->here)
    ts_error (vm, "Out of string space");
  vm->there -= size;
  strcpy (vm->data + vm->there, string);
  return vm->there;
}

/* The default tracing action: print the current word and stack to
   stderr. */
int
ts_default_tracer (ts_VM *vm, unsigned word)
{
  fprintf (stderr, "trace: %-12s", vm->words[word].name);
  print_stack (vm, stderr);
  fprintf (stderr, "\n");
  return no;
}

/* Push `c' onto vm's stack. */
void
ts_push (ts_VM *vm, int c)
{
  ts_INPUT_0 (vm);
  ts_OUTPUT_1 (c);
}

/* Return the top popped off vm's stack. */
int
ts_pop (ts_VM *vm)
{
  ts_INPUT_1 (vm, z);
  ts_OUTPUT_0 ();
  return z;
}


/* I/O streams */

/* Initialize a stream with the given closure. */
static void
set_stream (ts_Stream *stream, ts_Streamer *streamer, void *data,
	    const char *opt_filename)
{
  stream->ptr = stream->limit = stream->buffer;
  stream->streamer = streamer;
  stream->data = data;
  stream->place = make_origin_place (opt_filename);
}

/* Throw away any characters already buffered from vm's input. */
static void
discard_input (ts_VM *vm)
{
  ts_Stream *input = &vm->input;
  for (; input->ptr < input->limit; ++(input->ptr))
    advance (&input->place, input->ptr[0]);
  input->ptr = input->limit = input->buffer;
}

/* Refill vm's input buffer from its input source, return the first
   new character, and consume it if delta == 1.
   Pre: 0 <= delta <= 1 */
static int
refill (ts_VM *vm, int delta)
{
  ts_Stream *input = &vm->input;
  int nread = input->streamer (vm);
  if (nread <= 0)
    return EOF;
  input->ptr = input->buffer;
  input->limit = input->buffer + nread;
  {
    int result = input->ptr[0];
    if (delta)
      {
	advance (&input->place, result);
	++(input->ptr);
      }
    return result;
  }
}

/* Force any buffered output characters onto the output sink. */
static void
flush_output (ts_VM *vm)
{
  ts_Stream *output = &vm->output;
  /* TODO: allow streamer to only partially flush the buffer? */
  output->streamer (vm);
  output->ptr = output->buffer;
  output->limit = output->buffer + sizeof output->buffer;
}

/* A ts_Streamer that reads from a FILE *. */
static int
read_from_file (ts_VM *vm)
{
  ts_Stream *input = &vm->input;
  FILE *fp = (FILE *) input->data;
  /* FIXME: handle null bytes */
  if (NULL != fgets (input->buffer, sizeof input->buffer, fp))
    return strlen (input->buffer); 
  if (ferror (fp))
    ts_error (vm, "Read error: %s", strerror (errno));
  return 0;
}

/* A ts_Streamer that writes to a FILE *. */
static int
write_to_file (ts_VM *vm)
{
  ts_Stream *output = &vm->output;
  FILE *fp = (FILE *) output->data;
  int n = output->ptr - output->buffer;
  if (n != fwrite (output->buffer, 1, n, fp))
    ts_error (vm, "Write error: %s", strerror (errno));
  return n;
}

/* Set vm's input to come from fp. */
void
ts_set_input_file_stream (ts_VM *vm, FILE *fp, const char *opt_filename)
{
  set_stream (&vm->input, read_from_file, (void *) fp, opt_filename);
}

/* Set vm's output to go to fp. */
void
ts_set_output_file_stream (ts_VM *vm, FILE *fp, const char *opt_filename)
{
  set_stream (&vm->output, write_to_file, (void *) fp, opt_filename);
}

/* A ts_Streamer for inputs that never need refilling. */
static int
never_refill (ts_VM *vm)
{
  return 0;
}

/* Set vm's input to come from string.  You should not mutate the string
   after this until the input has all been read. */
void
ts_set_input_string (ts_VM *vm, const char *string)
{
  ts_Stream *input = &vm->input;
  /* Normally ptr and limit are always within buffer, but this time
     we cheat and use the string directly without copying it. */
  input->ptr = (char *)string;
  input->limit = (char *)string + strlen (string);
  input->streamer = never_refill;
  input->data = NULL;
}

/* Consume and return one character (or EOF) from vm's input source. */
static INLINE int
get_char (ts_VM *vm)
{
  ts_Stream *input = &vm->input;
  if (input->ptr == input->limit)
    return refill (vm, 1);
  {
    int result = input->ptr++[0];
    advance (&input->place, result);
    return result;
  }
}

/* Return one character (or EOF) from vm's input source, without
   consuming it. */
static INLINE int
peek_char (ts_VM *vm)
{
  ts_Stream *input = &vm->input;
  if (input->ptr == input->limit)
    return refill (vm, 0);
  return input->ptr[0];
}

/* Write string (of length size) to vm's output. */
static void
put_string (ts_VM *vm, const char *string, int size)
{
  ts_Stream *output = &vm->output;
  int i, newline = no;
  for (i = 0; i < size; ++i)
    {
      if (output->ptr == output->limit)
	flush_output (vm);
      output->ptr++[0] = string[i];
      if ('\n' == string[i])
	newline = yes;
    }
  if (newline)
    flush_output (vm);
}

/* Write c to vm's output. */
static void
put_char (ts_VM *vm, char c)
{
  put_string (vm, &c, 1);
}

/* Write n, formatted as a decimal number, to vm's output. */
static void
put_decimal (ts_VM *vm, int n)
{
  char s[20];
  put_string (vm, s, sprintf (s, "%d", n));
}

/* Write d, formatted as a decimal float number, to vm's output. */
static void
put_double (ts_VM *vm, double d)
{
  char s[20];
  put_string (vm, s, sprintf (s, "%.20g", d));
}


/* The dictionary */

/* Return the index of the last-defined word named `name', or else
   ts_not_found. */
int
ts_lookup (ts_VM *vm, const char *name)
{
  int i;
  for (i = vm->where - 1; 0 <= i; --i)
    if (NULL != vm->words[i].name && 
	0 == strcmp (name, vm->words[i].name))
      return i;
  return ts_not_found;
}

/* Add a word named `name' to vm's dictionary.  The name is not copied,
   so you shouldn't reuse its characters for anything else. */
void
ts_install (ts_VM *vm, char *name, ts_Action *action, int datum)
{
  if (ts_dictionary_size <= vm->where)
    ts_error (vm, "Too many words");
  if (ts_not_found != ts_lookup (vm, name))
    /* FIXME: this is pretty crude -- sometimes we won't want to be
       bothered by these warnings */
    fprintf (stderr, "Warning: redefinition of %s\n", name);
  {
    ts_Word *w = vm->words + vm->where++;
    w->action = action;
    w->datum = datum;
    w->name = name;
  }
}


/* VM creation/destruction and special primitives */

/* Primitive to push a literal value. */
static void
ts_do_literal (ts_VM *vm, ts_Word *pw)
{
  ts_INPUT_0 (vm); 
  ts_OUTPUT_1 (vm->pc++[0]); 
}

/* Primitive to pop, then jump if zero. */
void
ts_do_branch (ts_VM *vm, ts_Word *pw) 
{
  ts_INPUT_1 (vm, z);
  int y = vm->pc++[0];
  if (0 == z)
    vm->pc = data_cell (vm, y);
  ts_OUTPUT_0 ();
}

static void
ts_do_will (ts_VM *vm, ts_Word *pw);

/* Reclaim a vm. */
void
ts_vm_unmake (ts_VM *vm)
{
  flush_output (vm);
  free (vm);
}

/* Return a newly malloc'd vm, or NULL if out of memory.  Its
   dictionary and data area are empty except for certain reserved
   entries. */
ts_VM *
ts_vm_make (void)
{
  ts_VM *vm = malloc (sizeof *vm);
  if (NULL == vm)
    return NULL;

  vm->sp = -((int) sizeof vm->stack[0]);
  vm->pc = NULL;
  vm->here = 0;
  vm->there = ts_data_size;
  vm->where = 0;
  vm->mode = '(';
  ts_set_output_file_stream (vm, stdout, NULL);
  ts_set_input_file_stream (vm, stdin, NULL);
  vm->token_place = vm->input.place;
  vm->error = default_error;
  vm->error_data = NULL;
  vm->tracer = NULL;
  vm->tracer_data = NULL;
  vm->handler_stack = NULL;

  /* Internals depend on the order of these first definitions;
     see enums below. */
  ts_install (vm, ";",            NULL, 0);
  ts_install (vm, "<<literal>>",  ts_do_literal, 0);
  ts_install (vm, "<<branch>>",   ts_do_branch, 0);
  ts_install (vm, "z",            NULL, 0);
  ts_install (vm, "y",            NULL, 0);
  ts_install (vm, "x",            NULL, 0);
  ts_install (vm, "w",            NULL, 0);
  ts_install (vm, "z-",           NULL, 0);
  ts_install (vm, "yz-",          NULL, 0);
  ts_install (vm, "xyz-",         NULL, 0);
  ts_install (vm, "wxyz-",        NULL, 0);
  ts_install (vm, ";will",        NULL, 0);
  ts_install (vm, "<<will>>",     ts_do_will, 0);


  return vm;
}

enum { 
  EXIT = 0,			/* Dictionary index of the ";" word */
  LITERAL,	      /* Dictionary index of the "<<literal>>" word */
  BRANCH,	       /* Dictionary index of the "<<branch>>" word */
  LOCAL0,
  LOCAL1,
  LOCAL2,
  LOCAL3,
  GRAB1,
  GRAB2,
  GRAB3,
  GRAB4,
  WILL,
  DO_WILL,
  LAST_SPECIAL_PRIM = DO_WILL
};

enum { max_locals = 4 };

/* Compile a literal value to be pushed at runtime. */
static void
compile_push (ts_VM *vm, int c)
{
  compile (vm, LITERAL);
  compile (vm, c);
}


/* Primitives */

/* Execute a colon definition. */
static void 
do_sequence (ts_VM *vm, ts_Word *pw) 
{
  int locals[max_locals];
  int *old_pc = vm->pc;
  vm->pc = data_cell (vm, pw->datum);

  {			 /* TODO: eliminate overhead of setjmp here */
    ts_TRY (vm, frame)
      {
	for (;;)
	  {		    /* This code also appears in ts_run(). */
	    unsigned word = *(vm->pc)++;

	    if (NULL != vm->tracer && vm->tracer (vm, word))
	      break;

	    if (EXIT == word)
	      break;
	    else if ((unsigned)(word - LOCAL0) < (unsigned)max_locals)
	      ts_push (vm, locals[word - LOCAL0]);
	    else if ((unsigned)(word - GRAB1) < (unsigned)max_locals)
	      {				/* Grab locals */
		int i, count = 1 + (word - GRAB1);
		for (i = 0; i < count; ++i)
		  locals[i] = ts_pop (vm); /* TODO: speed up */
	      }
	    else if (WILL == word)
	      {
		/*
		  Post:
		  word: action = ts_do_will, datum = p
		  p: script_location
		 */
		ts_Word *w = vm->words + vm->where - 1;
		w->action = ts_do_will;
		*data_cell (vm, w->datum) = (char*)vm->pc - vm->data;
		break;
	      }
	    else if (word < (unsigned)(vm->where))
	      {
		ts_Action *action = vm->words[word].action;
		if (do_sequence == action && EXIT == vm->pc[0])
		  vm->pc = data_cell (vm, vm->words[word].datum); /* tail call */
		else
		  action (vm, &(vm->words[word]));
	      }
	    else
	      ts_error (vm, "Invoked an undefined word, #%d", word);
	  }

	vm->pc = old_pc;
	ts_POP_TRY (vm, frame);
      }
    ts_EXCEPT (vm, frame)
      {
	vm->pc = old_pc;
	escape (vm);
      }
  }
}

/* Execute the word that's at the given dictionary index. */
void
ts_run (ts_VM *vm, int word)
{
  /* This is do_sequence's loop body, minus the words that make no
     sense outside an instruction sequence. */
  do {
    if (NULL != vm->tracer && vm->tracer (vm, word))
      break;

    if ((unsigned)word <= LAST_SPECIAL_PRIM)
      ts_error (vm, "execute of a sequential-only word: %d", word);
    else if ((unsigned)word < (unsigned)(vm->where))
      vm->words[word].action (vm, &(vm->words[word]));
    else
      ts_error (vm, "Invoked an undefined word, #%d", word);
  } while (0);
}

/* The behavior of a word whose action was set by ;will.  (Like DOES>
   in Forth.) */
static void
ts_do_will (ts_VM *vm, ts_Word *pw)
{
  /*
    Pre:
    word: action = ts_do_will, datum = p
    p: script_location
  */
  ts_Word phony = { NULL, *data_cell (vm, pw->datum), NULL };
  ts_push (vm, pw->datum + sizeof(int));
  do_sequence (vm, &phony);
}

/* define<n> defines a primitive with <n> inputs popped from the
   stack.  Those top-of-stack inputs are named x, y, and z within the
   body (with z being the top, y being next, etc.). */
#define define0(name, code) \
  void name (ts_VM *vm, ts_Word *pw) { ts_INPUT_0 (vm); code }
#define define1(name, code) \
  void name (ts_VM *vm, ts_Word *pw) { ts_INPUT_1 (vm, z); code }
#define define2(name, code) \
  void name (ts_VM *vm, ts_Word *pw) { ts_INPUT_2 (vm, y, z); code }

define0 (ts_do_push,      ts_OUTPUT_1 (pw->datum); )

define1 (ts_make_literal, ts_OUTPUT_0 (); compile_push (vm, z); )
define1 (ts_execute,      ts_OUTPUT_0 (); ts_run (vm, z); )
define1 (ts_to_data,      ts_OUTPUT_1 ((int) data_byte (vm, z)); )
define1 (ts_comma,        ts_OUTPUT_0 (); compile (vm, z); )
define1 (ts_allot,        ts_OUTPUT_0 (); vm->here += z; ) /* FIXME overflow */
define0 (ts_align_bang,   ts_OUTPUT_0 (); vm->here = cell_align (vm->here); )
define0 (ts_here,         ts_OUTPUT_1 (vm->here); )

static INLINE void
nonzero (ts_VM *vm, int z)
{
  if (0 == z)
    ts_error (vm, "Division by 0");
}

define2 (ts_add,          ts_OUTPUT_1 (y + z); )
define2 (ts_sub,          ts_OUTPUT_1 (y - z); )
define2 (ts_mul,          ts_OUTPUT_1 (y * z); )
define2 (ts_umul,         ts_OUTPUT_1 ((unsigned)y * (unsigned)z); )
define2 (ts_idiv, nonzero (vm, z); ts_OUTPUT_1 (y / z); )
define2 (ts_imod, nonzero (vm, z); ts_OUTPUT_1 (y % z); )
define2 (ts_udiv, nonzero (vm, z); ts_OUTPUT_1 ((unsigned)y / (unsigned)z); )
define2 (ts_umod, nonzero (vm, z); ts_OUTPUT_1 ((unsigned)y % (unsigned)z); )
define2 (ts_eq,           ts_OUTPUT_1 (-(y == z)); )
define2 (ts_lt,           ts_OUTPUT_1 (-(y < z)); )
define2 (ts_ult,          ts_OUTPUT_1 (-((unsigned)y < (unsigned)z)); )
define2 (ts_and,          ts_OUTPUT_1 (y & z); )
define2 (ts_or,           ts_OUTPUT_1 (y | z); )
define2 (ts_xor,          ts_OUTPUT_1 (y ^ z); )
define2 (ts_lshift,       ts_OUTPUT_1 (y << z); )
define2 (ts_rshift,       ts_OUTPUT_1 (y >> z); )
define2 (ts_urshift,      ts_OUTPUT_1 ((unsigned)y >> (unsigned)z); )

define1 (ts_fetchu,       ts_OUTPUT_1 (*(int *)z); )
define1 (ts_cfetchu,      ts_OUTPUT_1 (*(unsigned char *)z); )
define2 (ts_storeu,       ts_OUTPUT_0 (); *(int *)z = y; )
define2 (ts_cstoreu,      ts_OUTPUT_0 (); *(unsigned char *)z = y; )
define2 (ts_plus_storeu,  ts_OUTPUT_0 (); *(int *)z += y; )

define1 (ts_fetch,        ts_OUTPUT_1 (*data_cell (vm, z)); )
define1 (ts_cfetch,       ts_OUTPUT_1 (*data_byte (vm, z)); )
define2 (ts_store,        ts_OUTPUT_0 (); *data_cell (vm, z) = y; )
define2 (ts_cstore,       ts_OUTPUT_0 (); *data_byte (vm, z) = y; )
define2 (ts_plus_store,   ts_OUTPUT_0 (); *data_cell (vm, z) += y; )

define0 (ts_start_tracing,ts_OUTPUT_0 (); vm->tracer = ts_default_tracer; )
define0 (ts_stop_tracing, ts_OUTPUT_0 (); vm->tracer = NULL; )

define1 (ts_add2,         ts_OUTPUT_1 (z + 2); )
define1 (ts_add1,         ts_OUTPUT_1 (z + 1); )
define1 (ts_sub1,         ts_OUTPUT_1 (z - 1); )
define1 (ts_sub2,         ts_OUTPUT_1 (z - 2); )
define1 (ts_is_negative,  ts_OUTPUT_1 (-(z < 0)); )
define1 (ts_is_zero,      ts_OUTPUT_1 (-(0 == z)); )
define1 (ts_times2,       ts_OUTPUT_1 (z << 1); )
define1 (ts_times4,       ts_OUTPUT_1 (z << 2); )
define1 (ts_div2,         ts_OUTPUT_1 (z >> 1); )
define1 (ts_div4,         ts_OUTPUT_1 (z >> 2); )

define1 (ts_emit,         ts_OUTPUT_0 (); put_char (vm, z); )
define1 (ts_print,        ts_OUTPUT_0 (); 
                          put_decimal (vm, z); put_char (vm, ' '); )

define1 (ts_prim_error,   ts_OUTPUT_0 (); 
                          ts_error (vm, "%s", data_byte (vm, z)); )

define1 (ts_prim_load,    ts_OUTPUT_0 (); 
	                  ts_load (vm, data_byte (vm, z)); )

/* Pop the top of stack (call it z), and change the last-defined word
   to be a constant with value z. */
void
ts_make_constant (ts_VM *vm, ts_Word *pw)
{
  ts_Word *w = vm->words + vm->where - 1;
  ts_INPUT_1 (vm, z);
  ts_OUTPUT_0 ();
  w->action = ts_do_push;
  w->datum = z;
}

/* Print vm's stack as decimal numbers to vm's output. */
void
ts_print_stack (ts_VM *vm, ts_Word *pw)
{
  int i;
  for (i = 0; i <= stack_pointer (vm); ++i)
    {
      if (0 < i)
	put_char (vm, ' ');
      put_decimal (vm, vm->stack[i]);
    }
  put_char (vm, '\n');
}

/* Make vm's stack empty. */
void
ts_clear_stack (ts_VM *vm, ts_Word *pw)
{
  vm->sp = -((int) sizeof vm->stack[0]);
}

/* C interface primitives.  Each run_foo_n primitive calls its
   pw->datum as a C function pointer taking n arguments and returning
   foo (either void or a single int value).  The n arguments are first
   popped from the stack (with the topmost as the rightmost argument).
   If there's a return value, it is then pushed on the stack. */

void
ts_run_void_0 (ts_VM *vm, ts_Word *pw)
{
  void (*f)(void) = (void (*)(void)) pw->datum;
  f ();
}

void
ts_run_void_1 (ts_VM *vm, ts_Word *pw)
{
  void (*f)(int) = (void (*)(int)) pw->datum;
  ts_INPUT_1 (vm, z);
  ts_OUTPUT_0 ();
  f (z);
}

void
ts_run_void_2 (ts_VM *vm, ts_Word *pw)
{
  void (*f)(int, int) = (void (*)(int, int)) pw->datum;
  ts_INPUT_2 (vm, y, z);
  ts_OUTPUT_0 ();
  f (y, z);
}

void
ts_run_void_3 (ts_VM *vm, ts_Word *pw)
{
  void (*f)(int, int, int) = (void (*)(int, int, int)) pw->datum;
  ts_INPUT_3 (vm, x, y, z);
  ts_OUTPUT_0 ();
  f (x, y, z);
}

void
ts_run_void_4 (ts_VM *vm, ts_Word *pw)
{
  void (*f)(int, int, int, int) = (void (*)(int, int, int, int)) pw->datum;
  ts_INPUT_4 (vm, w, x, y, z);
  ts_OUTPUT_0 ();
  f (w, x, y, z);
}

void
ts_run_void_5 (ts_VM *vm, ts_Word *pw)
{
  void (*f)(int, int, int, int, int) = 
    (void (*)(int, int, int, int, int)) pw->datum;
  ts_INPUT_5 (vm, v, w, x, y, z);
  ts_OUTPUT_0 ();
  f (v, w, x, y, z);
}

void
ts_run_int_0 (ts_VM *vm, ts_Word *pw)
{
  int (*f)(void) = (int (*)(void)) pw->datum;
  ts_INPUT_0 (vm);
  ts_OUTPUT_1 (f ());
  /* FIXME: what if an exception is thrown inside f()?  We'd have the stack
     unadjusted still. */
}

void
ts_run_int_1 (ts_VM *vm, ts_Word *pw)
{
  int (*f)(int) = (int (*)(int)) pw->datum;
  ts_INPUT_1 (vm, z);
  ts_OUTPUT_1 (f (z));
}

void
ts_run_int_2 (ts_VM *vm, ts_Word *pw)
{
  int (*f)(int, int) = (int (*)(int, int)) pw->datum;
  ts_INPUT_2 (vm, y, z);
  ts_OUTPUT_1 (f (y, z));
}

void
ts_run_int_3 (ts_VM *vm, ts_Word *pw)
{
  int (*f)(int, int, int) = (int (*)(int, int, int)) pw->datum;
  ts_INPUT_3 (vm, x, y, z);
  ts_OUTPUT_1 (f (x, y, z));
}

void
ts_run_int_4 (ts_VM *vm, ts_Word *pw)
{
  int (*f)(int, int, int, int) = (int (*)(int, int, int, int)) pw->datum;
  ts_INPUT_4 (vm, w, x, y, z);
  ts_OUTPUT_1 (f (w, x, y, z));
}

static INLINE float i2f (int i) { return *(float*)&i; }
static INLINE float f2i (int f) { return *(int*)&f; }

define2 (ts_fadd, ts_OUTPUT_1 (f2i (i2f (y) + i2f (z))); )
define2 (ts_fsub, ts_OUTPUT_1 (f2i (i2f (y) - i2f (z))); )
define2 (ts_fmul, ts_OUTPUT_1 (f2i (i2f (y) * i2f (z))); )
define2 (ts_fdiv, ts_OUTPUT_1 (f2i (i2f (y) / i2f (z))); )

define1 (ts_fprint, ts_OUTPUT_0 (); 
	            put_double (vm, i2f (z)); put_char (vm, ' '); )

/* Add all the safe built-in primitives to vm's dictionary. */
void
ts_install_standard_words (ts_VM *vm)
{
  ts_install (vm, "#",            ts_make_literal, 0);
  ts_install (vm, ",",            ts_comma, 0);
  ts_install (vm, "here",         ts_here, 0);
  ts_install (vm, "allot",        ts_allot, 0);
  ts_install (vm, "align!",       ts_align_bang, 0);
  ts_install (vm, "constant",     ts_make_constant, 0);

  ts_install (vm, "+",            ts_add, 0);
  ts_install (vm, "-",            ts_sub, 0);
  ts_install (vm, "*",            ts_mul, 0);
  ts_install (vm, "/",            ts_idiv, 0);
  ts_install (vm, "mod",          ts_imod, 0); /* TODO: rename to % ? */
  ts_install (vm, "u*",           ts_umul, 0);
  ts_install (vm, "u/",           ts_udiv, 0);
  ts_install (vm, "umod",         ts_umod, 0);
  ts_install (vm, "=",            ts_eq, 0);
  ts_install (vm, "<",            ts_lt, 0);
  ts_install (vm, "u<",           ts_ult, 0);
  ts_install (vm, "and",          ts_and, 0);
  ts_install (vm, "or",           ts_or, 0);
  ts_install (vm, "xor",          ts_xor, 0);
  ts_install (vm, "<<",           ts_lshift, 0);
  ts_install (vm, ">>",           ts_rshift, 0);
  ts_install (vm, "u>>",          ts_urshift, 0);

  ts_install (vm, "@",            ts_fetch, 0);
  ts_install (vm, "!",            ts_store, 0);
  ts_install (vm, "c@",           ts_cfetch, 0);
  ts_install (vm, "c!",           ts_cstore, 0);
  ts_install (vm, "+!",           ts_plus_store, 0);

  ts_install (vm, "emit",         ts_emit, 0);
  ts_install (vm, ".",            ts_print, 0);

  ts_install (vm, "execute",      ts_execute, 0);
  ts_install (vm, "start-tracing",ts_start_tracing, 0);
  ts_install (vm, "stop-tracing", ts_stop_tracing, 0);
  ts_install (vm, "clear-stack",  ts_clear_stack, 0);
  ts_install (vm, ".s",           ts_print_stack, 0);
  ts_install (vm, "error",        ts_prim_error, 0);

  /* Extras for efficiency */
  ts_install (vm, "-1",           ts_do_push, -1);
  ts_install (vm, "0",            ts_do_push, 0);
  ts_install (vm, "1",            ts_do_push, 1);
  ts_install (vm, "0<",           ts_is_negative, 0);
  ts_install (vm, "0=",           ts_is_zero, 0);
  ts_install (vm, "2+",           ts_add2, 0);
  ts_install (vm, "1+",           ts_add1, 0);
  ts_install (vm, "1-",           ts_sub1, 0);
  ts_install (vm, "2-",           ts_sub2, 0);
  ts_install (vm, "2*",           ts_times2, 0);
  ts_install (vm, "2/",           ts_div2, 0);
  ts_install (vm, "4*",           ts_times4, 0);
  ts_install (vm, "4/",           ts_div4, 0);
}

/* Floating-point primitives.  These are easy to misuse since floats
   get mixed with ints on the stack without any typechecking. */
void
ts_install_float_words (ts_VM *vm)
{
  ts_install (vm, "f+",           ts_fadd, 0);
  ts_install (vm, "f-",           ts_fsub, 0);
  ts_install (vm, "f*",           ts_fmul, 0);
  ts_install (vm, "f/",           ts_fdiv, 0);
  ts_install (vm, "f.",           ts_fprint, 0);
}

/* Add all the unsafe built-in primitives to vm's dictionary.  That
   more or less means anything that could corrupt memory or open a
   file, at the moment. */
void
ts_install_unsafe_words (ts_VM *vm)
{
  ts_install (vm, ">data",        ts_to_data, 0);
  ts_install (vm, "@u",           ts_fetchu, 0);
  ts_install (vm, "!u",           ts_storeu, 0);
  ts_install (vm, "c@u",          ts_cfetchu, 0);
  ts_install (vm, "c!u",          ts_cstoreu, 0);
  ts_install (vm, "+!u",          ts_plus_storeu, 0);

  ts_install (vm, "load",         ts_prim_load, 0);
}


/* Input scanning/parsing */

/* Try to parse text as a number (either signed or unsigned).
   Return yes iff successful, and set *result to the value. */
static int
parse_number (int *result, const char *text)
{
  char *endptr;
  int value;

  if ('\0' == text[0])
    return no;

  errno = 0, value = strtol (text, &endptr, 0);
  if ('\0' != *endptr || ERANGE == errno)
    {
      errno = 0, value = strtoul (text, &endptr, 0);
      if ('\0' != *endptr || ERANGE == errno)
	{
	  /* Ugly hack to more or less support float constants */
	  float fvalue;
	  errno = 0, fvalue = (float) strtod (text, &endptr);
	  if ('\0' != *endptr || ERANGE == errno)
	    return no;
	  value = *(int *)&fvalue;
	}
    }

  *result = value;
  return yes;
}

/* Add c to buf at position i. 
   Pre: i < size */
static void
append (ts_VM *vm, char *buf, int size, int i, char c)
{
  if (size == i + 1)
    {
      buf[i] = '\0';
      ts_error (vm, "Token too long: %s...", buf);
    }
  buf[i] = c;
}

#define punctuation "\\':()$"

/* Scan the next token of input (consuming up to size-1 bytes) and
   copy it into `buf'.  Return yes if successful, or no if we reach
   EOF.
   Pre: 3 <= size. */
static int
get_token (ts_VM *vm, char *buf, int size)
{
  int c, i = 0;
  do {
    c = get_char (vm);
  } while (isspace (c) && '\n' != c);

  vm->token_place = vm->input.place;

  if (EOF == c)
    return no;

  if ('$' == c)
    {
      buf[i++] = c;
      c = get_char (vm);
      if (EOF == c)
	{
	  buf[i] = '\0';
	  ts_error (vm, "Unterminated character constant: %s", buf);
	}
      buf[i++] = c;
    }
  else if (NULL != strchr ("\n" punctuation, c))
    buf[i++] = c;
  else if ('"' == c || '`' == c) /* Scan a string literal */
    {
      /* We allow both " and ` as string delimiters because idiotic
	 libsdl parses command-line arguments wrong (at least in
	 Windows), messing up double-quoted strings.  Backquoted
	 strings are a hack around that. */
      int delim = c;
      do {
	append (vm, buf, size, i++, c);
	c = get_char (vm);
	if (EOF == c)
	  {
	    buf[i] = '\0';
	    ts_error (vm, "Unterminated string constant: %s", buf);
	  }
      } while (delim != c);	/* TODO: escape sequences */
    }
  else
    {		/* Other tokens extend to whitespace, quote, or punctuation */
      do {
	append (vm, buf, size, i++, c);
	c = peek_char (vm);
	if (NULL != strchr (" \t\r\n\"`" punctuation, c))
	  break;
	get_char (vm);
      } while (EOF != c);
    }
  buf[i] = '\0';
  return yes;
}

/* Skip past the end of the current line of input. */
static void
skip_line (ts_VM *vm)
{
  int c;
  do {
    c = get_char (vm);
  } while (EOF != c && '\n' != c);
}

/* Act on one source-code token as the current mode directs. */
static void
dispatch (ts_VM *vm, const char *token)
{
  switch (token[0])
    {
    case '\\': 			/* a comment */
      skip_line (vm); 
      break;

    case '\'': case ':': case '(': case ')': /* mode-changing characters */
      vm->mode = token[0]; 
      break;

    case '$':			/* a character literal */
      ('(' == vm->mode ? ts_push : compile_push) (vm, token[1]);
      break;

    case '"':			/* a string literal */
    case '`':
      {
	int string_index = compile_string (vm, token + 1);
	('(' == vm->mode ? ts_push : compile_push) (vm, string_index);
	break;
      }

    default:
      if ('\'' == vm->mode)	/* get word index */
	{
	  int word = ts_lookup (vm, token);
	  if (ts_not_found == word)
	    ts_error (vm, "%s ?\n", token);
	  else
	    ts_push (vm, word);
	}
      else if (':' == vm->mode)	/* define word */
	{
	  ts_install (vm, save_string (vm, token), do_sequence, vm->here);
	  vm->mode = ')'; 
	}
      else
	{
	  int word = ts_lookup (vm, token);
	  if (ts_not_found != word) /* execute or compile the word */
	    ('(' == vm->mode ? ts_run : compile) (vm, word);
	  else
	    {			/* try to treat it as a literal number */
	      int value;
	      if (parse_number (&value, token))
		('(' == vm->mode ? ts_push : compile_push) (vm, value);
	      else
		ts_error (vm, "%s ?\n", token);
	    }
	}
    }
}


/* File loading and read-eval-print loop */

/* Print a prompt with the current mode and stack height. */
static void
prompt (ts_VM *vm)
{
  int height = stack_pointer (vm) + 1;
  put_char (vm, vm->mode);
  put_char (vm, ' ');
  if (0 < height)
    {
      put_char (vm, '<');
      put_decimal (vm, height);
      put_string (vm, "> ", 2);
    }
  flush_output (vm);
}

/* Read and execute source code interactively, starting in interpret
   mode.  Interactively means: we print a prompt, and errors only
   abort the current line. */
void
ts_interactive_loop (ts_VM *vm)
{
  char token[1024];
  vm->mode = '(';

  prompt (vm);
  for (;;)
    {
      ts_TRY (vm, frame)
	{
	  if (!get_token (vm, token, sizeof token))
	    {
	      ts_POP_TRY (vm, frame);
	      break;
	    }
	  else if ('\n' == token[0])
	    prompt (vm);
	  else
	    dispatch (vm, token);
	  ts_POP_TRY (vm, frame);
	}
      ts_EXCEPT (vm, frame)
	{
	  /* FIXME: need exception info to decide what to do */
	  discard_input (vm);
	  prompt (vm);
	}
    }

  printf ("\n");
}

/* Read and execute source code from the current input stream till EOF,
   starting in interpret mode. */
void
ts_loading_loop (ts_VM *vm)
{
  char token[1024];
  vm->mode = '(';
  while (get_token (vm, token, sizeof token))
    if ('\n' != token[0])
      dispatch (vm, token);
}

/* Read and execute source code from the file named `filename',
   starting and ending in interpret mode. */
void
ts_load (ts_VM *vm, const char *filename)
{
  ts_Stream saved = vm->input;
  FILE *fp = fopen (filename, "r");
  if (NULL == fp)
    ts_error (vm, "%s: %s\n", filename, strerror (errno));
  else
    {
      ts_TRY (vm, frame)
	{
	  ts_set_input_file_stream (vm, fp, filename);
	  ts_loading_loop (vm);

	  /* FIXME: on return, token_place could still point at filename,
	     which might get freed anytime after.  Null it out or something. */
	  fclose (fp);
	  vm->mode = '(';	/* should probably move this into callee */
	  vm->input = saved;
	  ts_POP_TRY (vm, frame);
	}
      ts_EXCEPT (vm, frame)
	{
	  fclose (fp);
	  vm->mode = '(';
	  vm->input = saved;
	  escape (vm);
	}
    }
}

/* Read and execute the contents of string. */
void
ts_load_string (ts_VM *vm, const char *string)
{
  ts_set_input_string (vm, string);
  ts_loading_loop (vm);
}

/* Do an interactive loop with stream as the input. */
void
ts_load_interactive (ts_VM *vm, FILE *stream)
{
  ts_set_input_file_stream (vm, stream, NULL);
  ts_interactive_loop (vm);
}
