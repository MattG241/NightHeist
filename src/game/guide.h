/* Night Heist - telling the player what to do next: the goal line, a
 * marker that follows the walking route to it (the target, then the van),
 * the job briefing and one-time tips the first time something comes up. */
#ifndef NH_GUIDE_H
#define NH_GUIDE_H

#include "heist.h"

typedef enum GuideTip {
    TIP_MARKER, TIP_GUARD, TIP_LOCK, TIP_NOTICED, TIP_CAMERA, TIP_GOT_IT, TIP_LIGHT, TIP_HIDE, TIP_COUNT
} GuideTip;

/* What the current player should do now, e.g. "Steal the hard drive". */
const char *guide_goal(const Heist *h, char *buf, int len);
/* Where that is (a world position); false if nowhere in particular. */
bool guide_target(const Heist *h, NhVec3 *pos);
/* Per player, once per tick: the route, the start-zone flag and tips. */
void guide_tick(Heist *h);
/* Show a tip unless it has been seen before. */
void guide_tip(Heist *h, GuideTip tip);
/* HUD: goal line, route marker (or an arrow at the screen edge) and tip. */
void guide_draw(const Heist *h, const NhMat34 *view, float aspect);
/* The briefing card before a campaign job. */
void guide_draw_briefing(const Heist *h, const GameSettings *gs);

#endif
