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
	int getUid();

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

};

