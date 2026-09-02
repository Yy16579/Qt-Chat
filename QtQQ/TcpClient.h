#pragma once

#include "Protocol.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QHash>
#include <QMap>


//断线意图
enum class DisconnectIntent {
	None,       //意外断线（服务端崩溃/网络故障/心跳超时abort）→ 触发自动重连
	Logout,     //用户主动登出 → 不重连
	KickOut     //被踢下线 → 不重连
};


//待确认消息结构体
struct PendingMsg {
	QByteArray packet;		//数据体（[msgId|seq|载荷]，重传时再喂给 sendPacket 重封包头）
	QString msgId;			//ACK 匹配键（服务端 MessageAck 数据体）
	int attempts;			//本轮连通周期内已重传次数（3/6/12s 有界 3 次）
	QTimer* timer;			//单次触发重传定时器（父对象 TcpClient）
};


//连接地址和端口从 config.ini 的 [Tcp] 节读取
class TcpClient : public QObject
{
	Q_OBJECT

public:
	static TcpClient& getInstance();

	void connectToServer();			//创建套接字，向服务端发起连接
	bool isConnected() const;		//连接状态查询

public:
	// 数据包打包发送接口 ===================================================================================
	// 发送消息包（返回 false = 发送失败：未连接/消息过长，调用方据此决定是否入本地仓库）
	bool sendMessage(bool groupFlag, int sendID, int recvID, int msgType, const QString& msg, const QString& file = "");

	// 发送拉取请求包
	void sendPullRequest(int singleConvId = -1);

	// 发送登录请求
	void sendLoginRequest(const QString& account, const QString& password);

	// 发送注销请求并断开连接
	void sendLogout();

	// 发送注册请求
	void sendRegisterRequest(const QString& account, const QString& password, const QString& name);

	// 发送心跳包
	void sendHeartbeat();
	
private:
	// 拼接包头并发送（所有上层接口最终调用这个）
	void sendPacket(quint16 packetType, const QByteArray& dataBody);
	// =================================================================================================================

private:
	// =================================================================================================================
	void loadSeqState(int empID);		//seq 表初始化（seq_<empID>.ini：[Send] 取号机 + [Ledger] 账本）
	void seedLedgerFromContacts(int empID);		//账本补零：按通讯录快照为缺失会话建游标 0 条目（空账本拉取死锁解）
	void saveSeqState(int convId);		//消息发送 seq 表状态同步至配置文件（防窗口崩溃）
	void saveLedgerState(int convId);	//账本状态同步至配置文件（渲染落账时调用，防窗口崩溃）
	QByteArray buildCursorTable(int singleConvId = -1);		//创建账本快照 [会话数2B] + N × [convId5B][游标10B]
	void handlePulledMsg(int convId, quint64 seq, const QString& msgId, const QByteArray& payload);		//拉取消息连续性校验
	void dispatchMsg(const QByteArray& payload);		//载荷切分 + 发射接收信号（拉取/缓冲排空共用出口）
	void flushPending();				//断线重连重登成功后，未确认消息全表重发（attempts 归零）
	void clearPending();				//会话终结（Logout/KickOut）清空全表
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


	// 业务分发信号 ====================================================================================================
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

	QHash<QString, PendingMsg> m_pending;		//待确认消息表：msgId → 重传信息
	QHash<int, QMap<quint64, QByteArray>> m_reorderBuf;		//乱序缓冲区：会话ID → (seq → 消息载荷)，超前消息暂存

	QMap<int, quint64> m_sendCounter;			//消息发送 seq 表：会话ID → 已发出最大 seq
	QMap<int, quint64> m_ledger;				//消息接收 seq 表（账本）：会话ID → 已接收最大 seq
};
