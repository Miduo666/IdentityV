#include "draw.h"
#include <thread>
#include <cstdint>
#include <stdio.h>
#include <sched.h>
#include <climits>
#include <unistd.h>
#include <sys/syscall.h>
#include "My_font/zh_Font.h"
#include "My_font/fontawesome-brands.h"
#include "My_font/fontawesome-regular.h"
#include "My_font/fontawesome-solid.h"
#include "My_font/gui_icon.h"
#include "kerneldriver-qxqd.hpp"
#include "DrawTool.h"
#include "Name.h"
#include "Offsets.h"
#include <sstream>
#include <iomanip>
#include <atomic>

// ============== 获取内存占用 ==============
static size_t get_memory_usage_kb() {
    FILE* file = fopen("/proc/self/statm", "r");
    if (!file) return 0;
    
    size_t size = 0;  // 总虚拟内存页数
    size_t resident = 0;  // 驻留内存页数 (实际物理内存)
    fscanf(file, "%zu %zu", &size, &resident);
    fclose(file);
    
    // 页大小通常是4KB
    return resident * 4;  // 返回KB
}

// ============== 优化：过滤关键词表 ==============
static const char* g_filter_keywords[] = {
    "creature",
    "dm65_survivor_girl_page",
    "skill_hudie",
    "h55_joseph_camera",
    "burke_console",
    "redqueen_e_heijin_yizi",
    "qiutu_box",
    "weapon",
    "nvyao"
};
static constexpr int g_filter_count = sizeof(g_filter_keywords) / sizeof(g_filter_keywords[0]);

