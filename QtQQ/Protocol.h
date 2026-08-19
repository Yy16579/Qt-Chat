#pragma once

#include <QtGlobal>


// 包结构定义
// ┌──────┬──────┬──────┬────────┬────────────┐
// │魔数        │版本        │包类型      │数据长度        │数据体                  │
// │2B          │1B          │2B          │2B              │NB                      │
// └──────┴──────┴──────┴────────┴────────────┘
// 偏移:  0          2             3             5                 7
// 总头部 = 7 字节


// 外层包固定头部大小
constexpr int PACKET_HEADER_SIZE = 7;
constexpr quint16 PACKET_MAGIC = 0x5A5A;
constexpr quint8 PACKET_VERSION = 1;

// 包类型枚举
enum class PacketType : quint16 {
    // === 消息类 (0x01xx) - 数据体为文本格式 ===
    Message         = 0x0100,   // 通用消息包（兼容现有消息）

    // === 认证类 (0x02xx) - 数据体为JSON文本 ===
    LoginRequest    = 0x0201,   // 登录请求
    LoginResponse   = 0x0202,   // 登录响应
    RegisterRequest = 0x0203,   // 注册请求
    RegisterResponse= 0x0204,   // 注册响应
    Logout          = 0x0205,   // 注销
    KickOut         = 0x0206,   // 踢下线通知

    // === 数据库类 (0x03xx) - 数据体为二进制格式 ===
    DbQuery         = 0x0301,   // 数据库查询请求
    DbQueryResult   = 0x0302,   // 数据库查询结果

    // === 状态类 (0x05xx) - 无数据体 ===
    Heartbeat       = 0x0501,   // 心跳包
    HeartbeatResponse = 0x0502, // 心跳响应

    // === 系统类 (0xFFxx) - 数据体为文本格式 ===
    ErrorResponse   = 0xFF02    // 错误响应
};
