#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <time.h>
#include <draw.h>

#include "TouchHelper_s.h"

#define maxE 5
#define maxF 10
#define UNGRAB 0
#define GRAB 1

struct touchObj {
    bool occupy = false;
    bool isTmpDown = false;
    bool isDown = false;
    bool isUp = false;
    int x = 0;
    int y = 0;
    int id = 0;
    int size1 = 0;
    int size2 = 0;
    int size3 = 0;
};

struct targ {
    int fdNum;
    float S2TX;
    float S2TY;
};

static int Touch_orientation = 0;
static bool g_Initialized = true;
struct touchObj Finger[maxE][maxF];

static pthread_t touchloop;

static struct input_event event[128];
static struct input_event downEvent[2];
static int fdNum = 0, origfd[maxE], nowfd;


static float touch_s = 0.0f;

static ImVec2 touch_screen_size;

static ImVec2 screenSize;

static bool isFirstDown = true;



char *genRandomString(int length) {
    const char *charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const size_t charsetSize = sizeof(charset) - 1;
    int i;
    char *string = (char *)calloc(length, sizeof(char));

    for (i = 0; i < length - 1; i++) {
        string[i] = charset[rand() % charsetSize];
    }
    string[length - 1] = '\0';

    return string;
}


ImVec2 rotatePointx(uint32_t orientation, ImVec2 mxy, ImVec2 wh) {
    if (orientation == 0) {
        return mxy;
    }
    ImVec2 xy(mxy.x, mxy.y);
    if (orientation == 3) {
        xy.x = (float) wh.y - mxy.y;
        xy.y = mxy.x;
    } else if (orientation == 2) {
        xy.x = (float) wh.x - mxy.x;
        xy.y = (float) wh.y - mxy.y;
    } else if (orientation == 1) {
        xy.x = mxy.y;
        xy.y = (float) wh.x - mxy.x;
    }
    return xy;
}

ImVec2 getTouchScreenDimension(int fd) {
    struct input_absinfo abs_x, abs_y;
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) == -1 || ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) == -1) {
        // 错误处理
        perror("Error getting touch screen dimensions");
        return ImVec2(0.0f, 0.0f); // 或者使用适当的默认值
    }

    return ImVec2(static_cast<float>(abs_x.maximum), static_cast<float>(abs_y.maximum));
}



void Upload() {
    const int MAX_EVENTS = 128;
    const int MAX_CONSECUTIVE_EVENTS = 10;
    int tmpCnt = 0, tmpCnt2 = 0, i, j;

    for (i = 0; i < fdNum; i++) {
        for (j = 0; j < maxF; j++) {
            if (Finger[i][j].isDown && tmpCnt2 <= MAX_CONSECUTIVE_EVENTS) {
                tmpCnt2++;

                // Fill event array with ABS_X, ABS_Y, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID events
                event[tmpCnt].type = EV_ABS;
                event[tmpCnt].code = ABS_X;
                event[tmpCnt].value = Finger[i][j].x;
                tmpCnt++;

                event[tmpCnt].type = EV_ABS;
                event[tmpCnt].code = ABS_Y;
                event[tmpCnt].value = Finger[i][j].y;
                tmpCnt++;

                event[tmpCnt].type = EV_ABS;
                event[tmpCnt].code = ABS_MT_POSITION_X;
                event[tmpCnt].value = Finger[i][j].x;
                tmpCnt++;

                event[tmpCnt].type = EV_ABS;
                event[tmpCnt].code = ABS_MT_POSITION_Y;
                event[tmpCnt].value = Finger[i][j].y;
                tmpCnt++;

                event[tmpCnt].type = EV_ABS;
                event[tmpCnt].code = ABS_MT_TRACKING_ID;
                event[tmpCnt].value = Finger[i][j].id;
                tmpCnt++;

                event[tmpCnt].type = EV_SYN;
                event[tmpCnt].code = SYN_MT_REPORT;
                event[tmpCnt].value = 0;
                tmpCnt++;
            }
        }
    }

    // Check if any events were added, if not, add SYN_MT_REPORT event
    if (tmpCnt == 0) {
        event[tmpCnt].type = EV_SYN;
        event[tmpCnt].code = SYN_MT_REPORT;
        event[tmpCnt].value = 0;
        tmpCnt++;

        // If it's the first touch, add BTN_TOUCH and BTN_TOOL_FINGER events
        if (!isFirstDown) {
            isFirstDown = true;
            event[tmpCnt].type = EV_KEY;
            event[tmpCnt].code = BTN_TOUCH;
            event[tmpCnt].value = 0;
            tmpCnt++;

            event[tmpCnt].type = EV_KEY;
            event[tmpCnt].code = BTN_TOOL_FINGER;
            event[tmpCnt].value = 0;
            tmpCnt++;
        }
    } else {
        // If not the first touch, write downEvent to nowfd
        if (isFirstDown) {
            isFirstDown = false;
            write(nowfd, downEvent, sizeof(downEvent));
        }
    }

    // Add SYN_REPORT event to event array
    event[tmpCnt].type = EV_SYN;
    event[tmpCnt].code = SYN_REPORT;
    event[tmpCnt].value = 0;
    tmpCnt++;

    // Write event array to nowfd
    write(nowfd, event, sizeof(struct input_event) * tmpCnt);
}