// 快速检查是否需要过滤（包含任意关键词则返回true）
inline bool should_filter(const std::string& name) {
    for (int i = 0; i < g_filter_count; i++) {
        if (name.find(g_filter_keywords[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 快速检查是否包含指定字符串
inline bool contains(const std::string& str, const char* pattern) {
    return str.find(pattern) != std::string::npos;
}

std::string 过滤类名,类名;
char gwd1[64];
char gwd2[64];
float 距离比例=11.886;
float 红夫人X, 红夫人Y, 红夫人Z;
float 红夫人镜像X, 红夫人镜像Y, 红夫人镜像Z;
typedef struct DataStruct{
    long int obj = 0;
    char str[256] = {0}; // 初始化为全零
    int 阵营 = 1;
    char 类名[256] = {0};
    long int objcoor = 0;
}DataStruct;

// 三缓冲结构 - 完全无锁方案
struct GameDataBuffer {
    DataStruct data[2000];
    int 数量 = 0;
};
GameDataBuffer g_buffer[3];                    // 三缓冲区
std::atomic<int> g_read_index{0};              // 当前读取的缓冲区索引
std::atomic<int> g_write_index{1};             // 当前写入的缓冲区索引
bool permeate_record = false;
bool permeate_record_ini = false;
struct Last_ImRect LastCoordinate = {0, 0, 0, 0};
static uint32_t orientation = -1;
ANativeWindow *window; 
// 屏幕信息
android::ANativeWindowCreator::DisplayInfo displayInfo;
// 窗口信息
ImGuiWindow *g_window;
// 绝对屏幕X _ Y
int abs_ScreenX, abs_ScreenY;
int native_window_screen_x, native_window_screen_y;
std::unique_ptr<AndroidImgui>  graphics;
ImFont* zh_font = NULL;
ImFont* icon_font_0 = NULL;
ImFont* icon_font_1 = NULL;
ImFont* icon_font_2 = NULL;
float 矩阵视野距离;
bool M_Android_LoadFont(float SizePixels) {
    ImGuiIO &io = ImGui::GetIO();
    
    // 使用内嵌字体（兼容Android 15/16）
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.SizePixels = SizePixels;
    config.OversampleH = 1;
    ::zh_font = io.Fonts->AddFontFromMemoryTTF((void *)OPPOSans_H, OPPOSans_H_size, SizePixels, &config, io.Fonts->GetGlyphRangesChineseFull());    

	static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.OversampleH = 3.0;
    icons_config.OversampleV = 3.0;		
    icons_config.SizePixels = SizePixels;
	::icon_font_0 = io.Fonts->AddFontFromMemoryCompressedTTF((const void *)&font_awesome_brands_compressed_data, sizeof(font_awesome_brands_compressed_data), 0.0f, &icons_config, icons_ranges);
	::icon_font_1 = io.Fonts->AddFontFromMemoryCompressedTTF((const void *)&font_awesome_regular_compressed_data, sizeof(font_awesome_regular_compressed_data), 0.0f, &icons_config, icons_ranges);
	::icon_font_2 = io.Fonts->AddFontFromMemoryCompressedTTF((const void *)&font_awesome_solid_compressed_data, sizeof(font_awesome_solid_compressed_data), 0.0f, &icons_config, icons_ranges);

    io.Fonts->AddFontDefault();
    return zh_font != nullptr;
}

// 获取 Android 系统版本
static int get_android_version() {
    char version_str[128] = {0};
    __system_property_get("ro.build.version.release", version_str);
    if (version_str[0] == '\0') return 0;
    return atoi(version_str);
}

void init_My_drawdata() {
    int android_version = get_android_version();
    if (android_version >= 15) {
        // Android 15 及以上：使用内嵌字体（系统字体访问受限）
        M_Android_LoadFont(24.0f);
    } else {
        // Android 14 及以下：使用系统字体
        ImGui::My_Android_LoadSystemFont(25.0f);
        M_Android_LoadFont(25.0f);
    }
}


void screen_config() {
    ::displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
}

void drawBegin() {
    if (::permeate_record_ini) {
        LastCoordinate.Pos_x = ::g_window->Pos.x;
        LastCoordinate.Pos_y = ::g_window->Pos.y;
        LastCoordinate.Size_x = ::g_window->Size.x;
        LastCoordinate.Size_y = ::g_window->Size.y;

        graphics->Shutdown();
        android::ANativeWindowCreator::Destroy(::window);
        ::window = android::ANativeWindowCreator::Create("逆天改命", native_window_screen_x, native_window_screen_y, permeate_record);
        graphics->Init_Render(::window, native_window_screen_x, native_window_screen_y);
        ::init_My_drawdata(); //初始化绘制数据
    } 

    screen_config();
    if (::orientation != displayInfo.orientation) {
        ::orientation = displayInfo.orientation;
        Touch::setOrientation(displayInfo.orientation);
        if (g_window != NULL) {
            g_window->Pos.x = 100;
            g_window->Pos.y = 125;        
        }        
        //cout << " width:" << displayInfo.width << " height:" << displayInfo.height << " orientation:" << displayInfo.orientation << endl;
    }
}

struct Vector3A
{
	float X;
	float Y;
	float Z;

	  Vector3A()
	{
		this->X = 0;
		this->Y = 0;
		this->Z = 0;
	}

	Vector3A(float x, float y, float z)
	{
		this->X = x;
		this->Y = y;
		this->Z = z;
	}

};

float xs_prime_mirror, ys_prime_mirror;
float xs_final, ys_final;
void calculate_mirror_reflection(float x1, float y1, float x2, float y2, float xs, float ys, float *xs_prime, float *ys_prime) {
    float xm = (x1 + x2) / 2.0;
    float ym = (y1 + y2) / 2.0;

    *xs_prime = 2.0 * xm - xs;
    *ys_prime = 2.0 * ym - ys;
}

void calculate_line_reflection(float x1, float y1, float x2, float y2, float xs, float ys, float *xs_prime, float *ys_prime) {
    float A = y2 - y1;
    float B = x1 - x2;
    float C = x2 * y1 - x1 * y2;

    float D = A * xs + B * ys + C;
    float denom = A * A + B * B;

    *xs_prime = xs - 2.0 * A * D / denom;
    *ys_prime = ys - 2.0 * B * D / denom;
}


uintptr_t libbase;
uintptr_t Arrayaddr, Count, Matrix;
uintptr_t 对象,对象阵营,自身,自身阵营,namezfcz,namezfc;
uintptr_t 红夫人 = 0, 红夫人镜像 = 0, 镜子 = 0, 捏镜子 = 0;  // 初始化为0
int zfcz,zfc;
float 过滤矩阵[17];
float matrix[16];
float angle;

static bool show_draw_Prop = true;//道具
static bool show_draw_prophet = true;//预知监管者
static bool redqueenmod = false;//红夫人模式
static bool show_draw_sender = true;//密码机进度
static bool show_draw_secret_mechine = false;//密码机位置

static bool show_draw_ClassName = false;//类名
static bool Debugging = false;//调试
static bool mirror = false;//镜子状态
static bool show_demo_window = false;
static bool show_another_window = false;
static bool inform_ghost = true;

float z_x, z_y, z_z, d_x, d_y, d_z, camera, r_x, r_y, r_w;
float X1,Y1,X2,Y2,W,H,MIDDLE,TOP,BOTTOM;
float 距离;	
char objtext[256];
//char content[1024];
char Team[1024];
char Name[1024];
char 监管者预知[1024];
char fullnameof[1024];
float px,py;
Vector3A D,Z,M;

ImColor 红色 = ImColor(250,0,0,255);
ImColor 绿色 = ImColor(0,255,0,255);
ImColor 蓝色 = ImColor(0,0,255,255);
ImColor 黄色 = ImColor(255,255,0,255);
ImColor 紫色 = ImColor(255,0,255,255);
ImColor 黑色 = ImColor(0,0,0,255);
ImColor 浅蓝色=ImColor(0, 191, 255, 255);
ImColor BoneColor = ImColor(255,0,0,255);
ImColor BotBoneColor = ImColor(255,255,255,255);
int 状态 = 0;
int 数据获取状态 = 0;
int 遍历次数=0;
char extractedString[64];
long int MatrixOffset = 0,ArrayaddrOffset = 0;
typedef struct {
    unsigned long addr;
    unsigned long taddr;
} ModuleBssInfo;

// 读取 /proc/[pid]/maps 中的内容
int read_process_maps(int pid, char *filename, char *line, size_t line_size) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        return -1; // 打开文件失败
    }

    while (fgets(line, line_size, fp) != NULL) {
        if (strstr(line, "r-xp") != NULL || strstr(line, "rw") != NULL) {
            fclose(fp);
            return 0; // 找到文件并成功读取
        }
    }
    fclose(fp);
    return -1; // 未找到内容
}

// 提取模块的地址范围
ModuleBssInfo extract_module_info(const char *line) {
    ModuleBssInfo info = {0, 0};
    long addr, taddr;
    if (sscanf(line, "%lx-%lx", &addr, &taddr) == 2) {
        info.addr = addr;
        info.taddr = taddr;
    }
    return info;
}

// 获取指定进程中的模块BSS信息
ModuleBssInfo get_module_bss(int pid, const char *module_name) {
    char filename[64];
    char line[1024];
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);

    ModuleBssInfo info = {0, 0};
    int found_module = 0;

    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        return info;
    }

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, module_name)) {
            found_module = 1;
        }

        if (found_module) {
            // 查找符合条件的 rw 权限行
            if (strstr(line, "rw") != NULL && strlen(line) < 86) {
                ModuleBssInfo temp_info = extract_module_info(line);
                if ((temp_info.taddr - temp_info.addr) / 4096 >= 2800) {
                    fclose(fp);
                    return temp_info;
                }
            }
        }
    }
    fclose(fp);
    return info;
}

