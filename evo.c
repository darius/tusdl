/*
 The engine for a generator of images by aesthetic selection. The full
 generator is a Tusl program linking in primitives from this C module.
 (Most of these comments were added long after it was written.)
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sim.h"


/* Configurable constants */

enum { 
  /* A genetic program has 'program_length' instructions, operating on a 
     circular stack of bounded depth. Upon reproduction a certain fraction
     of instructions are mutated, on average. */
  program_length  = 40,		/* genes */
  mutation_rate   = 15,		/* percent */
  stack_limit     = 6,

#if 0
  /* Image operations work on one rectangular tile at a time, for 
     efficiency; around 1k pixels turned out to be optimal, at least
     back when I wrote this. */
  tile_width      = 48,		/* in pixels */
  tile_height     = 32,

  /* A thumbnail uses a whole number of tiles, both horizontally and
     vertically. */
  thumb_cols      = 5,		/* in tiles */
  thumb_rows      = 5,
#else
  tile_width      = 32,		/* in pixels */
  tile_height     = 32,

  thumb_cols      = 4,		/* in tiles */
  thumb_rows      = 4,
#endif

  node_table_size = 101, 	/* # of buckets in a hashtable */
};

/* Derived constants */
enum {
  /* And the full image grid uses a whole number of thumbnails. */
  cols            = grid_width / (tile_width*thumb_cols), /* in thumbs */
  rows            = grid_height / (tile_height*thumb_rows),

  tile_size       = tile_width * tile_height,

  thumb_width     = grid_width / cols,
  thumb_height    = grid_height / rows,
  thumb_size      = thumb_width * thumb_height,
};

/* Pixel types -- not actually configurable without changing the code
   manipulating pixel values. */
enum {
  bits_per_pixel  = 32
};
typedef float Intensity;


/* Misc utility functions */

/* Allocate `size' bytes or die. */
static void *
allot (size_t size)
{
  void *p = malloc (size);
  if (p == NULL && size != 0)
    die ("allot: %s", strerror (errno));
  return p;
}

static void
unallot (void *p)
{
  if (p != NULL)		/* shouldn't be needed, but... */
    free (p);
}

/* Return a file open for writing whose name is generated from
   the printf-style `format' using the lowest integer that makes
   a file that doesn't already exist.
   buffer must be able to hold any sprintf of the format.
   The implementation has an obvious race condition, but I'm declaring
   it good enough anyway... as long as we don't have two evo processes
   writing into the same directory. */
static FILE *
open_save_file (char *buffer, const char *format, const char *mode)
{
  int n;
  for (n = 0; ; ++n)
    {
      FILE *f;
      sprintf (buffer, format, n);
      f = fopen (buffer, "r");
      if (f == NULL)
	return fopen (buffer, mode);
      fclose (f);
    }
}

/* Read the next token from `in', placing it nul-terminated into `buf'
   (of `size' bytes). A token is a maximal string of nonblanks.
   FIXME Hack alert: we only read up to `size'-1 bytes of the token, and
   silently return if it's longer in the input.
   Pre: 0 < size */
static void
read_token (FILE *in, char *buf, int size)
{
  int c;
  do {
    c = getc (in);
  } while (isspace (c));

  if (c == EOF)
    die ("Unexpected EOF: %s", strerror (errno));

  do {
    if (size <= 1)
      break;
    *buf++ = c;
    --size;
    c = getc (in);
  } while (c != EOF && !isspace (c));
  ungetc (c, in);

  *buf = '\0';
}

/* Return the numeric value of `token' or die. */
static double
parse_number (const char *token)
{
  char *endptr;
  double value;

  if (token[0] == '\0')
    die ("Bad data");

  errno = 0;
  value = strtod (token, &endptr);
  if (*endptr != '\0' || errno == ERANGE)
    die ("Bad data: %s", strerror (errno));

  return value;
}

/* Return a new random number in 0..n-1.
   (This could be done more randomly than with '%', you know.) */
static INLINE unsigned 
choose (unsigned n)
{
  return rand () % n;
}

/* Return a new random number in 0..1. */
static INLINE double
choose_double (void)
{
  return (double)rand () / RAND_MAX;
}


/* Image tile generation */

/* Convert an intensity ([0..1] nominal range, but may spill out of
   those limits) to a clipped RGB byte. */  
static INLINE Uint8
color_value (Intensity intensity)
{
  int i = (int) floor (256 * intensity);
  return i < 0 ? 0 : 255 < i ? 255 : i;
}

/* Loop through each local pixel coordinate (x,y) in a tile, 
   from top left by rows to bottom right.
   j is the array offset. */
#define FOR_EACH(x, y, j)              \
  int x, y, j;                         \
  for (y = 0; y < tile_height; ++y)    \
    for (x = 0, j = tile_width * y; x < tile_width; ++x, j = x + tile_width * y)

/* Write a's color values into the grid tile at (x0,y0) (upper left corner). */ 
static void
gridify (Intensity **a, int x0, int y0)
{
  Intensity *ar = a[0], *ag = a[1], *ab = a[2];
  FOR_EACH (x, y, j)
    {
      Pixel r = color_value (ab[j]) +
	(color_value (ar[j]) << 16) + 
	(color_value (ag[j]) << 8);
      put (x0 + x, y0 + y, r);
    }
}


/* Basic phenotype operations */

/* Fill dest with a constant color. */
static void
op_constant (Intensity constant_value, Intensity *dest)
{
  FOR_EACH (x, y, j)
    dest[j] = constant_value;
}

