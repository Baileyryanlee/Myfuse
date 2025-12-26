#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
union Gfx;

void FusePause_DrawPrompt(struct PlayState* play, union Gfx** polyOpaDisp, union Gfx** polyXluDisp);

#ifdef __cplusplus
}
#endif