void *TypeA(void *arg) {
  targ tmp = *(targ *)arg;
  int i = tmp.fdNum;
  float S2TX = tmp.S2TX;
  float S2TY = tmp.S2TY;
  struct input_event ie;
  int latest = 0;
  for (;;) {
    ImGuiIO &io = ImGui::GetIO();
    if (read(origfd[i], &ie, sizeof(struct input_event))) {
      if (ie.type == EV_ABS) {
        if (ie.code == ABS_MT_SLOT) {
          latest = ie.value;
          continue;
        }
        if (ie.code == ABS_MT_TRACKING_ID) {
          if (ie.value == -1) {
            Finger[i][latest].isUp = true;
            Finger[i][latest].isDown = false;
            if (g_Initialized) {
              io.MouseDown[0] = false;
            }
          } else {
            Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
            Finger[i][latest].isDown = true;
            if (g_Initialized) {
              io.MouseDown[0] = true;
            }
          }
          continue;
        }
        if (ie.code == ABS_MT_POSITION_X) {
          Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
          Finger[i][latest].x = (int)(ie.value * S2TX);
          Finger[i][latest].isTmpDown = true;
          continue;
        }
        if (ie.code == ABS_MT_POSITION_Y) {
          Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
          Finger[i][latest].y = (int)(ie.value * S2TY);
          Finger[i][latest].isTmpDown = true;
          continue;
        }
        if (ie.code == ABS_MT_TOUCH_MAJOR) {
          Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
          Finger[i][latest].size1 = ie.value;
          Finger[i][latest].isTmpDown = true;
          continue;
        }
        if (ie.code == ABS_MT_WIDTH_MAJOR) {
          Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
          Finger[i][latest].size2 = ie.value;
          Finger[i][latest].isTmpDown = true;
          continue;
        }
        if (ie.code == ABS_MT_TOUCH_MINOR) {
          Finger[i][latest].id = (i * 2 + 1) * maxF + latest;
          Finger[i][latest].size3 = ie.value;
          Finger[i][latest].isTmpDown = true;
          continue;
        }
      }
      if (g_Initialized) {
        struct input_event oneevent;
        ImVec2 point = rotatePointx(Touch_orientation, {(float)Finger[i][latest].x, (float)Finger[i][latest].y}, touch_screen_size);
        io.MousePos = ImVec2(point.x / touch_s, point.y / touch_s);
      }
      if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
        if (Finger[i][latest].isTmpDown) {
          Upload();
        }
        continue;
      }
    }
  }
  return 0;
}
const static char *touchDebug = "/storage/emulated/0/touch_debug.log";

void printTouchScreenInfo() {
    // 打印触摸屏信息
    printf("\033[1;31m已调用独家触摸库\033[0m\n");
    printf("Scale factor (touch_s): %f", touch_s);
}

void initializeDownEvent() {
    // 初始化 downEvent 数组
    downEvent[0].type = EV_KEY;
    downEvent[0].code = BTN_TOUCH;
    downEvent[0].value = 1;
    downEvent[1].type = EV_KEY;
    downEvent[1].code = BTN_TOOL_FINGER;
    downEvent[1].value = 1;
}