/* Fill (dr,dg,db) with RGB color values taken by interpreting the
   corresponding (ar,ag,ab) pixel values as HWB colors.

   The HWB color space is some random space meant to be 'intuitive' --
   I was hoping that'd bring out striking colors, and it worked pretty
   well. I think it deserves a lot of any credit for this program's
   appeal compared to other genetic art thingies I've seen. (It turns
   out most saved genomes invoke the 'hwb' op at least twice,
   suggesting that the niceness isn't just a matter of interpreting a
   final 3d result as a point in this colorspace.)

   I believe I adapted the code from this paper:
   http://alvyray.com/Papers/PapersCG.htm#HWB
   http://alvyray.com/Papers/CG/hwb2rgb.htm

   Tweaked partly because of the different function interface and
   partly because it was producing NaN's or something sometimes.
   (I don't remember exactly.)

   XXX We seem to be seeing different, and worse, colors when running
   saved genomes from the Elder Days. Since Intensity is float rather
   than double, maybe it'd help to use single-precision float functions
   here? -- like modff instead of modf, and fmodf instead of fmod?
   Grasping at straws, hurray. Of course, the discrepancy may be elsewhere,
   but I suspect it's here because the only change was in the colors and
   because this had been a trouble spot before, as mentioned above.
   Don't remember if I had any better reason to think so.
 */
static void
op_hwb_color (Intensity *dr, Intensity *dg, Intensity *db,
	      Intensity *ar, Intensity *ag, Intensity *ab)
{
  FOR_EACH (x, y, j)
    {
      double junk;
      Intensity h = fmod (ar[j], 6.0);
      if (h < 0)
	h = 6.0 + h;  /* FIXME: already got rounded in the wrong direction */
      {
	Intensity w = modf (ag[j], &junk);
	Intensity b = modf (ab[j], &junk);
	Intensity v = 1 - b;
	Intensity I = floor (h);
	int i = I;
	Intensity f = h - I;
	if (i & 1) 
	  f = 1 - f;
	{
	  Intensity n = w + f * (v - w);
	  Intensity R, G, B;
	  switch (i)
	    {
	    case 1: R = n, G = v, B = w; break;
	    case 2: R = w, G = v, B = n; break;
	    case 3: R = w, G = n, B = v; break;
	    case 4: R = n, G = w, B = v; break;
	    case 5: R = v, G = w, B = n; break;
	    default:		/* ugh */
	    case 0: R = v, G = n, B = w; break;
	    }
	  dr[j] = R;
	  dg[j] = G;
	  db[j] = B;
	}
      }    
    }
}

/* (x,y) image coordinates of this tile's top-left corner. */
static double left;
static double top;

/* width and height in image space of one pixel of this tile. */
static double x_scale;
static double y_scale;

/* Fill dest with random 1-bit values, on with probability
   proportional to 'a'. */
static void
op_sprinkle (Intensity *dest, Intensity *a)
{
  FOR_EACH (x, y, j)
    dest[j] = (fast_rand () / (double)UINT_MAX < a[j] ? 1.0 : 0.0);
}

/* Nullary operator: dest(x,y) = x. */
static void
op_x (Intensity *dest, Intensity *a, Intensity *b)
{
  FOR_EACH (x, y, j)
    dest[j] = left + x_scale * x;
}

/* Nullary operator: dest(x,y) = y. */
static void
op_y (Intensity *dest, Intensity *a, Intensity *b)
{
  FOR_EACH (x, y, j)
    dest[j] = top + y_scale * y;
}

/* Unary operator: dest(x,y) = expr 
   where expr uses arg1 = a(x,y) */
#define unop(name, exp)                                \
  static void                                          \
  name (Intensity *dest, Intensity *a, Intensity *b)   \
  {                                                    \
    FOR_EACH (x, y, j)                                 \
      {                                                \
	Intensity arg1 = a[j]; dest[j] = exp;          \
      }                                                \
  }

unop (op_abs,   fabs (arg1))
unop (op_atan,  atan (arg1))
unop (op_cos,   cos (arg1))
unop (op_exp,   exp (arg1))
unop (op_floor, floor (arg1))
unop (op_log,   log (fabs (arg1)))
unop (op_neg,   -arg1)
unop (op_sign,  arg1 < 0 ? -1.0 : arg1 == 0 ? 0.0 : 1.0)
unop (op_sin,   sin (arg1))
unop (op_sqrt,  sqrt (fabs (arg1)))
unop (op_tan,   tan (arg1))

/* Binary operator: dest(x,y) = expr 
   where expr uses arg1 = a(x,y) 
               and arg2 = b(x,y) */
#define binop(name, exp)                                       \
  static void                                                  \
  name (Intensity *dest, Intensity *a, Intensity *b)           \
  {                                                            \
    FOR_EACH (x, y, j)                                         \
      {                                                        \
	Intensity arg1 = a[j], arg2 = b[j]; dest[j] = exp;     \
      }                                                        \
  }

/* Convert a floating-point intensity to bits available for bitwise ops. 
   This tends to produce fractally patterns.
   (This is also useful for hashing an intensity.) */
static INLINE unsigned
intensity_to_bits (Intensity x)
{
  return *(tsuint *)&x;	/* unportable */
}

/* Convert bits back into a floating-point intensity. */
static INLINE Intensity
bits_to_intensity (unsigned u)
{
  return *(Intensity *)&u;	/* unportable */
}

binop (op_add, arg1 + arg2)
binop (op_sub, arg1 - arg2)
binop (op_mul, arg1 * arg2)
binop (op_div, arg1 / arg2)
binop (op_average, 0.5 * (arg1 + arg2))
binop (op_hypot, hypot (arg1, arg2))
binop (op_max, arg1 > arg2 ? arg1 : arg2)
binop (op_min, arg1 < arg2 ? arg1 : arg2)
binop (op_mix, (fast_rand () & 1) ? arg1 : arg2)
binop (op_mod, fmod (arg1, arg2))
binop (op_pow, pow (arg1, arg2))
binop (op_and, 
       bits_to_intensity (intensity_to_bits (arg1) & intensity_to_bits (arg2)))