long get_module_base(int pid, const char *module_name)
{
    long addr = 0;
    char filename[64];
    char line[1024];
    
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    
    FILE *fp = fopen(filename, "r");
    if (fp != NULL)
    {
        while (fgets(line, sizeof(line), fp))
        {
            if (strstr(line, module_name))
            {
                sscanf(line, "%lx-%*lx", &addr);
                break;
            }
        }
        fclose(fp);
    }
    return addr;
}

// 获取进程ID
int get_name_pid1(const char *packageName) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;

    struct dirent *entry;
    FILE *fp;
    char filename[64];
    char cmdline[64];
    int id = -1;

    while ((entry = readdir(dir)) != NULL) {
        id = atoi(entry->d_name);
        if (id != 0) {
            snprintf(filename, sizeof(filename), "/proc/%d/cmdline", id);
            fp = fopen(filename, "r");
            if (fp) {
                fgets(cmdline, sizeof(cmdline), fp);
                fclose(fp);
                if ((strstr(cmdline, packageName) != NULL || strstr(cmdline, "com.netease.idv") != NULL) &&
                    strstr(cmdline, "com") != NULL && strstr(cmdline, "PushService") == NULL && strstr(cmdline, "gcsdk") == NULL) {
                    sprintf(extractedString, "%s", cmdline);
                    closedir(dir);
                    return id;
                }
            }
        }
    }
    closedir(dir);
    return -1;
}

// ============== 线程CPU亲和性设置（小核优化） ==============
static long getCpuMaxFreqForThread(int cpu) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
    FILE* file = fopen(path, "r");
    if (!file) return -1;
    long freq = 0;
    fscanf(file, "%ld", &freq);
    fclose(file);
    return freq;
}

static void setThreadToLittleCore() {
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    
    // 获取CPU核心数
    int numCpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (numCpus <= 0) numCpus = 8;
    if (numCpus > 16) numCpus = 16;
    
    long freqs[16] = {0};
    long minFreq = LONG_MAX;
    long maxFreq = 0;
    
    // 获取所有核心的最大频率
    for (int i = 0; i < numCpus; i++) {
        freqs[i] = getCpuMaxFreqForThread(i);
        if (freqs[i] > 0) {
            if (freqs[i] < minFreq) minFreq = freqs[i];
            if (freqs[i] > maxFreq) maxFreq = freqs[i];
        }
    }
    
    // 计算小核阈值
    long threshold = (minFreq + maxFreq) / 2;
    int count = 0;
    
    for (int i = 0; i < numCpus; i++) {
        if (freqs[i] > 0 && freqs[i] <= threshold) {
            CPU_SET(i, &cpuSet);
            count++;
        }
    }
    
    // 如果检测失败，默认使用前4个核心
    if (count == 0) {
        int defaultLittle = (numCpus > 4) ? 4 : numCpus;
        for (int i = 0; i < defaultLittle; i++) {
            CPU_SET(i, &cpuSet);
        }
    }
    
    pid_t tid = syscall(__NR_gettid);  // Android 兼容的获取线程ID方式
    sched_setaffinity(tid, sizeof(cpuSet), &cpuSet);
    nice(5);  // 降低线程优先级
}

