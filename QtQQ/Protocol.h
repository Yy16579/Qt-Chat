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

// ===== 消息可靠性（推拉模型）常量 =====
// msgId 定长：uid后3位 + 毫秒时间戳后10位（13位十进制，消息全局唯一身份证）
// 用途：发送端重传的幂等键（服务端 tab_msg.msg_id 唯一索引，重复 INSERT 被数据库静默忽略）
constexpr int MSGID_LEN = 13;

// 账本定长：10位十进制 = 客户端已收到的最大消息 id（tab_msg 自增主键）
// 用途：拉取游标（WHERE id > 账本）+ 隐式批量确认 + 断点续传，客户端持久化于 QSettings
constexpr int CURSOR_LEN = 10;

// Pull 分页大小：单次拉取最大条数（客户端收满自动续拉）
constexpr int PULL_PAGE_SIZE = 20;

// 包类型枚举（按数据流方向 + 功能域排序：上行 → 下行信令 → 下行数据 → 其他）
enum class PacketType : quint16 {
    // === 上行通道（客户端 → 服务器）0x01xx ===
    Message         = 0x0100,   // 消息上行（唯一数据上行通道）
                                // 数据体 = [msgId 13B][群标志1B][发送者5B][接收者4~5B][类型1B][内容...]
                                // 服务器收到只入库（INSERT IGNORE 幂等），从不转发消息本体
    PullRequest     = 0x0101,   // 拉取请求（数据体 = 账本 10B = 我已收到的最大消息 id）
                                // 触发时机：收到敲门 / 登录成功 / 心跳对账发现落后
    Heartbeat       = 0x0102,   // 心跳包（数据体 = 账本 10B，服务器对账用）
                                // 对账：该用户最新消息 id > 账本 → 回敲门（消息本体绝不搭心跳顺风车）
    LoginRequest    = 0x0103,   // 登录请求（数据体 = 账号|密码）
    RegisterRequest = 0x0104,   // 注册请求
    Logout          = 0x0105,   // 注销

    // === 下行信令通道（服务器 → 客户端）0x02xx（全部 fire-and-forget，丢失无后果） ===
    MessageAck      = 0x0201,   // 投递确认（数据体 = msgId 13B）
                                // 唯一例外：驱动发送端停止重传（入库成功即回）
    MsgNotify       = 0x0202,   // 敲门（数据体 = 空或新消息条数）：提醒"你有新消息，来 Pull"
                                // 纯信令，丢失无后果（心跳对账、登录自动 Pull 双兜底）
    HeartbeatResponse = 0x0203, // 心跳响应
    LoginResponse   = 0x0204,   // 登录响应
    RegisterResponse= 0x0205,   // 注册响应
    KickOut         = 0x0206,   // 踢下线通知

    // === 下行数据通道（服务器 → 客户端）0x03xx ===
    PullResponse    = 0x0301,   // 拉取响应（消息本体唯一出口！）
                                // 数据体 = [新账本 10B][条数 1B] + N × [msgId 13B | 消息载荷]
                                // 消息载荷 = [群标志1B][发送者5B][接收者4~5B][类型1B][内容...]（与上行同构）
                                // 客户端收满 PULL_PAGE_SIZE 条自动续拉（发新 PullRequest 带新账本）

    // === 其他 0x04xx / 0xFFxx ===
    DbQuery         = 0x0401,   // 数据库查询请求
    DbQueryResult   = 0x0402,   // 数据库查询结果
    ErrorResponse   = 0xFF02    // 错误响应
};
