#pragma once

#include <linux/input.h>
#include <vector>
#include <functional>
#include "VectorStruct.h"

namespace Touch {
    struct touchObj {
        Vector2 pos{};
        int id = 0;
        bool isDown = false;
    };

    struct Device {
        int fd;
        float S2TX;
        float S2TY;
        input_absinfo absX, absY;
        touchObj Finger[10];

        Device() { memset((void *) this, 0, sizeof(*this)); }
    };

    bool Init(const Vector2 &s, bool p_readOnly);

    void Close();

    void Down(float x, float y);

    void Move(float x, float y);

    void Up();

    void Move(touchObj *touch, float x, float y);

    void Upload();

    void SetCallBack(const std::function<void(std::vector<Device> *)> &cb);

    Vector2 Touch2Screen(const Vector2 &coord);

    // 屏幕像素(当前方向) -> 触摸屏原生坐标，Touch2Screen 的逆运算
    Vector2 Screen2Touch(const Vector2 &screen);

    // 在真实触摸屏上借一个空闲 MT slot 注入一次点击(按下 hold_ms 毫秒后抬起)。会阻塞 hold_ms，别在渲染线程里直接调
    enum InjectResult {
        INJECT_OK = 0,
        INJECT_NOT_READY = -1,
        INJECT_NO_SLOT = -2,
        INJECT_BAD_POS = -3,
        INJECT_DOWN_FAILED = -4,
        INJECT_UP_FAILED = -5,
    };
    int InjectTap(float screen_x, float screen_y, int hold_ms);
    const char *InjectResultText(int result);

    Vector2 GetScale();

    void setOrientation(int orientation);

    void setOtherTouch(bool p_otherTouch);
}