int c;
char libso[256] = {"libclient.so"};
void read_thread(long int PD1, long int PD2, long int PD3) {
    // 将读取线程也绑定到小核，减少大核占用
    setThreadToLittleCore();
    
    ModuleBssInfo result;
    
    // 循环获取进程ID和模块地址，直到都有效为止
    while (true) {
        // 尝试获取进程ID
        pid = get_name_pid1("dwrg");
        if (pid == -1) {
            状态 = 0;
            sleep(2);  // 每2秒重试一次
            continue;
        }
        
        // 成功获取进程ID后，获取包名和模块地址
        get_name_pid(extractedString);
        
        if (strstr(extractedString, "com.netease.idv") != NULL) {
            libbase = get_module_base(pid, ".");
            result = get_module_bss(pid, ".");
        } else {
            libbase = get_module_base(pid, libso);
            result = get_module_bss(pid, libso);
        }
        
        // 检查模块地址是否有效
        if (libbase != 0 && result.addr != 0) {
            break;  // 成功获取进程ID和模块地址
        }
        
        // 模块地址无效，可能游戏刚启动，模块还未加载完成
        状态 = 0;
        sleep(2);  // 等待后重试
    }

    c = (result.taddr - result.addr) / 4096;
    long buff[512];  // 缓存指针数据

    // 搜索 MatrixOffset 和 ArrayaddrOffset
    while (MatrixOffset == 0 || ArrayaddrOffset == 0) {
        状态 = 1;
        bool found_both = false;
        
        for (int i = 0; i < c && !found_both; i++) {
            vm_readv(result.addr + (i * 4096), &buff, 0x1000);
            for (int ii = 0; ii < 512; ii++) {
                // 仅在 MatrixOffset 未找到时搜索
                if (MatrixOffset == 0 && buff[ii] != 0) {
                    int tempMatrix = getDword(getPtr64(buff[ii] + Offsets::MATRIX_PTR_1) + Offsets::MATRIX_FEATURE_OFFSET);
                    if (tempMatrix == Offsets::MATRIX_FEATURE_1 || tempMatrix == Offsets::MATRIX_FEATURE_2) {
                        MatrixOffset = result.addr - libbase + i * 4096 + ii * Offsets::ARRAY_ELEMENT_SIZE;
                    }
                }

                // 仅在 ArrayaddrOffset 未找到时搜索
                if (ArrayaddrOffset == 0 && buff[ii] == Offsets::ARRAY_FEATURE_1) {
                    int tempszz = getDword(result.addr + 4096 * i + Offsets::ARRAY_ELEMENT_SIZE * ii - Offsets::ARRAY_PREV_CHECK);
                    if (tempszz == Offsets::ARRAY_FEATURE_2 && getPtr64(result.addr + i * 4096 + ii * Offsets::ARRAY_ELEMENT_SIZE + Offsets::ARRAY_CHECK_OFFSET) == (result.addr + i * 4096 + ii * Offsets::ARRAY_ELEMENT_SIZE + Offsets::ARRAY_SELF_REF)) {
                        ArrayaddrOffset = result.addr - libbase + i * 4096 + ii * Offsets::ARRAY_ELEMENT_SIZE + Offsets::ARRAY_ADDR_OFFSET;
                    }
                }
                
                // 两个都找到了，提前退出
                if (MatrixOffset != 0 && ArrayaddrOffset != 0) {
                    found_both = true;
                    break;
                }
            }
        }

        if (found_both) {
            break;
        }
        遍历次数++;
        sleep(5);
    }
    状态 = 2;
    while (true)
    {    
        Arrayaddr = getPtr64(libbase + ArrayaddrOffset);    //数组
        long int 测试 = getPtr64(libbase + ArrayaddrOffset+8);
        Count = 2000;    //数组数量
        
        // 三缓冲：获取空闲的写入缓冲区
        int read_idx = g_read_index.load(std::memory_order_acquire);
        int last_write = g_write_index.load(std::memory_order_relaxed);
        // 找到既不是当前读取的，也不是上次写入的缓冲区
        int free_idx;
        for (free_idx = 0; free_idx < 3; free_idx++) {
            if (free_idx != read_idx && free_idx != last_write) break;
        }
        if (free_idx >= 3) free_idx = (last_write + 1) % 3;  // 安全回退
        
        GameDataBuffer& write_buffer = g_buffer[free_idx];
                             
        int 指针数量=0;
        int 红夫人模式=0;
        static std::string s;           // 静态变量复用，避免每次分配
        static std::string fullname;
        s.clear();                      // 清空而不是重新构造
        fullname.clear();
        for (int ii = 0; ii < Count; ii++){
            对象 = getPtr64(Arrayaddr + Offsets::ARRAY_ELEMENT_SIZE * ii);    // 遍历数量次数            
                
            if (对象 == 0)               
                break;                                
            
            namezfcz = GET_CLASSNAME_PTR(对象);
            int len = getDword(namezfcz + Offsets::CLASSNAME_LEN);
            if (len>=256)                
                continue;
                
            过滤类名.resize(0);
            for (int i = 0; i < len; i+=24) {                
                vm_readv(getPtr64(namezfcz)+i,gwd1,24);
                gwd1[24] = '\0';
                过滤类名 += gwd1;
            }
            
            if (should_filter(过滤类名)) {
                continue;  // 过滤随从等无关对象
            }

            
            float pd1 = getFloat(对象 + Offsets::OBJ_CAMP);
            float pd2 = getFloat(对象 + Offsets::OBJ_PD2);

            int 过滤重复指针=0;
            for (int i = 0; i < 指针数量; i++){
                if(对象 == write_buffer.data[i].obj){
                    过滤重复指针=1;
                }                    
            }
            if (过滤重复指针 == 1){
                continue;
            }
                
            
            // 使用 contains() 替代 strstr，更清晰且避免重复调用 c_str()
            bool is_player = contains(过滤类名, "player");
            bool is_boss = contains(过滤类名, "boss");
            bool is_scene = contains(过滤类名, "scene");
            bool is_prop = contains(过滤类名, "prop");
            bool is_mirror = contains(过滤类名, "mirror");
            
            if (is_player || is_boss || pd1 == 450 || is_scene || is_prop || is_mirror || Debugging)
            {
                write_buffer.data[指针数量].obj = 对象;
                if (is_boss) {
                    strcpy(write_buffer.data[指针数量].str, getboss(过滤类名.c_str()));
                    write_buffer.data[指针数量].阵营 = 1;
                    strcpy(write_buffer.data[指针数量].类名, 过滤类名.c_str());
                    write_buffer.data[指针数量].objcoor = getPtr64(对象 + Offsets::OBJ_COORD_PTR);
                    指针数量++;
                }
                else if (is_player || contains(过滤类名, "npc_deluosi_dress_ghost") || contains(过滤类名, "huojian")) {
                    strcpy(write_buffer.data[指针数量].str, getplayer(过滤类名.c_str()));
                    write_buffer.data[指针数量].阵营 = 2;
                    strcpy(write_buffer.data[指针数量].类名, 过滤类名.c_str());
                    write_buffer.data[指针数量].objcoor = getPtr64(对象 + Offsets::OBJ_COORD_PTR);
                    指针数量++;
                }
                else if (contains(过滤类名, "redqueen") && is_mirror && contains(过滤类名, "model")) {
                    strcpy(write_buffer.data[指针数量].类名, 过滤类名.c_str());
                    write_buffer.data[指针数量].objcoor = getPtr64(对象 + Offsets::OBJ_COORD_PTR);
                    write_buffer.data[指针数量].阵营 = 5;
                    指针数量++;
                }
                else {
                    const char* scene_result = getscene(过滤类名.c_str());
                    const char* prop_result = getprop(过滤类名.c_str());
                    if (is_scene) {
                        if (scene_result != NULL) {
                            strcpy(write_buffer.data[指针数量].类名, 过滤类名.c_str());
                            write_buffer.data[指针数量].objcoor = getPtr64(对象 + Offsets::OBJ_COORD_PTR);
                            strcpy(write_buffer.data[指针数量].str, scene_result);
                            write_buffer.data[指针数量].阵营 = 3;
                            指针数量++;
                        } else {
                            continue;
                        }
                    }
                    else if (is_prop && !contains(过滤类名, "woodpalne")) {
                        if (prop_result != NULL) {
                            strcpy(write_buffer.data[指针数量].类名, 过滤类名.c_str());
                            write_buffer.data[指针数量].objcoor = getPtr64(对象 + Offsets::OBJ_COORD_PTR);
                            strcpy(write_buffer.data[指针数量].str, prop_result);
                            write_buffer.data[指针数量].阵营 = 4;
                            指针数量++;
                        } else {
                            continue;
                        }
                    }
                }
            }
            
            // 预知监管者
            if (show_draw_prophet && is_boss) {
                std::string bossName = getboss(过滤类名.c_str());
                if (s.find(bossName) == std::string::npos 
                    && bossName.find("伊斯人") == std::string::npos 
                    && bossName.find("信徒") == std::string::npos
                    && bossName.find("厂长残火") == std::string::npos
                    && bossName.find("butcher") == std::string::npos) {
                    s += bossName + " ";
                    fullname += 过滤类名;
                    fullname += " | ";
                }
            }

            // 处理红夫人模式
            if (pd1 == Offsets::CAMP_VALUE) {
                int jxpd = getDword(对象 + Offsets::OBJ_GHOST);
                uintptr_t objcoor = getPtr64(对象 + Offsets::OBJ_COORD_PTR);  // 缓存指针，避免重复读取
                float coordE0 = getFloat(objcoor + Offsets::COORD_ALT_X);
                float coordE4 = getFloat(objcoor + Offsets::COORD_ALT_Z);
                float coordE8 = getFloat(objcoor + Offsets::COORD_ALT_Y);
                
                if (is_boss && contains(过滤类名, "redqueen") && !is_mirror &&
                    coordE0 != 0 && coordE8 != 0) {
                    if (红夫人模式 == 1 && 
                        ((coordE4 <= -300 && jxpd == Offsets::GHOST_VALUE) || (coordE4 >= -300 && jxpd != Offsets::GHOST_VALUE))) {
                        红夫人镜像 = 对象;
                    } else if (coordE4 >= -300 && jxpd != Offsets::GHOST_VALUE) {
                        红夫人 = 对象;
                        红夫人模式 = 1;
                    }
                }
                if (is_boss && is_mirror && coordE0 != 0 && coordE8 != 0) {
                    镜子 = 对象;
                }
            }
        }
        sprintf(监管者预知, "%s", s.c_str());
        sprintf(fullnameof, "%s", fullname.c_str());
        
        // 三缓冲：更新数量，记录写入索引，然后发布给读取线程
        write_buffer.数量 = 指针数量;
        g_write_index.store(free_idx, std::memory_order_relaxed);      // 记录本次写入位置
        g_read_index.store(free_idx, std::memory_order_release);       // 发布给读取线程
        
        usleep(8000);  // 8ms 更新一次
    }
}

