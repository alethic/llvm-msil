/* Shim of <assert.h> for the exit-corpus programs (both builds; -isystem
   ../shim): assert compiles to nothing, as with NDEBUG. */
#ifndef SHIM_ASSERT_H
#define SHIM_ASSERT_H
#define assert(x) ((void)0)
#endif
