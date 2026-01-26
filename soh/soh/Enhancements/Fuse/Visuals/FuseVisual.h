#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "z64.h"
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace FuseVisual {
void DrawLeftHandAttachments(PlayState* play, Player* player);
void DrawShieldAttachments(PlayState* play, Player* player);
} // namespace FuseVisual

extern "C" {
#endif

void FuseVisual_DrawLeftHandAttachments(PlayState* play, Player* player);
void FuseVisual_DrawShieldAttachments(PlayState* play, Player* player);

#ifdef __cplusplus
}
#endif
