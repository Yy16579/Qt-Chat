#pragma once

#include <QTcpSocket>


class TcpSocket  : public QTcpSocket
{
	Q_OBJECT

public:
	explicit TcpSocket(QObject* parent = nullptr);
	~TcpSocket();

public:
	void initSocket();

	void setUid(int uid);
	int getUid() const;
	void touch();				//刷新最后活跃时间，为当前时刻
	qint64 lastActive() const;	//读取最后活跃时间

signals:
	//信号
	void signalPacketReady(const QByteArray& data, int descriptor);
	void signalClientDisconnected(int descriptor);

private slots:
	//槽函数
	void onReadyRead();				//响应 readyRead 信号，负责 接收数据包 粘包切包 处理
	void onClientDisconnected();	//响应 disconnected 信号

private:
	int m_descriptor;	// TcpSocket 对象唯一标识（fd）描述符
	int m_uid;			// 当前通信用户 uid （客户端成功登录时初始化）

	QByteArray m_buffer;	// 数据包接收缓冲区，用于粘包处理

	qint64 m_lastActive;	// 最后活跃时间（收到任何包都刷新）
};

