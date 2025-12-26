#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
union Gfx;

void FusePause_DrawPrompt(struct PlayState* play, union Gfx** polyOpaDisp, union Gfx** polyXluDisp);
void FusePause_DrawModal(struct PlayState* play, union Gfx** polyOpaDisp, union Gfx** polyXluDisp);
void FusePause_UpdateModal(struct PlayState* play);
bool FusePause_IsModalOpen(void);

#ifdef __cplusplus
}
#endif