void Draw_Main(ImDrawList *Draw){
    // 三缓冲：直接读取活跃缓冲区，无需加锁
    int read_index = g_read_index.load(std::memory_order_acquire);
    const GameDataBuffer& read_buffer = g_buffer[read_index];
    
    Matrix = getPtr64(getPtr64(libbase + MatrixOffset) + Offsets::MATRIX_PTR_1) + Offsets::MATRIX_OFFSET_1; //矩阵
    M.X = getFloat(Matrix - Offsets::MATRIX_CAMERA);
    M.Z = getFloat(Matrix - Offsets::MATRIX_CAMERA + 4);
    M.Y = getFloat(Matrix - Offsets::MATRIX_CAMERA + 8);
    
    // 红夫人坐标读取（加保护检查）
    if (红夫人 != 0) {
        uintptr_t 红夫人坐标指针 = getPtr64(红夫人 + Offsets::OBJ_COORD_PTR);
        if (红夫人坐标指针 != 0) {
            红夫人X = getFloat(红夫人坐标指针 + Offsets::COORD_X);
            红夫人Z = getFloat(红夫人坐标指针 + Offsets::COORD_Z);
            红夫人Y = getFloat(红夫人坐标指针 + Offsets::COORD_Y);
        }
    }
    if (红夫人镜像 != 0) {
        uintptr_t 镜像坐标指针 = getPtr64(红夫人镜像 + Offsets::OBJ_COORD_PTR);
        if (镜像坐标指针 != 0) {
            红夫人镜像X = getFloat(镜像坐标指针 + Offsets::COORD_X);
            红夫人镜像Z = getFloat(镜像坐标指针 + Offsets::COORD_Z);
            红夫人镜像Y = getFloat(镜像坐标指针 + Offsets::COORD_Y);
        }
    }
    if (红夫人Z >= -300 && 红夫人镜像Z >= -300&&红夫人X != 0 && 红夫人Y != 0 && 红夫人镜像X != 0 && 红夫人镜像Y != 0) {
        mirror=true;
    }else{
        mirror=false;
    }
    Matrix = getPtr64(getPtr64(getPtr64(libbase + MatrixOffset) + Offsets::MATRIX_PTR_1) + Offsets::MATRIX_PTR_2) + Offsets::MATRIX_OFFSET_2; //矩阵
    memset(matrix, 0, 16);
    vm_readv(Matrix-4, 过滤矩阵, 17 * 4);

    
    if (过滤矩阵[0]==1){
        for (int i = 0; i < 16; i++){
            if (i<=3){
                if (过滤矩阵[i+1]>=-2&&过滤矩阵[i+1]<=2)
                matrix[i]=过滤矩阵[i+1];
            }else{
                matrix[i]=过滤矩阵[i+1];
            }
        }
    }
    if (show_draw_prophet){
        auto textSize = ImGui::CalcTextSize(监管者预知, 0, 25);
        Draw->AddText({px-(textSize.x/2),130}, 红色, 监管者预知);
    }

    for (int i = 0; i < read_buffer.数量; i++){
        const DataStruct& obj = read_buffer.data[i];  // 引用当前对象
    
        // if (strstr(obj.类名, "buzz") != NULL)
        //     continue;//跳过不知所谓的东西
        // if (strstr(obj.类名, "nvyao.gim") != NULL)
        //     continue;//跳过女妖蜡烛

        D.X = getFloat(obj.objcoor + Offsets::COORD_X);
        D.Z = getFloat(obj.objcoor + Offsets::COORD_Z);
        D.Y = getFloat(obj.objcoor + Offsets::COORD_Y);
        
        if (D.Y<1e-4&&D.Y>-1e-4 || D.X<1e-4&&D.X>-1e-4){
		    continue;//跳过xy0
		}
        // if (D.Z <3 && D.Y<3 &&D.X<3 && D.X >-3 && D.Y>-3 &&D.Z>-3 &&strstr(data[i].str,"哭泣小丑")!=NULL){
		//     continue;
		// }//这是不应该出现的东西


		if (D.Z<=-300){
		    continue;//跳过地下
	}
		int jxpd = getDword(obj.obj + Offsets::OBJ_GHOST);
		camera = matrix[3] * D.X + matrix[7] * D.Z + matrix[11] * D.Y + matrix[15];
        距离 = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2) + pow(D.Z - Z.Z, 2)) / 距离比例;
        // 矩阵视野距离 = sqrt(pow(D.X - M.X, 2) + pow(D.Y - M.Y, 2) + pow(D.Z - M.Z, 2)) / 距离比例;
		

		r_x = px + (matrix[0] * D.X + matrix[4] * D.Z + matrix[8] * D.Y + matrix[12]) / camera * px;
        r_y = py - (matrix[1] * D.X + matrix[5] * (D.Z+ 8.5) + matrix[9] * (D.Y) + matrix[13]) / camera * py;
        r_w = py - (matrix[1] * D.X + matrix[5] * (D.Z+ 28.5) + matrix[9] * (D.Y) + matrix[13]) / camera * py;
												
		W = (r_y - r_w) / 2;	// 宽度
		H = r_y - r_w;		// 高度
		X1 = r_x - (r_y - r_w) / 4;	// X1
		Y1 = r_y - H / 2;	// Y1
		X2 = X1 + W;		// X2
		Y2 = Y1 + H;		// Y2
		if (距离>=300){
            continue;
        }
        //w的判断是否需要继续修改？

        
        if (W>0){
            if (Debugging)
            {                
                std::string test;
                sprintf(objtext, "%lx", obj.obj);
                test += " [";
                test += std::to_string((int) 距离);    
                test += " 米]  0x";
                test += objtext;    
                test += " [类名] ";
                test += obj.类名;
                test += "x|";
                std::stringstream ss;
                ss << std::fixed << std::setprecision(2) << D.X;
                test += ss.str();
                test += "|y|";
                ss.str(""); // 清空字符串流
                ss << std::fixed << std::setprecision(2) << D.Y;
                test += ss.str();
                
               auto textSize = ImGui::CalcTextSize(test.c_str(), 0, 25);
                Draw->AddText({r_x-(textSize.x/2),r_y}, ImColor(255,200,0,255), test.c_str());
            }
		
            if (obj.阵营 == 3) 
            {
            std::string s;
            if (strstr(obj.类名, "ordnance_factory1\\dm65_scene_sender") != NULL && strstr(obj.类名, "low") != NULL) 
            {
                if (show_draw_secret_mechine) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << 距离;
                s += "[密码机] " + oss.str() + " 米";
                auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                Draw->AddText({r_x - (textSize.x / 2), r_y}, 
                  (距离 >= 61 && 距离 <= 63) ? 绿色 : ImColor(255, 255, 255, 255), 
                  s.c_str());}
            } 
            else if (strstr(obj.类名, "dm65_scene_prop_76") != NULL) 
            {
                s += "[地窖] " + std::to_string((int) 距离) + " 米";
                auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                Draw->AddText({r_x - (textSize.x / 2), r_y}, 浅蓝色, s.c_str());
            }
            }
		
		    if (show_draw_Prop && obj.阵营 == 4) 
            {
            std::string s;
                s += std::string(obj.str) + " " + std::to_string((int) 距离) + " 米";
                auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                Draw->AddText({r_x - (textSize.x / 2), r_y}, ImColor(255, 200, 0, 255), s.c_str());     
            }

            int zy;//=getbool(obj.obj + Offsets::OBJ_SELF);
            vm_readv(obj.obj + Offsets::OBJ_SELF, &zy, 1);        
                
            if (getFloat(obj.obj + Offsets::OBJ_CAMP) == Offsets::CAMP_VALUE || strstr(obj.str,"哭泣小丑") != NULL)
            {     
                // if (strstr(obj.类名, "h55_joseph_camera") != NULL){
                //     continue;//跳过约瑟夫相机
                // }
                    
                if (strstr(obj.类名, "redqueen_mirror") != NULL){
                    continue;//跳过红芙蓉镜子
                }
                
                // if (strstr(obj.类名, "burke_console") != NULL){
    		    //     continue;//跳过疯眼场景
    		    // }
    		    
    			// if (strstr(obj.类名, "h55_survivor_w_shangren_tiaoban") != NULL){
    			//     continue;//跳过商人跳板
    			// }
    			
                //我需要优化自身判定			            
                if (camera < 40 && camera > 10 && zy&&(obj.阵营==1||obj.阵营==2))
                {
                    自身 = obj.obj;
                    Z.X = D.X;
                    Z.Z = D.Z;
                    Z.Y = D.Y;
                    自身阵营=对象阵营;
                       continue;
                }

                //处理主要绘制逻辑
                //自定义显示鬼魂

                std::string s;
                s += obj.str;//翻译名
                auto textSize = ImGui::CalcTextSize(s.c_str(), 0, 25);
                std::string 人物距离;
                人物距离 += std::to_string((int) 距离);
                人物距离 += " 米";
                auto textSize1 = ImGui::CalcTextSize(人物距离.c_str(), 0, 25);
                if (obj.阵营 == 1 || obj.阵营 == 2) {
                    if(jxpd == Offsets::GHOST_VALUE && !inform_ghost){continue;}         
                    else
                {
                    if(jxpd == Offsets::GHOST_VALUE)
                    {
                        if (strstr(obj.str, "红蝶") != NULL || strstr(obj.str, "无常") != NULL || strstr(obj.str, "歌剧") != NULL|| strstr(obj.str, "破轮") != NULL || strstr(obj.str, "木偶") != NULL) {
                            continue;
                        }
                        else
                        {
                            ImGui::GetForegroundDrawList()->AddRect({X1, Y1}, {X2, Y2}, 浅蓝色, 3, 0, 1.8);
                            Draw->AddText({X1 + W / 2 - (textSize.x / 2), Y1 - 45}, ImColor(255, 200, 0, 255), s.c_str());
                        }
                    }
                    
                else
                {
                    if (obj.阵营 == 1) 
                    {
                        ImGui::GetForegroundDrawList()->AddRect({X1, Y1}, {X2, Y2}, BoneColor, 3, 0, 1.8f);
                        Draw->AddText({X1 + W / 2 - (textSize.x / 2), Y1 - 45}, ImColor(255, 200, 0, 255), s.c_str());
                        Draw->AddText({X1 + W / 2 - (textSize1.x / 2), Y2 + 10}, ImColor(255, 200, 0, 255), 人物距离.c_str());
                    }
                    else if (obj.阵营 == 2)
                    {
                        ImGui::GetForegroundDrawList()->AddRect({X1, Y1}, {X2, Y2}, 绿色, 3, 0, 1.8f);
                        Draw->AddText({X1 + W / 2 - (textSize.x / 2), Y1 - 45}, ImColor(255, 200, 0, 255), s.c_str());
                        Draw->AddText({X1 + W / 2 - (textSize1.x / 2), Y2 + 10}, ImColor(255, 200, 0, 255), 人物距离.c_str());
                    }

                    if (obj.阵营 == 1) {
                        ImGui::GetForegroundDrawList()->AddLine({px, 160}, {X1 + W / 2, Y1}, ImColor(255, 20, 147), 2);
                    } else if (obj.阵营 == 2 ) {
                        ImGui::GetForegroundDrawList()->AddLine({px, 160}, {X1 + W / 2, Y1}, ImColor(255, 255, 255), 2);
                    }
                }
                }
                }
                        
            }                                                          			
    	}  
    
	                
	   //红夫人镜像                                   
	    if (mirror&&redqueenmod){
            if (getFloat(obj.obj + Offsets::OBJ_CAMP) == Offsets::CAMP_VALUE && obj.阵营 == 2){
                std::string ss;
                calculate_mirror_reflection(红夫人X, 红夫人Y, 红夫人镜像X, 红夫人镜像Y, D.X, D.Y, &xs_prime_mirror, &ys_prime_mirror);
                calculate_line_reflection(红夫人X, 红夫人Y, 红夫人镜像X,  红夫人镜像Y,xs_prime_mirror, ys_prime_mirror, &D.X, &D.Y);
                camera = matrix[3] * D.X + matrix[7] * D.Z + matrix[11] * D.Y + matrix[15];
                距离 = sqrt(pow(D.X - Z.X, 2) + pow(D.Y - Z.Y, 2) + pow(D.Z - Z.Z, 2)) / 距离比例;
        		r_x = px + (matrix[0] * D.X + matrix[4] * D.Z + matrix[8] * D.Y + matrix[12]) / camera * px;
                r_y = py - (matrix[1] * D.X + matrix[5] * (D.Z+ 8.5) + matrix[9] * (D.Y) + matrix[13]) / camera * py;
                r_w = py - (matrix[1] * D.X + matrix[5] * (D.Z+ 28.5) + matrix[9] * (D.Y) + matrix[13]) / camera * py;
												
        		W = (r_y - r_w) / 2;	// 宽度
        		H = r_y - r_w;		// 高度
        		X1 = r_x - (r_y - r_w) / 4;	// X1
        		Y1 = r_y - H / 2;	// Y1
        		X2 = X1 + W;		// X2
        		Y2 = Y1 + H;		// Y2
        
                if (W>0){

                    ss += obj.str;
                    auto textSize = ImGui::CalcTextSize(ss.c_str(), 0, 25);
                    Draw->AddText({X1 + W/2-(textSize.x/2),Y1-45}, BotBoneColor, ss.c_str());
                        ImGui::GetForegroundDrawList()->AddRect({X1, Y1},{X2, Y2}, BotBoneColor,3, 0,1.8f);   
                        std::string 镜像距离;
                        镜像距离 += std::to_string((int) 距离);
                        镜像距离 += " 米";
                        auto textSize1 = ImGui::CalcTextSize(镜像距离.c_str(), 0, 25);
                        Draw->AddText({X1 + W/2-(textSize1.x/2),Y2+10}, BotBoneColor, 镜像距离.c_str());
                        ImGui::GetForegroundDrawList()->AddLine({px, 160},{X1 + W/2, Y1}, ImColor(255, 255, 255),2);
                               
                }//判断W
            }                
        }
    }      
}
        
