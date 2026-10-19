#ifndef NH_TESTLEVEL_H
#define NH_TESTLEVEL_H

#include "core/nh_ai.h"
#include "core/nh_level.h"

/* Hand-built greybox test building used for lighting and AI work. */
void testlevel_build(Level *L);
NhVec3 testlevel_start(float *yaw);
void testlevel_guards(AiWorld *ai);

#endif
