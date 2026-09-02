#pragma once

#include "TcpSocket.h"
#include "Protocol.h"
#include "ServerTask.h"		

#include <QTcpServer>
#include <QHash>
#include <QTimer>
#include <QThreadPool>


// 线程模型：主线程负责 IO 以及 解析/校验/路由/发包（快业务），凡碰 MySQL 的慢业务打包成池任务外包
// 结果经 TaskSignals 信号回到下方"结果槽"继续处理
// 消息流（推拉模型）：消息上行 → 入库（池）→ 回投递 ACK + 敲门在线收件人 → 收件人发 Pull → 拉取响应（消息本体唯一出口）
class TcpServer  : public QTcpServer
{
	Q_OBJECT

public:
	TcpServer(int port);
	~TcpServer();

public:
	bool startListen();		//开启监听状态

protected:
	//客户端发来连接时 Qt 自动调用
	void incomingConnection(qintptr socketDescriptor) override;		

private:
	void registerHandlers();		//注册业务表

	// 业务处理函数 =========================================================================================
	// 参数说明：fullPacket = 完整原始包（含外层包头）
	//           dataBody  = 数据体（剥离外层包头后的业务数据）
	//           descriptor = 来源客户端的 fd 标识
	void handleMessage(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 消息上行：解析 msgId/seq/载荷 → 投 StoreMsgTask / GroupMembersTask
	void handlePullRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);		// 拉取请求：解析账本 → 投 PullTask
	void handleHeartbeat(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 心跳包：带游标表对账
	void handleLoginRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);		// 登录请求：解析账密 → LoginTask 验证
	void handleRegisterRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);	// 注册请求
	void handleLogout(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 注销请求：解绑用户路由映射（纯内存，主线程直干）
	// ======================================================================================================

private:
	//数据包 包头拼接与发送
	void sendPacket(quint16 packetType, const QByteArray& dataBody, TcpSocket* target);

private slots:
	//槽函数
	void onPacketReady(const QByteArray& data, int descriptor);		// 解析数据包，分发业务
	void onClientDisconnected(int descriptor);						// 客户端断开连接

	void onCheckTimeout();		//心跳超时扫描

	// 池任务结果槽 =========================================================================================
	// 公共模式：开头查 m_fdSocketMap 判空——任务在途期间客户端可能已断开，结果作废（竞态防护）
	void onDbChecked(bool ok, const QString& error);											// DbCheckTask：启动自检结果打印
	void onLoginVerified(int descriptor, bool ok, int uid, const QByteArray& snapshot,
						const QHash<int, quint64>& maxSeqs);										// LoginTask：回发响应/同步账本/互踢/绑路由
	void onMsgStored(int descriptor, const QString& msgId, bool ok, 
						int recvId, int convId, quint64 seq);									// StoreMsgTask：回 ACK / 更新会话消息最大 seq / 敲门
	void onGroupMsgStored(int descriptor, const QString& msgId, bool ok,
						const QList<int>& memberIds, int convId, quint64 seq);					// GroupMembersTask：回 ACK / 更新会话消息最大 seq / 敲门
	void onPullLoaded(int descriptor, const QByteArray& dataBody);								// PullTask：消息下发（JSON 数据体纯转发）
	// ======================================================================================================

private:
	//成员变量
	int m_port;			//监听的端口号

	QHash<int, TcpSocket*> m_fdSocketMap;		// 连接表：fd → socket（用于连接管理、广播、断开清理）
	QHash<int, TcpSocket*> m_uidSocketMap;		// 路由表：uid → socket（用于敲门投递）（客户端成功登录时添加）

	QTimer* m_checkTimer;		//心跳超时扫描定时器
	
	QHash<PacketType, void (TcpServer::*)(const QByteArray&, const QByteArray&, int)> m_handlers;		// 业务表

	QThreadPool* m_taskPool;		// 线程池（4 线程常驻：MySQL 慢查询全部外包于此）
	TaskSignals* m_taskSignals;		// 池任务结果回传器（池线程 emit → 队列投递 → 主线程结果槽）

	QHash<int, QHash<int, quint64>> m_convMaxSeq;	//总帐本：uid → (会话ID → 该会话最新 seq)
};

