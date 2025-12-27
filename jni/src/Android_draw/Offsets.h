// Offsets.h - 所有可能随引擎更新变化的偏移集中管理
// 引擎更新后只需修改此文件
#pragma once
#include <cstdint>

namespace Offsets {

    // ===== 特征值（引擎更新必查）=====
    // 用于在BSS段搜索定位矩阵和数组
    constexpr int MATRIX_FEATURE_1 = 970037390;   // 矩阵特征码1
    constexpr int MATRIX_FEATURE_2 = 970061201;   // 矩阵特征码2
    constexpr int ARRAY_FEATURE_1 = 16384;        // 数组特征值
    constexpr int ARRAY_FEATURE_2 = 257;          // 数组验证值
    
    // ===== 类名指针链（引擎更新必查）=====
    // 对象 -> 类名字符串的解析路径
    // namezfcz = getPtr64(getPtr64(getPtr64(getPtr64(getPtr64(对象 + 0xe0)+0x0)+0x8)+0x20)+0x20)+0x8
    constexpr uintptr_t CLASSNAME_BASE = 0xe0;    // 类名链起始
    constexpr uintptr_t CLASSNAME_STEP1 = 0x0;    // 第一级偏移
    constexpr uintptr_t CLASSNAME_STEP2 = 0x8;    // 第二级偏移
    constexpr uintptr_t CLASSNAME_STEP3 = 0x20;   // 第三级偏移
    constexpr uintptr_t CLASSNAME_STEP4 = 0x20;   // 第四级偏移
    constexpr uintptr_t CLASSNAME_FINAL = 0x8;    // 最终偏移
    constexpr uintptr_t CLASSNAME_LEN = 0x8;      // 类名长度偏移
    
    // ===== 矩阵指针链 =====
    // Matrix = getPtr64(getPtr64(libbase + MatrixOffset) + 0x98) + 0x380
    // Matrix = getPtr64(getPtr64(getPtr64(libbase + MatrixOffset) + 0x98) + 0x10) + 0x180
    constexpr uintptr_t MATRIX_PTR_1 = 0x98;      // 矩阵指针第一级
    constexpr uintptr_t MATRIX_PTR_2 = 0x10;      // 矩阵指针第二级
    constexpr uintptr_t MATRIX_OFFSET_1 = 0x380;  // 矩阵偏移（用于相机位置）
    constexpr uintptr_t MATRIX_OFFSET_2 = 0x180;  // 矩阵偏移（用于视图矩阵）
    constexpr uintptr_t MATRIX_CAMERA = 0x290;    // 相机坐标偏移
    constexpr uintptr_t MATRIX_FEATURE_OFFSET = 0x2a8;  // 矩阵特征码位置
    
    // ===== 数组搜索相关 =====
    constexpr uintptr_t ARRAY_CHECK_OFFSET = 0x70;   // 数组自引用检查偏移
    constexpr uintptr_t ARRAY_SELF_REF = 0x78;       // 数组自引用值偏移
    constexpr uintptr_t ARRAY_ADDR_OFFSET = 0x88;    // 数组地址偏移
    constexpr uintptr_t ARRAY_PREV_CHECK = 0x8;      // 数组前置检查偏移
    constexpr uintptr_t ARRAY_ELEMENT_SIZE = 0x8;    // 数组元素大小（64位指针）
    
    // ===== 对象结构 =====
    constexpr uintptr_t OBJ_COORD_PTR = 0x40;     // 对象 -> 坐标结构体指针
    
    // 主坐标（Transform）
    constexpr uintptr_t COORD_X = 0x50;           // 坐标 X
    constexpr uintptr_t COORD_Z = 0x54;           // 坐标 Z（高度）
    constexpr uintptr_t COORD_Y = 0x58;           // 坐标 Y
    
    // 备用坐标（红夫人等特殊用途）
    constexpr uintptr_t COORD_ALT_X = 0xE0;       // 备用坐标 X
    constexpr uintptr_t COORD_ALT_Z = 0xE4;       // 备用坐标 Z
    constexpr uintptr_t COORD_ALT_Y = 0xE8;       // 备用坐标 Y
    
    // ===== 状态判断 =====
    constexpr uintptr_t OBJ_CAMP = 0x150;         // 阵营/角色类型偏移
    constexpr float CAMP_VALUE = 450.0f;          // 角色阵营值（求生者/监管者）
    
    constexpr uintptr_t OBJ_GHOST = 0x78;         // 鬼魂/隐身状态偏移
    constexpr int GHOST_VALUE = 65150;            // 鬼魂状态值
    
    constexpr uintptr_t OBJ_SELF = 0xaa;          // 自身判断偏移
    
    constexpr uintptr_t OBJ_PD2 = 0x298;          // 预留偏移（当前未使用）
    
    // ===== 辅助宏 =====
    // 获取类名指针的完整链
    #define GET_CLASSNAME_PTR(obj) \
        (getPtr64(getPtr64(getPtr64(getPtr64(getPtr64((obj) + Offsets::CLASSNAME_BASE) + Offsets::CLASSNAME_STEP1) + Offsets::CLASSNAME_STEP2) + Offsets::CLASSNAME_STEP3) + Offsets::CLASSNAME_STEP4) + Offsets::CLASSNAME_FINAL)

} // namespace Offsets
