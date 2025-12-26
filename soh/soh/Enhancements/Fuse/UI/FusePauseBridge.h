#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Gfx;

void FusePause_DrawPrompt(struct PlayState* play, struct Gfx** polyOpaDisp);

#ifdef __cplusplus
}
#endif

