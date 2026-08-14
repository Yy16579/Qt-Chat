#include "TcpServer.h"

#include <QDebug>


TcpServer::TcpServer(int port)
	: m_port(port)
{}

TcpServer::~TcpServer()
{
	// 清理连接表
	for (QHash<int, TcpSocket*>::iterator it = m_fdSocketMap.begin(); it != m_fdSocketMap.end(); it++) {
		it.value()->disconnectFromHost();
	}
	// 

}

bool TcpServer::startListen() {
	//host 固定为本机，监听所有网卡 
	if (this->listen(QHostAddress::Any, this->m_port)) {
		qDebug() << QStringLiteral("服务端监听端口 %1 成功！").arg(this->m_port);
		return true;
	}
	else {
		qDebug() << QStringLiteral("服务端监听端口 %1 失败！").arg(this->m_port);
		return false;
	}
}

void TcpServer::incomingConnection(qintptr socketDescriptor) {
	// socketDescriptor 是操作系统底层的 socket 文件描述符（fd） ，是一个整数
	// Qt 把底层 fd 交给你，你自己决定怎么包装它

	qDebug() << QStringLiteral("新的连接： fd=%1，总连接数：%2").arg(socketDescriptor).arg(m_fdSocketMap.size() + 1) << '\n';

	// 创建通信 socket 对象
	TcpSocket* socket = new TcpSocket(this);
	socket->setSocketDescriptor(socketDescriptor);
	socket->initSocket();

	// 连接信号槽
	connect(socket, &TcpSocket::signalPacketReady, this, &TcpServer::onPacketReady);
	connect(socket, &TcpSocket::signalClientDisconnected, this, &TcpServer::onClientDisconnected);

	// 添加至连接表
	this->m_fdSocketMap.insert(socketDescriptor, socket);
}

void TcpServer::onPacketReady(const QByteArray& data, int descriptor) {
	//	TcpServer（业务层）
	//		├─ 接收完整包
	//		├─ 解析外层包头（魔数、版本、包类型、数据长度）
	//		├─ 校验包头合法性（魔数、版本、长度）
	//		├─ 提取数据体
	//		├─ 按包类型分发到对应处理函数
	//		└─ 各处理函数执行具体业务（消息转发 / 登录 / 数据库查询等）



	
}

void TcpServer::onClientDisconnected(int descriptor) {
	// 1. 取出通信 socket 对象
	TcpSocket* socket = m_fdSocketMap.value(descriptor);
	
	// 2. 如果已登录，从路由表移除
	if (socket && socket->getUid() != -1) {
		m_uidSocketMap.remove(socket->getUid());
		qDebug() << QStringLiteral("用户 uid=%1 已从路由表移除").arg(socket->getUid());
	}

	// 3. 从连接表移除
	m_fdSocketMap.remove(descriptor);

	// 4. 安全释放 socket 对象
	if (socket) {
		socket->deleteLater();
	}

	qDebug() << QStringLiteral("客户端断开 fd=%1，剩余连接数：%2")
		.arg(descriptor).arg(m_fdSocketMap.size()) << '\n';
}