binop (op_or, 
       bits_to_intensity (intensity_to_bits (arg1) | intensity_to_bits (arg2)))
binop (op_xor,
       bits_to_intensity (intensity_to_bits (arg1) ^ intensity_to_bits (arg2)))


/* Result graphs.
   This 'compiled' representation of an evo program is a DAG of op
   nodes. Results are cached on each node, so you evaluate by walking
   the DAG and checking for cached results. I'm not sure why I didn't
   make the caching more persistent, to speed up repeated evaluations
   or evaluations of related programs -- IIRC it took too much memory
   compared to the thumbnail cache. */

typedef enum { 
  end, opc0, opc1, opc2, mix, constant, color, hwb, rotcolor, 
  part1, part2, sprinkle 
} OpType;

typedef void Opcode (Intensity *, Intensity *, Intensity *);

typedef struct Node Node;
struct Node {
  OpType type;
  Opcode *opcode;
  int arity;
  Node *arguments[3];
  Intensity constant_value; /* Used only by the constant op */
  int step;		    /* Program-counter for this operation in
                               the uncompiled program (only used by
                               mix and sprinkle ops) */
  char *name;
  unsigned hashcode;
  Node *next;		    /* The next node in the hashtable bucket. */
  Intensity *result;	    /* The computed intensity field, or NULL
                               if not yet computed. */
};

/* A hashtable with buckets of nodes.  All nodes live here. */
static Node *node_table[node_table_size];

/* Reclaim all nodes from the table. */
static void
free_all_nodes (void)
{
  int i;
  for (i = 0; i < node_table_size; ++i)
    {
      Node *q, *p;
      for (p = node_table[i]; p != NULL; p = q)
	{
	  q = p->next;
	  unallot (p);
	}
      node_table[i] = NULL;
    }
}

/* Make a hash value combining two sub-hashes. */
static INLINE unsigned
combine (unsigned h1, unsigned h2)
{
  /* We rotate h1 to avoid gratuitous symmetry. */
  return ((h1 << 1) | (h1 >> 31)) ^ h2;	/* not really portable */
}

/* Compute a hash value for node. */
static unsigned
node_hash (Node *node)
{
  unsigned h = combine (node->type, (unsigned) node->opcode);
  int i;
  for (i = 0; i < node->arity; ++i)
    h = combine (h, node->arguments[i]->hashcode);
  if (node->type == constant)
    h = combine (h, intensity_to_bits (node->constant_value));
  if (node->type == mix || node->type == sprinkle)
    h = combine (h, node->step);
  return h;
}

/* Return true iff node1 and node2 are structurally equivalent. */
static int
node_equal (Node *node1, Node *node2)
{
  if (node1->hashcode != node2->hashcode) /* quick redundant check */
    return 0;
  return node1->type == node2->type && 
    node1->opcode == node2->opcode &&
    node1->arguments[0] == node2->arguments[0] && /* args are uniquified */
    node1->arguments[1] == node2->arguments[1] &&
    node1->arguments[2] == node2->arguments[2] &&
    (node1->type != constant || 
     node1->constant_value == node2->constant_value) &&
    ((node1->type != mix && node1->type != sprinkle) || 
     node1->step == node2->step);
}

/* Return the unique node equal to 'node'. Add it to the table if not
   in there already. */
static Node *
uniquify (Node *node)
{
  Node **bucket = &node_table[node->hashcode % node_table_size];
  Node *p;
  for (p = *bucket; p != NULL; p = p->next)
    if (node_equal (p, node))
      {
	unallot (node);
	return p;
      }
  node->next = *bucket;
  *bucket = node;
  return node;
}

/* Post: no node has a computed intensity field. */
static void
reset_cache (void)
{
  int i;
  Node *b;
  for (i = 0; i < node_table_size; ++i)
    for (b = node_table[i]; b != NULL; b = b->next)
      b->result = NULL;
}

/* Return the unique node for the given arguments.
   Pre: the arguments make sense (e.g. arity is right for opcode, etc.) */
static Node *
make_node (char *name, OpType type, Opcode opcode, 
	   int step, Intensity constant_value,
	   int arity, Node *arg0, Node *arg1, Node *arg2)
{
  Node *node = allot (sizeof *node);
  node->type = type;
  node->opcode = opcode;
  node->arity = arity;
  node->arguments[0] = arg0;
  node->arguments[1] = arg1;
  node->arguments[2] = arg2;
  node->constant_value = constant_value;
  node->step = step;
  node->name = name;
  node->result = NULL;
  node->hashcode = node_hash (node);
  node->next = NULL;
  return uniquify (node);
}

static int indent = 0;

/* Debugging aid */
static void
node_dump (Node *node)
{
  int i;
  printf ("%*s", indent, "");
  if (node->type == constant)
    printf ("%g %p\n", node->constant_value, node);
  else
    printf ("%s %p\n", node->name, node);
  ++indent;
  for (i = 0; i < node->arity; ++i)
    node_dump (node->arguments[i]);
  --indent;
}

/* The tile heap holds all intensity tiles. There's space for one 
   RGB triple for each of 'program_length' instructions, plus one 
   initial 'zero' tile. */
static Intensity heap[3 * program_length * tile_size + 1];
static Intensity *heap_ptr;

/* Free all currently-allocated intensity tiles. */
static void
reset_heap (void)
{
  heap_ptr = heap;
}

/* Allocate 'blocks' consecutive intensity tiles, starting at the
   current heap_ptr. */
static void
allocate (int blocks)
{
  if (heap_ptr - heap >= sizeof heap / sizeof heap[0])
    die ("bug");
  heap_ptr += blocks * tile_size;
}

/* Return the tile resulting from evaluating 'node' into the tile at 
   coordinate index 'tile_id' (caching it). */ 
