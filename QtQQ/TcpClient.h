#pragma once

#include "Protocol.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>



// 断线意图
enum class DisconnectIntent {
	None,       //意外断线（服务端崩溃/网络故障/心跳超时abort）→ 触发自动重连
	Logout,     //用户主动登出 → 不重连
	KickOut     //被踢下线 → 不重连
};


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
	// 网络重连调度
	void startReconnectTimer();		//启动重连定时器

private:
	// 解析数据包，分发业务
	void onProcessPacket(const QByteArray& packet);

private:
	TcpClient();
	~TcpClient();
	TcpClient(const TcpClient&) = delete;
	TcpClient& operator=(const TcpClient&) = delete;

signals:
	// 信号
	void signalErrorOccurred(const QString& errorMsg);		//通用错误提示信号
	
	void signalReconnectStarted();		//重连流程启动信号（UI 提示用）
	void signalReconnected();			//重连+重登成功，会话恢复信号（UI 恢复提示用）


	// 数据包业务分发信号 ======================================================================================
	void signalMessageReceived(int groupFlag, int sendId, int recvId, int msgType, const QString& msg);
	void signalLoginResponse(bool result, int empID);
	void signalKickedOut();
	// =================================================================================================================

private slots:
	//槽函数
	void onReadyRead();			//响应 readyRead 信号，负责 接收数据包 粘包切包 处理

	void onLoginResponseInternal(bool result, int empID);		//内部槽：接管自动重登的响应

private:
	//成员变量
	QTcpSocket* m_tcpClientSocket;

	QByteArray m_buffer;		//数据包接收缓冲区

	QTimer* m_heartbeatTimer;	//心跳发送定时器（10s 周期）
	qint64 m_lastPongTime;		//最后收到心跳响应的时间戳


	DisconnectIntent m_intent;      //断线意图

	bool m_loggedIn;				//是否登录成功
	QString m_account;              //重登凭据：账号（登录请求时留存）
	QString m_password;             //重登凭据：密码

	QTimer* m_reconnectTimer;		//断线重连定时器（单次触发，按退避间隔重排）
	int m_reconnectAttempts;		//断线重连次数（计算退避间隔）
};
