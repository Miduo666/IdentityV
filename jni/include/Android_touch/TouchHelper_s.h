#ifndef TOUCHHELPER_S_H
#define TOUCHHELPER_S_H

#include "imgui.h"

extern void Touch_Init(const ImVec2 &s);
extern void Touch_setOrientation(int orientation_);
extern void Touch_Down(int id, int x, int y);
extern void Touch_Move(int id, int x, int y);
extern void Touch_Up(int id);

	
#endif