static Intensity *
eval (Node *node, int tile_id)
{
  Intensity *result = heap_ptr;
  if (node->result != NULL)
    return node->result;
  switch (node->type)
    {
    case opc0:
      allocate (1);
      node->opcode (result, NULL, NULL);
      break;
    case opc1:
      allocate (1);
      node->opcode (result, eval (node->arguments[0], tile_id), NULL);
      break;
    case opc2:
      allocate (1);
      node->opcode (result, 
		    eval (node->arguments[0], tile_id),
		    eval (node->arguments[1], tile_id));
      break;
    case mix:
      allocate (1);
      {
	Intensity *arg0 = eval (node->arguments[0], tile_id);
	Intensity *arg1 = eval (node->arguments[1], tile_id);
	seed_rand (node->step + 64 * tile_id);
	op_mix (result, arg0, arg1);
      }
      break;
    case constant:
      allocate (1);
      op_constant (node->constant_value, result);
      break;
    case color:
    case rotcolor:
      assert (0);
      break;
    case hwb:
      allocate (3);
      op_hwb_color (result, result + tile_size, result + 2 * tile_size,
		    eval (node->arguments[0], tile_id), 
		    eval (node->arguments[1], tile_id), 
		    eval (node->arguments[2], tile_id));
      break;
    case part1:
      result = eval (node->arguments[0], tile_id) + 1 * tile_size;
      break;
    case part2:
      result = eval (node->arguments[0], tile_id) + 2 * tile_size;
      break;
    case sprinkle:
      allocate (1);
      {
	Intensity *arg0 = eval (node->arguments[0], tile_id);
	seed_rand (node->step + 64 * tile_id);
	op_sprinkle (result, arg0);
      }
      break;
    default: 
      assert (0);
    }
  node->result = result;
  return result;
}

typedef enum { small, big } Coord_system;

enum {
  tile_ids = thumb_rows*thumb_cols + rows*cols
};

/* Return the tile resulting from evaluating 'node' into the tile
   at cs:(col,row). */
static Intensity *
evaluate (Node *node, Coord_system cs, int col, int row)
{
  int tile_id;
  double aspect = (double)thumb_width / thumb_height;
  if (cs == small)
    {
      left    = -aspect + (2*aspect/thumb_cols) * col;
      top     = -1.0 + (2.0/thumb_rows) * row;
      x_scale = 2*aspect/thumb_width;
      y_scale = 2.0/thumb_height;
      tile_id = row * thumb_cols + col;
    }
  else if (cs == big)
    {
      left    = -aspect + (2*aspect/(cols*thumb_cols)) * col;
      top     = -1.0 + (2.0/(rows*thumb_rows)) * row;
      x_scale = 2*aspect/(tile_width*cols*thumb_cols);
      y_scale = 2.0/(tile_height*rows*thumb_rows);
      tile_id = thumb_rows*thumb_cols + row * cols*thumb_cols + col;
    }
  else
    assert (0);

  return eval (node, tile_id);
}


/* Analyzer */

/* Return true iff 'node' is in the array 'seen' of current length
   'num_seen', appending it if it's not. */
static int
adjoin (Node *node, Node **seen, int *num_seen)
{
  int i = *num_seen;
  for (; 0 < i; --i)
    if (node == seen[i-1])
      return 1;

  seen[(*num_seen)++] = node;
  return 0;
}

/* Return the number of nodes reachable from 'node' that aren't
   in the array 'seen' of current length 'num_seen', appending any
   newly-reached nodes to that array. 
   Pre: if a node n is already in the seen array, so are its reachable
        nodes, unless they're pending in the current recursion... 
        geez, this specification is more complicated than the code. */
static int
count_unvisited_nodes (Node *node, Node **seen, int *num_seen)
{
  if (adjoin (node, seen, num_seen))
    return 0;
  {
    int count = 1;
    int i;
    /* TODO: don't count part1/part2 nodes? */
    for (i = 0; i < node->arity; ++i)
      count += count_unvisited_nodes (node->arguments[i], seen, num_seen);
    return count;
  }
}

/* Return the number of nodes in the graph reachable from {r,g,b}.
   Pre: graph is no bigger than the biggest possible graph compiled
        from program_length instructions. */
static int
count_reachable_nodes (Node *r, Node *g, Node *b)
{
  Node *seen[5 * program_length];
  int num_seen = 0;
  int count = 0;
  count += count_unvisited_nodes (r, seen, &num_seen);
  count += count_unvisited_nodes (g, seen, &num_seen);
  count += count_unvisited_nodes (b, seen, &num_seen);
  return count;
}


/* Compiling a program (a sequence of stack ops) into a node graph.
   This can produce a dag because the stack is circular and of bounded
   depth. */

typedef struct Instruc Instruc;
struct Instruc {
  int frequency;
  OpType type;
  Opcode *opcode;
  int pops, pushes;
  char *name;
  Intensity constant_value;
};

/* The symbolic stack represents the state produced by executing a sequence
   of instructions, as a node graph with a node for each RGB component at 
   each possible stack slot. You produce an image by evaluating the
   nodes for the top-of-stack. */
static int stack_ptr = 0;

static Node *r_stack[stack_limit];
static Node *g_stack[stack_limit];
static Node *b_stack[stack_limit];

/* Initialize the symbolic stack. */
static void
clear_stack (void)
{
  int i;
  Node *zero = make_node ("0", constant, NULL, 0, 0.0, 0, NULL, NULL, NULL);
  stack_ptr = 0;
  for (i = 0; i < stack_limit; ++i)
    {
      r_stack[i] = zero;
      g_stack[i] = zero;
      b_stack[i] = zero;
    }
}