void Touch_Init(const ImVec2 &s) {
    screenSize = s;
    printTouchScreenInfo();
    initializeDownEvent();



    downEvent[0].type = EV_KEY;
    downEvent[0].code = BTN_TOUCH;
    downEvent[0].value = 1;
    downEvent[1].type = EV_KEY;
    downEvent[1].code = BTN_TOOL_FINGER;
	downEvent[1].value = 1;
    
    char temp[128];
    DIR *dir = opendir("/dev/input/");
    dirent *ptr = NULL;
    int eventCount = 0;
    while ((ptr = readdir(dir)) != NULL)
    {
        if (strstr(ptr->d_name, "event"))
            eventCount++;
    }
    struct input_absinfo abs, absX[maxE], absY[maxE];
    int fd, i, tmp1, tmp2;
    int screenX, screenY, minCnt = eventCount + 1;
    fdNum = 0;
    for (i = 0; i <= eventCount; i++)
    {
        sprintf(temp, "/dev/input/event%d", i);
        fd = open(temp, O_RDWR);
        if (fd)
        {
            uint8_t *bits = NULL;
            ssize_t bits_size = 0;
            int res, j, k;
            bool itmp1 = false, itmp2 = false, itmp3 = false;
            while (1)
            {
                res = ioctl(fd, EVIOCGBIT(EV_ABS, bits_size), bits);
                if (res < bits_size)
                    break;
                bits_size = res + 16;
                bits = (uint8_t *) realloc(bits, bits_size * 2);
                if (bits == NULL)
                {
                    FILE *fpD = fopen(touchDebug, "w+");
                    fprintf(fpD, "获取事件失败\n");
                    fclose(fpD);
                    while (1)
                    {
                        exit(0);
                    }
                }
            }
            for (j = 0; j < res; j++)
            {
                for (k = 0; k < 8; k++)
                    if (bits[j] & 1 << k && ioctl(fd, EVIOCGABS(j * 8 + k), &abs) == 0)
                    {
                        if (j * 8 + k == ABS_MT_SLOT)
                        {
                            itmp1 = true;
                            continue;
                        }
                        if (j * 8 + k == ABS_MT_POSITION_X)
                        {
                            itmp2 = true;
                            continue;
                        }
                        if (j * 8 + k == ABS_MT_POSITION_Y)
                        {
                            itmp3 = true;
                            continue;
                        }
                    }
            }
            if (itmp1 && itmp2 && itmp3)
            {
                tmp1 = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absX[fdNum]);
                tmp2 = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY[fdNum]);
                if (tmp1 == 0 && tmp2 == 0)
                {
                    origfd[fdNum] = fd;
                    ioctl(fd, EVIOCGRAB, GRAB);
                    if (i < minCnt)
                    {
                        screenX = absX[fdNum].maximum;
                        screenY = absY[fdNum].maximum;
                        minCnt = i;
                    }
                    fdNum++;
                    if (fdNum >= maxE)
                        break;
                }
            }
            else
            {
                close(fd);
            }
        }
    }
    
    if (minCnt > eventCount)
    {
        FILE *fpD = fopen(touchDebug, "w+");
        fprintf(fpD, "获取屏幕驱动失败\n");
        fclose(fpD);
        while (1)
        {
            exit(0);
        } 
    }

    struct uinput_user_dev ui_dev;
    nowfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (nowfd <= 0)
    {
        FILE *fpD = fopen(touchDebug, "w+");
        fprintf(fpD, "打开驱动失败\n");
        fclose(fpD);
        while (1)
        {
            exit(0);
        }
    } 

    memset(&ui_dev, 0, sizeof(ui_dev));
    strncpy(ui_dev.name, genRandomString(rand() % 10 + 5), UINPUT_MAX_NAME_SIZE);
    ui_dev.id.bustype = 0;
    ui_dev.id.vendor = rand() % 10 + 5;
    ui_dev.id.product = rand() % 10 + 5;
    ui_dev.id.version = rand() % 10 + 5;

    ioctl(nowfd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    ioctl(nowfd, UI_SET_EVBIT, EV_ABS);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_X);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_Y);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(nowfd, UI_SET_EVBIT, EV_SYN);
    ioctl(nowfd, UI_SET_EVBIT, EV_KEY);
    ioctl(nowfd, UI_SET_PHYS, genRandomString(rand() % 10 + 5));

    sprintf(temp, "/dev/input/event%d", minCnt);
    fd = open(temp, O_RDWR);
    if (fd)
    {
        struct input_id id;
        if (!ioctl(fd, EVIOCGID, &id))
        {
            ui_dev.id.bustype = id.bustype;
            ui_dev.id.vendor = id.vendor;
            ui_dev.id.product = id.product;
            ui_dev.id.version = id.version;
        }
        uint8_t *bits = NULL;
        ssize_t bits_size = 0;
        int res, j, k;
        while (1)
        {
            res = ioctl(fd, EVIOCGBIT(EV_KEY, bits_size), bits);
            if (res < bits_size)
                break;
            bits_size = res + 16;
            bits = (uint8_t *) realloc(bits, bits_size * 2);
            if (bits == NULL)
            {
                FILE *fpD = fopen(touchDebug, "w+");
                fprintf(fpD, "获取事件失败\n");
                fclose(fpD);
                while (1)
                {
                    exit(0);
                }
            }
        }
        for (j = 0; j < res; j++)
        {
            for (k = 0; k < 8; k++)
                if (bits[j] & 1 << k)
                {
                    if (j * 8 + k == BTN_TOUCH || j * 8 + k == BTN_TOOL_FINGER)
                        continue;
                    ioctl(nowfd, UI_SET_KEYBIT, j * 8 + k);
                }
        } 
    }

    ui_dev.absmin[ABS_MT_POSITION_X] = 0;
    ui_dev.absmax[ABS_MT_POSITION_X] = screenX;
    ui_dev.absmin[ABS_MT_POSITION_Y] = 0;
    ui_dev.absmax[ABS_MT_POSITION_Y] = screenY;

    ui_dev.absmin[ABS_X] = 0;
    ui_dev.absmax[ABS_X] = screenX;
    ui_dev.absmin[ABS_Y] = 0;
    ui_dev.absmax[ABS_Y] = screenY;
    //*retX = screenX;
    //*retY = screenY;

    write(nowfd, &ui_dev, sizeof(ui_dev));

    if (ioctl(nowfd, UI_DEV_CREATE))
    {
        FILE *fpD = fopen(touchDebug, "w+");
        fprintf(fpD, "创建驱动失败\n");
        fclose(fpD);
        while (1)
        {
            exit(0);
        }
    }

    targ tmp[fdNum];
    for (i = 0; i < fdNum; i++)
    {
        tmp[i].fdNum = i;
        tmp[i].S2TX = (float)screenX / (float)absX[i].maximum;
        tmp[i].S2TY = (float)screenY / (float)absY[i].maximum;
        pthread_create(&touchloop, NULL, TypeA, &tmp[i]);
    }
    fdNum++;
    system("chmod 000 -R /proc/bus/input/*");
    system("chmod 311 -R /sys/devices/virtual/input/*/"); 
    
    touch_screen_size = getTouchScreenDimension(fd);
    touch_s = (touch_screen_size.x + touch_screen_size.y) / (screenSize.x + screenSize.y);
}

