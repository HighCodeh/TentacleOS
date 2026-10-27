#include <stdint.h>
#if UINTPTR_MAX == UINT32_MAX
#include "mquickjs_atom_32.h"
#else
#include "mquickjs_atom_64.h"
#endif