/* Return the stack index 'increment' steps from 'ptr', with wraparound.
   Pre: abs(increment) <= stack_limit 
   FIXME dump the precondition and rewrite for clarity instead of speed */
static INLINE int
bump (int ptr, int increment)
{
  ptr += increment;
  if (increment < 0)
    {
      if (ptr < 0) ptr += stack_limit;
    }
  else 
    {
      if (stack_limit <= ptr) ptr -= stack_limit;
    }
  return ptr;
}

/* Symbolically evaluate the execution of *p as if as the given step
   number in a program, with the given arguments (top of stack, next on 
   stack, third on stack). Each stack-location argument is an RGB triple
   of pointers to node pointers. Symbolic evaluation may create new result
   nodes, mutate some of the RGB triples to point to them, and update the 
   stack pointer. */
static void
really_pretend (Instruc *p, 
		Node ***tos, Node ***nos, Node ***pos,
		int step)
{
  char *name = p->name;
  Opcode *code = p->opcode;
  Node *r0, *r1, *r2;
  switch (p->type)
    {
    case opc0:
      r0 = r1 = r2 = 
	make_node (name, opc0, code, step, 0, 0, NULL, NULL, NULL);
      break;
    case opc1:
      r0 = make_node (name, opc1, code, step, 0, 1, *tos[0], NULL, NULL);
      r1 = make_node (name, opc1, code, step, 0, 1, *tos[1], NULL, NULL);
      r2 = make_node (name, opc1, code, step, 0, 1, *tos[2], NULL, NULL);
      break;
    case opc2:
      r0 = make_node (name, opc2, code, step, 0, 2, *tos[0], *nos[0], NULL);
      r1 = make_node (name, opc2, code, step, 0, 2, *tos[1], *nos[1], NULL);
      r2 = make_node (name, opc2, code, step, 0, 2, *tos[2], *nos[2], NULL);
      break;
    case mix:
      r0 = make_node (name, mix, NULL, step, 0, 2, *tos[0], *nos[0], NULL);
      r1 = make_node (name, mix, NULL, step, 0, 2, *tos[1], *nos[1], NULL);
      r2 = make_node (name, mix, NULL, step, 0, 2, *tos[2], *nos[2], NULL);
      break;
    case constant:
      r0 = r1 = r2 = 
	make_node (name, constant, NULL, step, p->constant_value, 
		   0, NULL, NULL, NULL);
      break;
    case color:
      r0 = *tos[0];
      r1 = *nos[1];
      r2 = *pos[2];
      break;
    case hwb:
      r0 = make_node (name, hwb, NULL, step, 0, 3, *tos[0], *tos[1], *tos[2]);
      r1 = make_node ("part1", part1, NULL, step, 0, 1, r0, NULL, NULL);
      r2 = make_node ("part2", part2, NULL, step, 0, 1, r0, NULL, NULL);
      break;
    case rotcolor:
      r0 = *tos[1];
      r1 = *tos[2];
      r2 = *tos[0];
      break;
    case sprinkle:
      r0 = r1 = r2 = 
	make_node (name, sprinkle, NULL, step, 0, 1, *tos[0], NULL, NULL);
      break;
    default: 
      assert (0);
    }
  *tos[0] = r0;
  *tos[1] = r1;
  *tos[2] = r2;
}

/* Symbolically evaluate the execution of *p as if as the given step
   number in a program. */
static void
pretend (Instruc *p, int step)
{
  stack_ptr = bump (stack_ptr, -p->pops);
  {
    Node **tos[] = { 
      &r_stack[stack_ptr], 
      &g_stack[stack_ptr], 
      &b_stack[stack_ptr]
    };
    Node **nos[] = { 
      &r_stack[bump (stack_ptr, 1)],
      &g_stack[bump (stack_ptr, 1)],
      &b_stack[bump (stack_ptr, 1)]
    };
    Node **pos[] = { 
      &r_stack[bump (stack_ptr, 2)],
      &g_stack[bump (stack_ptr, 2)],
      &b_stack[bump (stack_ptr, 2)]
    };
    really_pretend (p, tos, nos, pos, step);
  }
  stack_ptr = bump (stack_ptr, p->pushes);
}

/* Symbolically evaluate 'program', leaving a graph representation of
   it in the symbolic stack. */
static void
compile (Instruc *program)
{
  int i;
  clear_stack ();
  for (i = 0; program[i].type != end; ++i)
    pretend (&program[i], i);
  assert (i < program_length);
  stack_ptr = bump (stack_ptr, -1);
}


/* Building and mutating genomes */

/* Make a constant instruction. */
static Instruc
make_constant (Intensity value)
{
  Instruc result = 
    { 1, constant, NULL, 0, 1, "constant", value };
  return result;
}

