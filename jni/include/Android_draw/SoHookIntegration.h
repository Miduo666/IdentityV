#ifndef SO_HOOK_INTEGRATION_H
#define SO_HOOK_INTEGRATION_H

#include <cstdint>

struct ImDrawList;

namespace SoHook {

void StartListeners();
void StopListeners();
bool IsCopycatDrawingActive();
void Update(int targetPid);
void RenderPanel(int targetPid);
void DrawOverlay(ImDrawList *drawList, const float viewProjection[16], float centerX, float centerY,
                 int kernelPlayerCount, float playerX, float playerY, float playerZ, float unitsPerMeter);

}

#endif
