#include "tusdl.h"
#include "rand.h"

extern randctx ctx;

extern void seed_rand (int seed);

static INLINE unsigned
fast_rand (void)
{
  return RAND (&ctx);
}