/* One of each possible instruction type. */
static Instruc toolbox[] = {
  { 1, constant, NULL,         0, 1, "constant"}, 
  { 1, opc0,     op_x,         0, 1, "x"},
  { 1, opc0,     op_y,         0, 1, "y"},
  { 1, sprinkle, NULL,         0, 1, "sprinkle"}, /* FIXME: should be 1,1 */
  /* Ugh, fixing this would change images we've already saved. So I
     guess we should grandfather this in, but add another instruction
     with a different name and a correct pop/push value. And give 
     it a random frequency of 0, presumably. Alternatively, remove 
     the sprinkle op and convert everything in the `photo album' to 
     use the mix op in its place. Is that feasible? */

  { 1, opc1,     op_abs,       1, 1, "abs"},
  { 1, opc1,     op_atan,      1, 1, "atan"},
  { 1, opc1,     op_cos,       1, 1, "cos"},
  { 1, opc1,     op_exp,       1, 1, "exp"},
  { 1, opc1,     op_floor,     1, 1, "floor"},
  { 1, opc1,     op_log,       1, 1, "log"},
  { 1, opc1,     op_neg,       1, 1, "neg"},
  { 1, opc1,     op_sign,      1, 1, "sign"},
  { 1, opc1,     op_sin,       1, 1, "sin"},
  { 1, opc1,     op_sqrt,      1, 1, "sqrt"},
  { 1, opc1,     op_tan,       1, 1, "tan"},

  { 1, hwb,      NULL,         1, 1, "hwb"},

  { 1, opc2,     op_add,       2, 1, "+"},
  { 1, opc2,     op_sub,       2, 1, "-"},
  { 1, opc2,     op_mul,       2, 1, "*"},
  { 1, opc2,     op_div,       2, 1, "/"},
  { 1, opc2,     op_average,   2, 1, "average"},
  { 1, opc2,     op_hypot,     2, 1, "hypot"},
  { 1, opc2,     op_max,       2, 1, "max"},
  { 1, opc2,     op_min,       2, 1, "min"},
  { 1, mix,      NULL,         2, 1, "mix"},
  { 1, opc2,     op_mod,       2, 1, "mod"},
  { 1, opc2,     op_pow,       2, 1, "pow"},
  { 1, opc2,     op_and,       2, 1, "and"},
  { 1, opc2,     op_or,        2, 1, "or"},
  { 1, opc2,     op_xor,       2, 1, "xor"},

  { 1, color,    NULL,         3, 1, "color"},
  { 1, rotcolor, NULL,         1, 1, "rotcolor"},
};

/* Return the total of all instruction-type frequencies. 
   Maybe I should call them weights. */
static int
frequency_sum (void)
{
  int i = 0, sum = 0;
  for (; i < sizeof toolbox / sizeof toolbox[0]; ++i)
    sum += toolbox[i].frequency;
  return sum;
}

/* Return a random instruction type, weighted by instruction frequency. */
static Instruc
weighted_random_instruc (void)
{
  int k = choose (frequency_sum ());
  int i = 0, sum = 0;
  for (;; ++i)
    {
      sum += toolbox[i].frequency;
      if (k <= sum)
	{
	  assert (0 <= i && i < sizeof toolbox / sizeof toolbox[0]);
	  return toolbox[i];
	}
    }
}

/* Return a random instruction, of type weighted by instruction frequency. */
static Instruc
random_instruc (void)
{
  Instruc result = weighted_random_instruc ();
  if (result.type == constant)
    result.constant_value = choose_double ();
  return result;
}

/* Set 'pgm' to a program with 'length' random instructions. */
static void
randomize (Instruc *pgm, int length)
{
  int i;
  for (i = 0; i < length-1; ++i)
    pgm[i] = random_instruc ();
  pgm[length-1].type = end;
}

/* Make a random change to one instruction. */
static void
point_mutation (Instruc *ins)
{
  if (ins->type == constant && choose (100) < 50)
    ins->constant_value += (choose_double () - 0.5) / 10.0;
  else
    *ins = random_instruc ();
}

/* Randomly change 0 or more of pgm's instructions. */
static void
mutate (Instruc *pgm, int length)
{
  int i;
  for (i = 0; i < length-1; ++i)
    if (choose (100) < mutation_rate)
      point_mutation (&pgm[i]);
}


/* Genotype I/O */

/* Write one instruction to 'out'. */
static void
write_instruc (FILE *out, Instruc p)
{
  if (p.type == constant)
    fprintf (out, " %g", p.constant_value);
  else
    fprintf (out, " %s", p.name);
}

/* Read one instruction from 'in'. Die on any unexpected input. */
static Instruc
read_instruc (FILE *in)
{
  int i;
  char name[80];
  read_token (in, name, sizeof name);
  for (i = 0; i < sizeof toolbox / sizeof toolbox[0]; ++i)
    if (0 == strcmp (name, toolbox[i].name))
      return toolbox[i];
  return make_constant (parse_number (name));
}

/* Write 'pgm' to 'out'. It must have exactly 'length' instructions. */
static void
write_program (FILE *out, Instruc *pgm, int length)
{
  int i;
  fprintf (out, "%d", length-1);
  for (i = 0; i < length-1; ++i)
    write_instruc (out, pgm[i]);
  fprintf (out, "\n");
}

/* Read 'pgm' from 'in'. It must have exactly 'length' instructions.
   FIXME make this more flexible */
static void
read_program (FILE *in, Instruc *pgm, int length)
{
  int i, in_length;
  if (1 != fscanf (in, "%d", &in_length))
    {
      if (feof (in))
	return;
      die ("Bad data in evo-state: %s", strerror (errno));
    }
  if (in_length != length-1)
    die ("Incompatible saved data");
  for (i = 0; i < length-1; ++i)
    pgm[i] = read_instruc (in);
  pgm[length-1].type = end;
}


/* Thumbnail cache */

static Pixel thumbnail_cache[grid_size];
static int cache_valid[cols][rows];

static void
copy_grid_square (Uint32 *dest, const Uint32 *src, int col, int row)
{
  int x0 = col * thumb_width;
  int y0 = row * thumb_height;
  int x, y;
  for (y = y0; y < y0 + thumb_height; ++y)
    for (x = x0; x < x0 + thumb_width; ++x)
      {
	int j = y * grid_width + x;
	dest[j] = src[j];
      }
}

static void
copy_to_grid (int col, int row)
{
  copy_grid_square (grid, thumbnail_cache, col, row);
}

static void
update_cache (int col, int row)
{
  copy_grid_square (thumbnail_cache, grid, col, row);
  cache_valid[col][row] = 1;
}

static void
invalidate_cache (int col, int row)
{
  cache_valid[col][row] = 0;
}


/* Top-level gene stuff */

/* The instructions for each program. 
   FIXME give a name to this concept of a visible choice */
static Instruc programs[cols][rows][program_length];

