#pragma once

#include "TcpSocket.h"
#include "Protocol.h"

#include <QTcpServer>
#include <QHash>
#include <QTimer>


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
	void handleMessage(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 消息包：私聊/群聊转发
	void handleLoginRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);		// 登录请求：绑定uid，建立路由映射
	void handleLogout(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 注销请求：解绑用户路由映射
	void handleRegisterRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);	// 注册请求
	void handleDbQuery(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 数据库查询请求
	void handleHeartbeat(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor);			// 心跳包：保活
	// ======================================================================================================

private:
	// 离线消息推送
	void pushOfflineMessages(int uid, TcpSocket* socket);

private:
	// 数据包 包头拼接与发送
	void sendPacket(quint16 packetType, const QByteArray& dataBody, TcpSocket* target);

private slots:
	//槽函数
	void onPacketReady(const QByteArray& data, int descriptor);		// 解析数据包，分发业务
	void onClientDisconnected(int descriptor);						// 客户端断开连接

	void onCheckTimeout();		//心跳超时扫描

private:
	//成员变量
	int m_port;			//监听的端口号

	QHash<int, TcpSocket*> m_fdSocketMap;		// 连接表：fd → socket（用于连接管理、广播、断开清理）
	QHash<int, TcpSocket*> m_uidSocketMap;		// 路由表：uid → socket（用于精准转发）（客户端成功登录时添加）

	QTimer* m_checkTimer;		//心跳超时扫描定时器
	QHash<PacketType, void (TcpServer::*)(const QByteArray&, const QByteArray&, int)> m_handlers;		// 业务表
};