void Touch_Down(int id, int x, int y) {
    const int num = fdNum - 1;
    if (num >= 0 && num < maxE && id >= 0 && id < maxF)
    {
        // 边界检查确保数组访问安全
        auto& currentFinger = Finger[num][id];
        
        // 设置手指的唯一标识
        currentFinger.id = (num * 2 + 1) * maxF + id;
        
        // 检查缩放系数是否有效，避免除以零错误
        if (touch_s != 0) {
            // 将触摸坐标乘以缩放系数
            currentFinger.x = x * touch_s;
            currentFinger.y = y * touch_s;
        } else {
            // 缩放系数无效，记录日志或进行错误处理
            // 这里只是简单输出错误信息到控制台
     //       std::cerr << "Error: Invalid touch_s value. Division by zero avoided." << std::endl;
        }
        
        // 设置手指大小参数
        currentFinger.size1 = currentFinger.size2 = currentFinger.size3 = 8;
        
        // 标记手指按下状态，并上传触摸事件
        currentFinger.isDown = true;
        Upload();
    }
    else
    {
        // 手指索引或 ID 超出有效范围，记录日志或进行错误处理
        // 这里只是简单输出错误信息到控制台
     ///   std::cerr << "Error: Invalid finger index or ID." << std::endl;
    }
}


void Touch_Move(int id, int x, int y) {
    int num = fdNum - 1;
    if (num >= 0 && num < maxE && id >= 0 && id < maxF) {
        // 设置手指的唯一标识和触摸坐标，进行有效性检查
        Finger[num][id].id = (num * 2 + 1) * maxF + id;
        Finger[num][id].x = x * touch_s;
        Finger[num][id].y = y * touch_s;
        Finger[num][id].size1 = 8;
        Finger[num][id].size2 = 8;
        Finger[num][id].size3 = 8;
        Finger[num][id].isDown = true;
        // 打印缩放因子   
        Upload();
    }
    else
    {
        // 手指索引或 ID 超出有效范围，记录日志或进行错误处理
        // 这里只是简单输出错误信息到控制台
   //     std::cerr << "Error: Invalid finger index or ID." << std::endl;
    }
}


void Touch_Up(int id) {
    usleep(3 * 1000); // 延时 3 毫秒
    int num = fdNum - 1;
    if (num >= 0 && num < maxE && id >= 0 && id < maxF) {
        // 标记手指抬起状态，并上传触摸事件
        Finger[num][id].isDown = false;
        Finger[num][id].isUp = true;
        Upload();
    } else {
        // 手指索引或 ID 超出有效范围，记录日志或进行错误处理
        // 这里只是简单输出错误信息到控制台
        // std::cerr << "Error: Invalid finger index or ID." << std::endl;
    }
}


void Touch_setOrientation(int orientation_) {
    ::Touch_orientation = orientation_;
}