/* Require col and row to be in range. */
static void
check_coords (int col, int row)
{
  if (col < 0 || cols <= col)
    die ("Bad column: %d\n", col);
  if (row < 0 || rows <= row)
    die ("Bad row: %d\n", row);
}

/* Fill program (col, row) with a new random program. */
static void
populate (int col, int row)
{
  check_coords (col, row);
  randomize (programs[col][row], program_length);
  invalidate_cache (col, row);
}

/* Mutate program (col, row) randomly. */
static void
sample (int col, int row)
{
  check_coords (col, row);
  mutate (programs[col][row], program_length);
  invalidate_cache (col, row);
}

/* Copy program (col2, row2) into (col1, row1). */
static void
copy (int col1, int row1, int col2, int row2)
{
  check_coords (col1, row1);
  check_coords (col2, row2);
  memcpy (programs[col1][row1], 
	  programs[col2][row2], 
	  sizeof programs[col1][row1]);
  invalidate_cache (col1, row1);
}

/* Generate image tile (grid_col, grid_row) for 'program' sector
   cs:(col,row). [or something. FIXME document this properly] */
static void
generate_grid (Instruc *program, Coord_system cs, int col, int row,
	       int grid_col, int grid_row)
{
  free_all_nodes ();
  compile (program);
  reset_cache ();
  reset_heap ();
  {
    Intensity *tos[] = { 
      evaluate (r_stack[stack_ptr], cs, col, row),
      evaluate (g_stack[stack_ptr], cs, col, row),
      evaluate (b_stack[stack_ptr], cs, col, row)
    };
    gridify (tos, grid_col * tile_width, grid_row * tile_height);
  }
}

/* Generate the thumbnail image for program (col, row). */
static void
generate (int col, int row)
{
  int i, j;
  check_coords (col, row);
  if (cache_valid[col][row])
    {
      copy_to_grid (col, row);
      return;
    }
  invalidate_cache (col, row);
  for (i = 0; i < thumb_cols; ++i)
    for (j = 0; j < thumb_rows; ++j)
      {
	int c = col * thumb_cols + i;
	int r = row * thumb_rows + j;
	generate_grid (programs[col][row], small, i, j, c, r);
      }
  update_cache (col, row);
}

/* Generate the sector at (col, row) of the full image for
   program (pcol, prow). */
static void
generate_big (int pcol, int prow, int col, int row)
{
  int i, j;
  check_coords (col, row);
  for (i = 0; i < thumb_cols; ++i)
    for (j = 0; j < thumb_rows; ++j)
      {
	int c = col * thumb_cols + i;
	int r = row * thumb_rows + j;
	generate_grid (programs[pcol][prow], big, c, r, c, r);
      }
}

/* Return a measure of the complexity of the program at (col,row):
   the size of the graph implementing it, after optimization. */
static int
complexity (int col, int row)
{
  check_coords (col, row);
  compile (programs[col][row]);
  return count_reachable_nodes (r_stack[stack_ptr], 
				g_stack[stack_ptr], 
				b_stack[stack_ptr]);
}

/* Return true iff grid images g and h are identical. */
static int
same_thumbs (int gc, int gr, int hc, int hr)
{
  check_coords (gc, gr);
  check_coords (hc, hr);
  {
    int gx0 = gc * thumb_width;
    int gy0 = gr * thumb_height;

    int hx0 = hc * thumb_width;
    int hy0 = hr * thumb_height;

    int x, y;
    for (y = 0; y != thumb_height; ++y)
      for (x = 0; x != thumb_width; ++x)
	if (get (gx0 + x, gy0 + y) != get (hx0 + x, hy0 + y))
	  return 0;
    return -1;
  }
}


/* Other top-level commands */

/* Write every program to 'out'. 
   (For some reason I picked column-major order.) */
static void
write_state (FILE *out)
{
  int i, j;
  for (j = 0; j < rows; ++j)
    for (i = 0; i < cols; ++i)
      write_program (out, programs[i][j], program_length);
}

/* Read every program from 'in'. */
static void
read_state (FILE *in)
{
  int i, j;
  for (j = 0; j < rows; ++j)
    for (i = 0; i < cols; ++i)
      {
	read_program (in, programs[i][j], program_length);
	invalidate_cache (i, j);
      }
}

static void
read_random (FILE *in, int lines)
{
  int n = rows * cols;		/* number of lines still to pick */
  for (; 0 < n; --lines)
    {
      char line[256];
      if (choose (lines) < n)
	{
	  int i = (rows * cols - n) % cols;
	  int j = (rows * cols - n) / cols;
	  read_program (in, programs[i][j], program_length);
	  invalidate_cache (i, j);
	  --n;
	}
      else if (fgets (line, sizeof line, in) == NULL)
	die ("evo-saved: %s", strerror (errno));
    }
}

/* Append the state to file evo-state.
   FIXME code duplication */
static void
append (void)
{
  FILE *out = fopen ("evo-saved", "a");
  if (out == NULL)
    fprintf (stderr, "evo-saved: %s\n", strerror (errno));
  else
    {
      write_state (out);
      fclose (out);
      printf ("Appended to evo-saved\n");
    }
}

static void
append1 (void)
{
  FILE *out = fopen ("evo-saved", "a");
  if (out == NULL)
    fprintf (stderr, "evo-saved: %s\n", strerror (errno));
  else
    {
      write_program (out, programs[0][0], program_length);
      fclose (out);
      printf ("Appended 1 to evo-saved\n");
    }
}

/* Write the state to file evo-state. */
static void
save (void)
{
  FILE *out = fopen ("evo-state", "w");
  if (out == NULL)
    fprintf (stderr, "evo-state: %s\n", strerror (errno));
  else
    {
      write_state (out);
      fclose (out);
      printf ("Saved as evo-state\n");
    }
}

