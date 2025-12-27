#include <TouchHelperA.h>
void CenterText(int X,int Y,ImVec4 color,const char* content ){
	auto textSize = ImGui::CalcTextSize(content, 0, 0, 30.0f);
	
    
    ImGui::GetBackgroundDrawList()->AddText(NULL, 30.0f,ImVec2(X - textSize.x / 2, Y), ImGui::ColorConvertFloat4ToU32(color),content);

	    	
}