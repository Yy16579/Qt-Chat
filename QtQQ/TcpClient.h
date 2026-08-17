#pragma once

#include "Protocol.h"

#include <QObject>
#include <QTcpSocket>


//连接地址和端口从 config.ini 的 [Tcp] 节读取
class TcpClient : public QObject
{
	Q_OBJECT

public:
	static TcpClient& getInstance();

	void connectToServer();			//创建套接字，向服务端发起连接

public:
	// 数据包打包发送接口 ===================================================================================
	// 拼接内部数据
	// ===== 消息类接口 =====
	// 发送消息包
	void sendMessage(bool groupFlag, int sendID, int recvID, int msgType, const QString& msg, const QString& file = "");

	// ===== 认证类接口 =====
	// 发送登录请求
	void sendLoginRequest(const QString& account, const QString& password);

	// 发送注销请求并断开连接
	void sendLogout();

	// 发送注册请求
	void sendRegisterRequest(const QString& account, const QString& password, const QString& name);

	// ===== 数据库类接口 =====
	// 发送数据库查询请求（SQL + 参数）
	void sendDbQuery(const QString& sql, const QStringList& params = {});

	// ===== 状态类接口 =====
	// 发送心跳包
	void sendHeartbeat();

private:
	// 拼接包头并发送（所有上层接口最终调用这个）
	void sendPacket(quint16 packetType, const QByteArray& dataBody);
	// =================================================================================================================

private:
	// 解析数据包，分发业务
	void onProcessPacket(const QByteArray& packet);

private:
	TcpClient();
	~TcpClient();
	TcpClient(const TcpClient&) = delete;
	TcpClient& operator=(const TcpClient&) = delete;

signals:
	//信号
	void signalErrorOccurred(const QString& errorMsg);		//通用错误提示信号

	// 数据包业务分发信号 ======================================================================================
	void signalMessageReceived(int groupFlag, int sendId, int recvId, int msgType, const QString& msg);
	void signalLoginResponse(bool result, int empID);
	void signalKickedOut();
	// =================================================================================================================

private slots:
	//槽函数
	void onReadyRead();			//响应 readyRead 信号，负责 接收数据包 粘包切包 处理

private:
	//成员变量
	QTcpSocket* m_tcpClientSocket;

	QByteArray m_buffer;	//数据包接收缓冲区
};