/* Read the state in from file evo-state. */
static void
restore (void)
{
  FILE *in = fopen ("evo-state", "r");
  if (in == NULL)
    fprintf (stderr, "evo-state: %s\n", strerror (errno));
  else
    {
      read_state (in);
      fclose (in);
    }
}

static int
count_lines (FILE *f)
{
  char line[256];
  int n = 0;
  while (fgets (line, sizeof line, f) != NULL)
    if (strchr (line, '\n') != NULL)
      ++n;
  if (ferror (f))
    die ("evo-saved: %s", strerror (errno));
  return n;
}

static void
load_random (void)
{
  FILE *in = fopen ("evo-saved", "r");
  if (in == NULL)
    fprintf (stderr, "evo-state: %s\n", strerror (errno));
  else
    {
      int lines = count_lines (in);
      fseek (in, 0, SEEK_SET);
      read_random (in, lines);
      fclose (in);
    }
}

static FILE *
open_file (const char *filename, const char *mode)
{
  FILE *file = fopen (filename, mode);
  if (file == NULL)
    die ("%s: %s", filename, strerror (errno));
  return file;
}

/* Write the grid to out as a PPM file. */
static void
output_picture (FILE *out)
{
  fprintf (out, "P6\n");
  fprintf (out, "# Generated by evo\n");

  fprintf (out, "# ");
  write_program (out, programs[0][0], program_length);

  fprintf (out, "%d %d 255\n", grid_width, grid_height);
  {
    int i, j;
    for (i = 0; i < grid_height; ++i)
      {
	Uint8 buffer[3*grid_width];
	for (j = 0; j < grid_width; ++j) 
	  {
	    Uint32 pixel = grid[i * grid_width + j];
	    Uint8 r = 0xFF & (pixel >> 16);
	    Uint8 g = 0xFF & (pixel >>  8);
	    Uint8 b = 0xFF & (pixel >>  0);
	    buffer[3*j+0] = r;
	    buffer[3*j+1] = g;
	    buffer[3*j+2] = b;
	  }
	if (1 != fwrite (buffer, sizeof buffer, 1, out))
	  {
	    printf ("Error writing image: %s\n", strerror (errno));
	    return;
	  }
      }
  }
}

/* Save the grid to file evoNN.ppm for the next available NN. */
static void
save_image (void)
{
  char filename[PATH_MAX];
  FILE *f = open_save_file (filename, "evo%d.ppm", "wb");
  if (f == NULL)
    fprintf (stderr, "Couldn't open image file: %s\n", strerror (errno));
  else
    {
      output_picture (f);
      fclose (f);
      printf ("Image written to %s\n", filename);
    }
}

/* Regenerate the image program from file regress-state, 
   writing the image to regress-out. */
static void
regress (void)
{
  start_sdl (32);
  {
    FILE *in = open_file ("regress-state", "r");
    read_state (in);
    fclose (in);
  }
  {
    int i, j;
    for (j = 0; j < rows; ++j)
      for (i = 0; i < cols; ++i)
	generate (i, j);
  }
  {
    FILE *out = open_file ("regress-out", "wb");
    output_picture (out);
  }
}

static void
no_sdl (int bits_per_pixel)
{
  grid = NULL;
  grid8 = NULL;
  if (32 == bits_per_pixel)
    grid = (Uint32 *) allot (grid_size * sizeof grid[0]);
  else if (8 == bits_per_pixel)
    grid8 = (Uint8 *) allot (grid_size * sizeof grid8[0]);
}


/* Main program */

/* Tusl word to run a read-eval-print loop. */
// XXX use 'repl'
static void
command_loop (ts_VM *vm, ts_Word *pw)
{
  ts_load_interactive (vm, stdin);
}

/* Expose this C module to Tusl. */
void
install_evo_words (ts_VM *vm)
{
  int i;
  /* Create a variable for each instruction frequency. */
  for (i = 0; i < sizeof toolbox / sizeof toolbox[0]; ++i)
    {
      char *name = allot (strlen (toolbox[i].name) + 2);
      name[0] = '&';
      strcpy (name+1, toolbox[i].name);
      ts_install (vm, name, ts_do_push, (tsint) &toolbox[i].frequency);
    }

  ts_install (vm, "thumb-width",     ts_do_push, thumb_width);
  ts_install (vm, "thumb-height",    ts_do_push, thumb_height);
  ts_install (vm, "cols",            ts_do_push, cols);
  ts_install (vm, "rows",            ts_do_push, rows);

  ts_install (vm, "command-loop",    command_loop, 0);

  ts_install (vm, "populate",        ts_run_void_2, (tsint) populate);
  ts_install (vm, "mutate",          ts_run_void_2, (tsint) sample);
  ts_install (vm, "copy",            ts_run_void_4, (tsint) copy);
  ts_install (vm, "generate",        ts_run_void_2, (tsint) generate);
  ts_install (vm, "generate-big",    ts_run_void_4, (tsint) generate_big);
  ts_install (vm, "complexity",      ts_run_int_2,  (tsint) complexity);
  ts_install (vm, "same-thumbs?",    ts_run_int_4,  (tsint) same_thumbs);

  ts_install (vm, "save-image",      ts_run_void_0, (tsint) save_image);
  ts_install (vm, "append",          ts_run_void_0, (tsint) append);
  ts_install (vm, "append1",         ts_run_void_0, (tsint) append1);
  ts_install (vm, "save",            ts_run_void_0, (tsint) save);
  ts_install (vm, "restore",         ts_run_void_0, (tsint) restore);
  ts_install (vm, "load-random",     ts_run_void_0, (tsint) load_random);

  ts_install (vm, "regress",         ts_run_void_0, (tsint) regress);
  ts_install (vm, "no-sdl",          ts_run_void_1, (tsint) no_sdl);
}
