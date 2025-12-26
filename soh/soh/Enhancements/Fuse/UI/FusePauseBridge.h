#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

void FusePause_DrawPrompt(struct PlayState* play);
void FusePause_OnSwordFusePressed();

#ifdef __cplusplus
}
#endif

