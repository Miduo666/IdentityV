#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <fstream>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <iostream>
#include <fstream>
#include <ctime>

using namespace std;
#include "draw.h"            // 绘制套
#include "AndroidImgui.h"      // 创建绘制套
#include "GraphicsManager.h"   // 获取当前渲染模式
#include "Android_draw/timer.h"
timer DrawFPS;
float fps=60;
long int value1, value2, value3;

void daemonize() {
    pid_t pid = fork();  // 创建子进程
    if (pid < 0) {
        exit(1);  // fork 失败，退出程序
    }
    if (pid > 0) {
        exit(0);  // 父进程退出，子进程继续
    }
    
    if (setsid() < 0) {  // 创建新会话
        exit(1);  // 失败退出
    }

    // 设置新的工作目录（一般设置为根目录以避免占用终端）
    if (chdir("/") < 0) {
        exit(1);
    }

    // 关闭不再使用的文件描述符
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // 重定向标准输入输出到日志文件（可选）
    open("/dev/null", O_RDWR);  // stdin
    open("/dev/null", O_RDWR);  // stdout
    open("/dev/null", O_RDWR);  // stderr
}

int main(int argc, char *argv[]) {
    daemonize();  // 将程序转为守护进程
    
    value1 = 970061201;
    value2 = 16384;
    value3 = 257;

    ::graphics = GraphicsManager::getGraphicsInterface(GraphicsManager::VULKAN);//绘图方式

    // 获取屏幕信息    
    ::screen_config(); 

    ::native_window_screen_x = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::native_window_screen_y = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::abs_ScreenX = (::displayInfo.height > ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);
    ::abs_ScreenY = (::displayInfo.height < ::displayInfo.width ? ::displayInfo.height : ::displayInfo.width);

    // GetPKG();
    
    ::window = android::ANativeWindowCreator::Create("逆天改命", native_window_screen_x, native_window_screen_y, permeate_record);
    graphics->Init_Render(::window, native_window_screen_x, native_window_screen_y);
    
    Touch::Init({(float)::abs_ScreenX, (float)::abs_ScreenY}, true);
    Touch::setOrientation(displayInfo.orientation);
    
    new std::thread(read_thread, value1, value2, value3);
    
    DrawFPS.SetFps(fps);
    DrawFPS.AotuFPS_init();
    DrawFPS.setAffinity();
    
    ::init_My_drawdata();  // 初始化绘制数据
    
    static bool flag = true;
    while (flag) {
        drawBegin();
        graphics->NewFrame();        
        Layout_tick_UI(&flag);
        graphics->EndFrame();
        DrawFPS.SetFps(fps);
        DrawFPS.AotuFPS();
    }
    
    graphics->Shutdown();
    android::ANativeWindowCreator::Destroy(::window);
    return 0;
}


//加入错误日志防止闪退
/*我修改了VkPresentModeKHR present_modes[] = {
    VK_PRESENT_MODE_MAILBOX_KHR,   // 首选：低延迟，无撕裂
    VK_PRESENT_MODE_FIFO_KHR,      // 备选：最稳定，兼容性最好
    VK_PRESENT_MODE_IMMEDIATE_KHR  // 备选：最低延迟，但可能出现撕裂
};*/
