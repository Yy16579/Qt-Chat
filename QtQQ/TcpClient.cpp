#include "TcpClient.h"
#include "CommonUtils.h"

#include <QSettings>
#include <QHostAddress>
#include <QMessageBox>
#include <QtEndian>


TcpClient::TcpClient()
	:QObject(nullptr)
	, m_tcpClientSocket(nullptr)
{}

TcpClient::~TcpClient() 
{}

TcpClient& TcpClient::getInstance() {
	static TcpClient instance;
	return instance;
}

void TcpClient::connectToServer() {
	// socket 已存在且已连接，直接返回
	//避免 TalkWindowShell 重复构造时多次调用 connectToHost 报错
	if (this->m_tcpClientSocket != nullptr &&
		this->m_tcpClientSocket->state() != QAbstractSocket::UnconnectedState) {
		return;
	}

	//从 config.ini 配置文件读取服务端地址和端口
	QSettings settings(CommonUtils::getConfigPath(), QSettings::IniFormat);
	QString host = settings.value("Tcp/host").toString();
	quint16 port = settings.value("Tcp/port").toUInt();


	//创建客户端 tcp 套接字
	//		对应 socket API  --------  socket(AF_INET, SOCK_STREAM, 0);
	if (this->m_tcpClientSocket == nullptr) {
		this->m_tcpClientSocket = new QTcpSocket(this);


		//监听 socket  errorOccurred 信号
		connect(this->m_tcpClientSocket, &QAbstractSocket::errorOccurred,
			this, [this](QAbstractSocket::SocketError err) {
				//统一在此处理所有连接异常和断开场景
				//服务端正常关闭连接时，显示友好提示
				if (err == QAbstractSocket::RemoteHostClosedError) {
					emit this->signalErrorOccurred(QStringLiteral("连接已断开"));
					return;
				}
				//其他异常错误，显示具体错误信息（如"主机找不到""连接被拒""网络中断"）
				emit this->signalErrorOccurred(this->m_tcpClientSocket->errorString());
		});

		//监听 socket  connected 信号
		connect(m_tcpClientSocket, &QAbstractSocket::connected,
			this, [this]() {
				// connect 成功，打印连接成功消息
				qDebug() << "TCP connected";
		});
	}

	//向服务端发起连接
	//		对应 socket API  --------  connect(fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
	this->m_tcpClientSocket->connectToHost(QHostAddress(host), port);
}

void TcpClient::sendMessage(bool groupFlag, int sendID, int recvID, int msgType, const QString& msg, const QString& file) {
	//接收到的参数：
	//		例1（纯表情）   :		(0, "1images023")	
	//		例2（纯文本）   :		(1, "你好")
	//		例3（）

	/*
	【表情】数据包格式：
	群聊标志（0私聊，1群聊） + 发信息员工QQ号 + 收信息员工QQ号（群QQ号）
		+ 信息类型（表情） + 表情个数 + images + 表情数据

	【文本】数据包格式：
	群聊标志（0私聊，1群聊） + 发信息员工QQ号 + 收信息员工QQ号（群QQ号）
		+ 信息类型（文本） + 数据长度 + 文本数据	

	【文件】数据包格式：
	群聊标志（0私聊，1群聊） + 发信息员工QQ号 + 收信息员工QQ号（群QQ号）
		+ 信息类型（2文件） + 文件长度 + "bytes" + 文件名称 + "data_begin" + 文件内容

	msgType，信息类型，0是表情，1是文本，2是文件
	*/


	//先检查 socket 状态
	if (!m_tcpClientSocket || m_tcpClientSocket->state() != QAbstractSocket::ConnectedState) {
		emit signalErrorOccurred(QStringLiteral("未连接到服务器"));
		return;
	}

	//对数据进行封包
	QString strSend;		//发送数据包

	//数据包字段
	QString strGroupFlag = groupFlag ? "1" : "0";	//群聊标志，0私聊 1群聊
	QString strSendID = QString::number(sendID);	//发送方 ID
	QString strRecvID = QString::number(recvID);	//接收方 ID
	QString strDataType;		//消息类型
	QString strData;			//消息数据
	
	//初始化数据包字段
	if (msgType == 0) {			//消息类型为 表情
		strDataType = QString::number(0);
		strData = msg;
	}
	else if (msgType == 1) {	//消息类型为 文本
		strDataType = QString::number(1);

		//数据长度（UTF-8 字节数），始终为5位宽，不足补零
		int dataLength = msg.toUtf8().size();
		if (dataLength > 99999) {
			emit signalErrorOccurred(QStringLiteral("消息过长"));
			return;
		}
		QString strDataLength = QString::number(dataLength).rightJustified(5, '0');

		strData = strDataLength + msg;
	}
	else if (msgType == 2) {	//消息类型为 文件
		strDataType = QString::number(2);

	}

	//拼接内层数据包（文本格式）
	strSend = strGroupFlag + strSendID + strRecvID + strDataType + strData;

	//组装外层二进制头并发送（包类型=Message）
	this->sendPacket(static_cast<quint16>(PacketType::Message), strSend.toUtf8());
}

void TcpClient::sendLoginRequest(const QString& account, const QString& password) {

}

void TcpClient::sendRegisterRequest(const QString& account, const QString& password, const QString& name) {

}

void TcpClient::sendDbQuery(const QString& sql, const QStringList& params) {

}

void TcpClient::sendHeartbeat() {

}

void TcpClient::sendPacket(quint16 packetType, const QByteArray& dataBody) {
	//拼接包头并发送
	QByteArray packet;
	//1. 魔数（大端）
	quint16 magic = qToBigEndian(PACKET_MAGIC);
	packet.append(reinterpret_cast<const char*>(&magic), 2);
	//2. 版本
	packet.append(static_cast<char>(PACKET_VERSION));
	//3. 包类型（大端）
	quint16 type = qToBigEndian(packetType);
	packet.append(reinterpret_cast<const char*>(&type), 2);
	//4. 数据长度（大端）
	quint16 len = qToBigEndian(static_cast<quint16>(dataBody.size()));
	packet.append(reinterpret_cast<const char*>(&len), 2);
	//5. 数据体
	packet.append(dataBody);

	//发送
	this->m_tcpClientSocket->write(packet);
}

