# Qt-Chat —— C++/Qt6 即时通讯系统

独立开发的全栈 IM 项目：**Qt6 桌面客户端 + 自研二进制协议 + 控制台服务端**，重点攻坚 IM 核心难题——**消息可靠性（不丢、不重、不乱序）**。

## 功能一览

- 用户登录 / 注册（账号或工号双方式验证）
- 私聊 / 群聊（服务端群消息扇出投递）
- 文本 + 表情消息（QtWebEngine + HTML 模板渲染气泡）
- 好友 / 部门树通讯录（登录时服务端快照一次性下发）
- 无边框主窗口 + QSS 动态换肤 + 系统托盘
- 本地历史消息持久化（SQLite 按账号分库）
- 断线自动重连 + 重登 + 未确认消息补发

## 架构总览

```
┌──────────────────┐   自定义二进制协议   ┌──────────────────────┐
│   Qt6 客户端      │◄──────────────────►│      服务端            │
│                  │   包头[类型|长度]+载荷 │  （QCoreApplication）  │
│ UserLogin        │                    │                      │
│ CCMainWindow     │                    │  主线程：所有 TCP 连接 │
│ TalkWindowShell  │                    │  的收发/切包/取号      │
│ TcpClient(单例)   │                    │                      │
│ TalkSessionStore │                    │  QThreadPool：数据库  │
│  (SQLite 本地库)  │                    │  任务（expiry -1）    │
└──────────────────┘                    │  DbConnPool：每线程    │
                                        │  一条 MySQL 连接       │
                                        │        MySQL          │
                                        └──────────────────────┘
```

**线程模型**：服务端 socket 收发留在主线程（不被慢 SQL 阻塞），数据库操作作为任务投递线程池；连接池按**线程 ID 登记命名连接**（`pool_<tid>`），杜绝 QSqlDriver 跨线程使用的未定义行为。

## 核心技术：消息可靠性三板斧

| 目标 | 机制 |
|---|---|
| **不丢** | 消息携带全局唯一 `msgId`，服务端入库成功回 ACK；超时未确认按 3/6/12s 有界重传；断线重连重登成功后未确认消息全量补发 |
| **不重** | 双端幂等闸门：服务端 `INSERT IGNORE` + `uk(recv_id, msg_id)` 唯一索引；客户端 SQLite `msgId` 唯一索引 + `INSERT OR IGNORE`，重拉消息静默吸收不重复渲染 |
| **不乱序** | 会话内服务端分配单调递增 `seq`；客户端维护接收账本，超前消息进乱序缓冲区；检测到空洞发定点补拉请求（500ms 全局扫描定时器）；补拉次数耗尽触发**跳洞机制**——排空缓冲、账本前移，防止单条永久丢失的消息卡死整个会话 |

配套机制：10s 心跳保活（超时主动断开）、指数退避重连、断线意图分流（登出/被踢不重连）、重连后账本镜像对齐。

## 技术栈

**客户端**：C++17 / Qt6（Widgets、Network、WebEngineWidgets）/ CMake / SQLite
**服务端**：C++17 / Qt6（Network、Sql、Concurrent）/ CMake / MySQL / QThreadPool
**工程化**：QSS 样式分离、Meyers 单例、RAII（QMutexLocker 限定临界区）、Qt 对象树父子内存管理、`.gitattributes` 行尾规范化、windeployqt 自动部署（POST_BUILD）

## 目录结构

```
QtQQ/                  # 客户端
├── TcpClient.*        # 网络层单例：收发/心跳/重连/ACK重传/账本/乱序缓冲
├── TalkSessionStore.* # 本地存储：msg_<empID>.db 按账号分库，幂等入库
├── CCMainWindow.*    # 无边框主窗口、换肤、托盘
├── TalkWindow*.*/    # 聊天窗口（WebEngine 气泡渲染）
├── basicwindow.*     # 所有窗口的基类：无边框/换肤统一入口
└── Resources/        # QSS 样式 + 图片资源
QtQQ_Server/          # 服务端
├── TcpServer.*       # 主线程连接管理、seq 号池分配
├── TcpSocket.*       # 单连接收发/切包
├── ServerTask.*      # 登录/投递/群扇出/补拉等池任务 + DbConnPool
└── config/           # config.ini.example
```

## 构建与运行

环境：Qt 6.x、CMake 3.21+、MSVC 2022（`/utf-8` 已配置）、MySQL 5.7+

```bash
# 1. 服务端导入数据库脚本（建库建表），两端分别：
# 2. 复制配置模板为实际配置
cp QtQQ/config/config.ini.example QtQQ/config/config.ini
cp QtQQ_Server/config/config.ini.example QtQQ_Server/config/config.ini
# 3. 编辑 config.ini：[Database] 节填 MySQL 连接参数，[Tcp] 节填服务器地址

# 4. 构建（客户端 / 服务端 各自）
cmake --preset <preset>        # 见 CMakePresets.json
cmake --build --preset <preset>
```

Debug 构建通过 POST_BUILD 自动执行 windeployqt 部署 Qt 运行时。

## 协议格式

包头定长（包类型 2B + 数据长度 4B），载荷按类型自定义二进制结构；消息体含 `msgId`（全局唯一，幂等键）与会话 `seq`（服务端号池分配，连续性校验依据）。心跳/ACK 为小体积高频包，通讯录快照等大块数据单次全量下发。

---

> 项目为个人学习性质的全栈实现，重点在协议设计与可靠性机制的工程落地，欢迎交流。
