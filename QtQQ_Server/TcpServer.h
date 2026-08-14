#pragma once

#include "TcpSocket.h"

#include <QTcpServer>
#include <QHash>


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

signals:
	//信号
	

private slots:
	//槽函数
	void onPacketReady(const QByteArray& data, int descriptor);		//接收完整数据包，处理业务层逻辑
	void onClientDisconnected(int descriptor);						//客户端断开连接

private:
	//成员变量
	int m_port;			//监听的端口号

	QHash<int, TcpSocket*> m_fdSocketMap;		// 连接表：fd → socket（用于连接管理、广播、断开清理）
	QHash<int, TcpSocket*> m_uidSocketMap;		// 路由表：uid → socket（用于精准转发）（客户端成功登录时添加）

};

