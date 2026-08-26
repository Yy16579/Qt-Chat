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
	// 参数说明：fullPacket = 完整原始包（含外层包头，转发场景直接使用）
	//           dataBody  = 数据体（剥离外层包头后的业务数据）
	//           descriptor = 来源客户端的 fd 标识
	void handleMessage(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 消息包：私聊在线直转；私聊离线/群聊 → 池任务
	void handleLoginRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);		// 登录请求：解析账密 → LoginTask 验证（结果回 onLoginVerified）
	void handleLogout(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 注销请求：解绑用户路由映射（纯内存，主线程直干）
	void handleRegisterRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);	// 注册请求
	void handleDbQuery(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 数据库查询请求
	void handleHeartbeat(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 心跳包：保活（主线程直答——DB 再卡心跳不受影响，这正是池化的收益）
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
	void onDbChecked(bool ok, const QString& error);										// DbCheckTask：启动自检结果打印
	void onLoginVerified(int descriptor, bool ok, int uid, const QByteArray& snapshot);		// LoginTask：拼响应/互踢/绑路由/拉离线
	void onGroupMembersLoaded(int descriptor, int sendId, int groupId, const QList<int>& memberIds, 
		const QByteArray& fullPacket, const QByteArray& dataBody);							// GroupMembersTask：逐成员分发
	void onOfflineLoaded(int descriptor, int uid, const QList<QByteArray>& contents);		// LoadOfflineTask：逐条推送
	// ======================================================================================================

private:
	//成员变量
	int m_port;			//监听的端口号

	QHash<int, TcpSocket*> m_fdSocketMap;		// 连接表：fd → socket（用于连接管理、广播、断开清理）
	QHash<int, TcpSocket*> m_uidSocketMap;		// 路由表：uid → socket（用于精准转发）（客户端成功登录时添加）

	QTimer* m_checkTimer;		//心跳超时扫描定时器
	
	QHash<PacketType, void (TcpServer::*)(const QByteArray&, const QByteArray&, int)> m_handlers;		// 业务表

	QThreadPool* m_taskPool;		// 线程池（4 线程常驻：MySQL 慢查询全部外包于此）
	TaskSignals* m_taskSignals;		// 池任务结果回传器（池线程 emit → 队列投递 → 主线程结果槽）
};

