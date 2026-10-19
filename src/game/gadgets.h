/* Night Heist - gadgets: selection (D-pad), use (A), thrown projectiles and
 * their effects, plus rendering of projectiles, smoke and laser beams. */
#ifndef NH_GADGETS_H
#define NH_GADGETS_H

#include "heist.h"

/* Returns true if A was consumed by a gadget. */
bool gadgets_update(Heist *h, const NhInput *in, bool a_free);
void gadgets_render(Heist *h, const NhMat34 *view);

#endif
