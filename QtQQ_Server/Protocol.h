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

// ===== 消息可靠性（推拉模型 + seq 会话序号）常量 =====
// msgId 定长：uid后3位 + 毫秒时间戳后10位（13位十进制，消息全局唯一身份证）
// 用途：发送端重传的幂等键（服务端 uk_recv_msg(recv_id,msg_id) 唯一索引，重复 INSERT 被数据库静默忽略）
//       + MessageAck 回执凭据（发送端 pending 表按 msgId 记账，收 ACK 停重传定时器）
constexpr int MSGID_LEN = 13;

// seq 定长：10位十进制 = 会话内连续序号（服务端统一分配：私聊按会话对 / 群按整群一条流水，主线程号池 ++ 取号）
// 用途：会话内排序权威 + 拉取游标（WHERE seq > 游标）+ 空洞检测（收包校验 seq == 账本+1）
// 仅用于下行方向（PullRequest/Heartbeat 游标表 + PullResponse 消息字段）；上行 Message 已删除 seq 字段
// 号池重启自愈：启动时 SELECT MAX(seq) GROUP BY 从 DB 重建断点（内存管速度，DB 管记忆）
constexpr int SEQ_LEN = 10;

// convId 定长：5位十进制补零 = 会话键（私聊 = 发送者 uid / 群聊 = 群号）
// 用途：消息归属哪段对话（账本/游标按会话独立记账，tab_msg.conv_id 提列建索引）
constexpr int CONV_LEN = 5;

// Pull 分页大小：单次拉取单会话最大条数（服务端判满页敲门，驱动客户端续拉）
constexpr int PULL_PAGE_SIZE = 20;

// 包类型枚举（按数据流方向 + 功能域排序：上行 → 下行信令 → 下行数据 → 其他）
enum class PacketType : quint16 {
    // === 上行通道（客户端 → 服务器）0x01xx ===
    Message         = 0x0100,   // 消息上行（唯一数据上行通道）
                                // 数据体 = [msgId 13B][群标志1B][发送者5B][接收者（私聊5B/群聊4B）][类型1B][内容...]
                                // seq 由服务端统一分配（单 TCP 流保序 + 主线程串行处理 = 到达顺序即取号顺序），
                                // 服务器收到后三查（recentAcked/inFlight）→ 号池取号 → 入库（INSERT IGNORE 幂等），从不转发消息本体
    PullRequest     = 0x0101,   // 拉取请求（数据体 = 游标表 = 每会话独立报进度）
                                // 数据体 = [会话数 2B] + N × [convId 5B][游标 10B]；"00"（count=0）= 无会话要拉
                                // 空包（<2B）协议违规，服务端直接丢弃；全量拉取能力已移除（微信式：换设备由登录响应账本镜像同步进度）
                                // 触发时机：收到敲门 / 登录成功 / 空洞定点补拉（单会话；心跳对账发现落后即走敲门链路，不重复列）
    Heartbeat       = 0x0102,   // 心跳包（数据体 = 游标表，与 PullRequest 同构）
                                // 对账：任一会话 服务端最新 seq > 游标 → 回敲门（消息本体绝不搭心跳顺风车）
                                // 进度上报：游标 upsert 至 tab_ledger（渲染落账后批次级主动补发，不等 10s 周期）
                                // ★ 允许空数据体 = 纯保活（与 PullRequest 的刻意差异：心跳可空包，拉取不可）
                                // 反向补查：服务端有、客户端没报的会话（新会话积压）也算落后 → 敲门
    LoginRequest    = 0x0103,   // 登录请求（数据体 = 账号|密码）
    RegisterRequest = 0x0104,   // 注册请求
    Logout          = 0x0105,   // 注销

    // === 下行信令通道（服务器 → 客户端）0x02xx（全部 fire-and-forget，丢失无后果） ===
    MessageAck      = 0x0201,   // 投递确认（数据体 = msgId 13B）
                                // 唯一例外：驱动发送端停止重传（入库成功即回）
    MsgNotify       = 0x0202,   // 敲门（数据体 = 空）：提醒"你有新消息，来 Pull"
                                // 纯信令，丢失无后果（心跳对账、登录自动 Pull 双兜底）
    HeartbeatResponse = 0x0203, // 心跳响应
    LoginResponse   = 0x0204,   // 登录响应
                                // 成功 = "1" + uid(5B) + 账本镜像表(count2B + N×[convId5B+游标10B]) + 通讯录JSON
                                //（账本镜像 = tab_ledger 持久化的接收进度：客户端镜像后拉"上次进度之后"的消息，
                                //  离线未读可见、历史不重拉——换设备/同设备统一此语义；失败 = "0"）
    RegisterResponse= 0x0205,   // 注册响应
    KickOut         = 0x0206,   // 踢下线通知

    // === 下行数据通道（服务器 → 客户端）0x03xx ===
    PullResponse    = 0x0301,   // 拉取响应（消息本体唯一出口！）
                                // 数据体 = JSON {"count":N,"msgs":[{"convId":"..","seq":"..","msgId":"..","payload":"base64"}...]}
                                // convId/seq/msgId 字符串承载（quint64 防精度损失）；payload = 上行载荷原文 base64
                                // 客户端逐条校验 seq == 账本[convId]+1：命中渲染落账 / 落后丢弃（重复）/
                                // 超前发现空洞 → 该消息进乱序缓冲区 + 单会话定点补拉（游标=当前账本，500ms×5 次）
                                // 续拉由服务端驱动：满页（count ≥ PULL_PAGE_SIZE）敲门，客户端重新全表拉取

    // === 其他 0x04xx / 0xFFxx ===
    DbQuery         = 0x0401,   // 数据库查询请求
    DbQueryResult   = 0x0402,   // 数据库查询结果
    ErrorResponse   = 0xFF02    // 错误响应
};