static bool show_window = true; // 控制内部窗口显示的布尔变量
static bool voice = true; // 初始化 voice 变量

// 获取输入设备数量
int GetInputDeviceCount() {
    DIR *dir = opendir("/dev/input/");
    if (!dir) return -1;
    dirent *ptr = NULL;
    int count = 0;
    while ((ptr = readdir(dir)) != NULL) {
        if (strstr(ptr->d_name, "event"))
            count++;
    }
    closedir(dir);  // 关闭目录
    return count ? count : -1;
}

// 在 VolumeKeyHide 中更新 show_window
void VolumeKeyHide() {
    int EventCount = GetInputDeviceCount(); // 获取输入设备数量
    if (EventCount <= 0) return;
    
    int *fdArray = (int *)malloc(EventCount * sizeof(int));
    if (!fdArray) return;

    for (int i = 0; i < EventCount; i++) {
        char temp[128];
        sprintf(temp, "/dev/input/event%d", i);
        fdArray[i] = open(temp, O_RDWR | O_NONBLOCK);
    }

    input_event ev;

    while (1) {
        for (int i = 0; i < EventCount; i++) {
            if (fdArray[i] < 0) continue;  // 跳过无效的文件描述符
            
            memset(&ev, 0, sizeof(ev));
            // 非阻塞读取，不需要每个设备都 usleep
            while (read(fdArray[i], &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type == EV_KEY && ev.code == KEY_VOLUMEDOWN && ev.value == 1) {
                    voice = false;
                }
                if (ev.type == EV_KEY && ev.code == KEY_VOLUMEUP && ev.value == 1) {
                    voice = true;
                }
            }
        }

        // 更新窗口状态
        show_window = voice;
        
        usleep(10000);  // 10ms 检查一次，替代原来的 1ms*N + 0.5ms
    }

    free(fdArray);
}

void Layout_tick_UI(bool *main_thread_flag) {
    static bool volume_thread_started = false; // 用来确保只启动一次线程

    if (!volume_thread_started) {
        std::thread volume_thread(VolumeKeyHide);
        volume_thread.detach(); // 将线程分离，确保程序继续运行
        volume_thread_started = true;
    }

    // 屏幕中心坐标
    px = static_cast<float>(displayInfo.width) / 2;
    py = static_cast<float>(displayInfo.height) / 2;

    // 自定义绘制（保持原有逻辑）
    Draw_Main(ImGui::GetForegroundDrawList()); 

    // 如果按钮被点击，显示详细信息窗口
    if (show_window) {
        ImGui::Begin("New_Edition", nullptr, ImGuiWindowFlags_AlwaysAutoResize); // 自动调整大小的窗口

        // 创建窗口并应用透明度
        
        // 恢复窗口位置与大小（如果需要）
        if (::permeate_record_ini) {
            ImGui::SetWindowPos({LastCoordinate.Pos_x, LastCoordinate.Pos_y});
            ImGui::SetWindowSize({LastCoordinate.Size_x, LastCoordinate.Size_y});
            permeate_record_ini = false;
        }

        // 渲染模式与 FPS 信息
        ImGui::Text("渲染模式 : %s, gui版本 : %s", graphics->RenderName, IMGUI_VERSION);
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "帧率 %.1f FPS", ImGui::GetIO().Framerate);
        
        // 内存占用
        size_t mem_kb = get_memory_usage_kb();
        if (mem_kb >= 1024) {
            ImGui::Text("内存占用: %.2f MB", mem_kb / 1024.0f);
        } else {
            ImGui::Text("内存占用: %zu KB", mem_kb);
        }

        // 数据状态
        ImGui::Text("数据状态:");
        if (状态 == 2) {
            ImGui::TextColored(ImVec4(0.0f, 205.0f, 0.0f, 100.0f), "已获取到游戏数据");
        } else if (状态 == 1) {
            ImGui::TextColored(ImVec4(255.0f, 0.0f, 0.0f, 100.0f), "正在获取游戏数据");
        }

        // 折叠区域：基础信息
        if (ImGui::CollapsingHeader("基础信息")) {
            ImGui::Text("游戏进程:%d", pid);
            ImGui::Text("模块入口:%lx", libbase);
            ImGui::Text("游戏包名:%s", extractedString);
            //数据没有清除功能
            ImGui::Text("矩阵地址:%lx", Matrix);
            ImGui::Text("数组地址:%lx", Arrayaddr);
            ImGui::Text("矩阵偏移:%lx", MatrixOffset);
            ImGui::Text("模块页数:%d", c);
            ImGui::Text("数组偏移:%lx", ArrayaddrOffset);
            ImGui::Text("数据获取状态:%d", 数据获取状态);
            // ImGui::Text("自身x:%f", Z.X);
            // ImGui::Text("自身y:%f", Z.Y);
            // ImGui::Text("自身z:%f", Z.Z);
            ImGui::Text("监管者:%s", 监管者预知);
            // ImGui::Text("红夫人:%lx", 红夫人);
            // ImGui::Text("红夫人镜像:%lx", 红夫人镜像);
            // ImGui::Text("镜子:%lx", 镜子);
            // ImGui::Text("镜子状态:%d", mirror);
            // ImGui::Text("监管者类名:%s",fullnameof);
        }
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        // 折叠区域：绘制设置
        if (ImGui::CollapsingHeader("绘制设置")) {

            ImGui::Text("自动砸板");
            ImGui::Checkbox("显示鬼魂", &inform_ghost);
            ImGui::SameLine();
            ImGui::Checkbox("预知监管", &show_draw_prophet);

 
            ImGui::Checkbox("绘制道具", &show_draw_Prop);
            ImGui::SameLine();
            ImGui::Checkbox("夫人模式", &redqueenmod);

            ImGui::Checkbox("绘制调试", &Debugging);
            ImGui::SameLine();
            ImGui::Checkbox("显示密码机", &show_draw_secret_mechine);
            ImGui::Text("");
            if (ImGui::Button("结束进程"))
            {
                exit(0);
            }
        }

        ImGui::End(); // 详细信息面板结束
    }
}