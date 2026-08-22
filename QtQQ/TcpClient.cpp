#include "TcpClient.h"
#include "CommonUtils.h"

#include <QSettings>
#include <QHostAddress>
#include <QtEndian>
#include <QDebug>
#include <QDateTime>


TcpClient::TcpClient()
	:QObject(nullptr)
	, m_tcpClientSocket(nullptr)
	, m_lastPongTime(0)
	, m_intent(DisconnectIntent::None)
	, m_loggedIn(false)
	, m_reconnectAttempts(0)
{
	//创建心跳定时器（10s 周期）
	this->m_heartbeatTimer = new QTimer(this);
	this->m_heartbeatTimer->setInterval(10 * 1000);
	connect(this->m_heartbeatTimer, &QTimer::timeout, this, &TcpClient::sendHeartbeat);

	//创建重连定时器（单次触发：到期发起重连，结果决定是否重排下一轮）
	this->m_reconnectTimer = new QTimer(this);
	this->m_reconnectTimer->setSingleShot(true);
	connect(this->m_reconnectTimer, &QTimer::timeout, this, &TcpClient::connectToServer);


	//监听自己的登录响应：自动重登的回音由内部槽接管
	connect(this, &TcpClient::signalLoginResponse, this, &TcpClient::onLoginResponseInternal);
}

TcpClient::~TcpClient() 
{}

TcpClient& TcpClient::getInstance() {
	static TcpClient instance;
	return instance;
}

void TcpClient::connectToServer() {
	// socket 状态检查，避免重复发起连接
	//避免 TalkWindowShell 重复构造时多次调用 connectToHost 报错
	if (this->m_tcpClientSocket != nullptr) {
		QAbstractSocket::SocketState state = this->m_tcpClientSocket->state();

		//已连接或正在连接：直接返回，不重复发起
		if (state == QAbstractSocket::ConnectedState ||
			state == QAbstractSocket::ConnectingState) {
			return;
		}

		//残留的非连接态（如退出登录后的 ClosingState、DNS 解析中的 HostLookupState）：
		//Qt 规定这些状态下调用 connectToHost 会报 OperationError
		//"Trying to connect while connection is in progress" 并拒绝连接，
		//必须先 abort() 立即复位到 UnconnectedState 再重连
		//（进入 ClosingState 前写缓冲已清空，Logout 包早已发出，abort 不丢数据）
		this->m_tcpClientSocket->abort();
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

				//断线重连次数 > 0 ，说明正处于断线重连状态，且连接依旧失败
				if (this->m_reconnectAttempts > 0) {
					this->startReconnectTimer();	//重连失败，启动计时器，重排下次重连
				}
		});

		//监听 socket  connected 信号
		connect(this->m_tcpClientSocket, &QAbstractSocket::connected,
			this, [this]() {
				// connect 成功，打印连接成功消息
				qDebug() << "TCP connected";

				//启动心跳定时器，定时发送心跳包
				this->m_lastPongTime = QDateTime::currentMSecsSinceEpoch();
				this->m_heartbeatTimer->start();

				//静默重登：曾登录成功（凭据有效且已留存）→ 用留存凭据自动重新登录
				if (this->m_loggedIn == true) {
					//登录状态下断线重连成功
					//只有重新登录后，才清零计数 m_reconnectAttempts = 0
					this->sendLoginRequest(this->m_account, this->m_password);
				}
				else {
					//未登录状态下的连接成功（首次连接 / 登录页断线重连成功）：
					//重连流程使命已尽（连接已恢复），清零计数 m_reconnectAttempts = 0
					this->m_reconnectAttempts = 0;
				}
		});

		//监听 socket  readyRead 信号
		connect(this->m_tcpClientSocket, &QTcpSocket::readyRead, this, &TcpClient::onReadyRead);

		//监听 socket  disconnected 信号
		connect(this->m_tcpClientSocket, &QTcpSocket::disconnected,
			this, [this]() {

				this->m_heartbeatTimer->stop();	//停止心跳定时器
				this->m_buffer.clear();			//清空缓冲区
				
				//断线意图判断（所有断线路径最终都汇到 disconnected，统一在此分流）
				if (this->m_intent == DisconnectIntent::Logout || this->m_intent == DisconnectIntent::KickOut) {
					//主动断开（登出/被踢）：不重连，清状态

					this->m_intent = DisconnectIntent::None;
					this->m_loggedIn = false;
					this->m_account = "";
					this->m_password = "";
					this->m_reconnectAttempts = 0;

					this->m_reconnectTimer->stop();		//封存可能挂起的重连定时器
				}
				else {
					//意外断线（服务端崩溃/网络故障/心跳超时abort）：自动重连，开启重连定时器
					this->startReconnectTimer();
				}
		});
	}

	//向服务端发起连接
	//		对应 socket API  --------  connect(fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
	this->m_tcpClientSocket->connectToHost(QHostAddress(host), port);
}

void TcpClient::sendMessage(bool groupFlag, int sendID, int recvID, int msgType, const QString& msg, const QString& file) {
	// 拼接内层（群标识 + 发送方ID + 接收方ID + 消息类型 + 消息内容）
	
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

	//拼接内部数据
	QString strSend;		//内部数据包

	//数据包字段
	QString strGroupFlag = groupFlag ? "1" : "0";	//群聊标志，0私聊 1群聊
	//ID 按协议定宽补零（右对齐左补零），与收发两端的定长切分对齐
	//不补零属隐式依赖数据库 ID 位数（员工5位/群4位），ID 位数变化即解析错位
	QString strSendID = QString::number(sendID).rightJustified(5, '0');		//发送方 ID（5位）
	//接收方 ID：私聊为员工ID（5位），群聊为群ID（4位）
	QString strRecvID = groupFlag ? QString::number(recvID).rightJustified(4, '0')
		: QString::number(recvID).rightJustified(5, '0');
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
		//注意上限：外层包头长度字段为 quint16（最大 65535），数据体超限会被
		//static_cast<quint16> 静默截断，导致接收端切包错位、数据流错乱
		//预留内层地址头+类型+前缀的开销，文本上限取 60000
		int dataLength = msg.toUtf8().size();
		if (dataLength > 60000) {
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
	// 拼接内层（账号 + "|" + 密码）

	//先检查 socket 状态
	if (!m_tcpClientSocket || m_tcpClientSocket->state() != QAbstractSocket::ConnectedState) {
		//未连接（如被踢/断线后回到登录页）：主动发起连接，用户稍后重点登录即可
		//（被踢场景 FIN 时序导致 UserLogin 构造时 connectToServer 空转，此处补上连接路径）
		this->connectToServer();
		emit signalErrorOccurred(QStringLiteral("正在连接服务器，请稍后重试"));
		return;
	}

	//拼接内部数据（账号非定长，使用 "|" 与密码进行分隔）
	QString strSend;		//内部数据包
	strSend = account + "|" + password;

	//组装外层二进制头并发送（包类型=LoginRequest）
	this->sendPacket(static_cast<quint16>(PacketType::LoginRequest), strSend.toUtf8());

	//登录凭据留存
	this->m_account = account;
	this->m_password = password;
	this->m_intent = DisconnectIntent::None;
}

void TcpClient::sendLogout() {
	//用户主动退出登录：通知服务端解绑并断开连接

	//先检查 socket 状态
	if (!m_tcpClientSocket || m_tcpClientSocket->state() != QAbstractSocket::ConnectedState) {
		//未连接状态下的登出（如断线重连期间用户主动退出）：
		//终止挂起的重连循环并清留存凭据——否则定时器触发重连后，
		//connected 槽会拿旧凭据静默重登（幽灵登录/换号登录会登错账号）
		this->m_reconnectTimer->stop();
		this->m_reconnectAttempts = 0;
		this->m_loggedIn = false;
		this->m_account = "";
		this->m_password = "";
		return;
	}

	//发送注销包（告知服务端主动解绑，无需响应）
	this->sendPacket(static_cast<quint16>(PacketType::Logout), QByteArray());

	//断线意图
	this->m_intent = DisconnectIntent::Logout;

	//主动断开连接（会话终结）
	//   disconnectFromHost 会先 flush 发送缓冲再断开，Logout 包不会丢
	this->m_tcpClientSocket->disconnectFromHost();
}

void TcpClient::sendRegisterRequest(const QString& account, const QString& password, const QString& name) {

}

void TcpClient::sendDbQuery(const QString& sql, const QStringList& params) {

}

void TcpClient::sendHeartbeat() {
	//心跳超时自检：3个周期（30s）没收到任何回音 → 判定半开失联
	if (QDateTime::currentMSecsSinceEpoch() - this->m_lastPongTime > 30 * 1000) {
		qDebug() << QStringLiteral("[心跳] 超过30s未收到服务端回音，连接失联");

		//停止心跳定时器
		this->m_heartbeatTimer->stop();

		//复位 socket（触发 disconnected → 意图分流 → startReconnect 走重连流程）
		// abort 同步复位不阻塞 connectToHost ，方便后续快速断线重连
		this->m_tcpClientSocket->abort();
		return;
	}

	//未超时：发送心跳包（空载荷）
	this->sendPacket(static_cast<quint16>(PacketType::Heartbeat), QByteArray());
}

void TcpClient::sendPacket(quint16 packetType, const QByteArray& dataBody) {
	//拼接包头
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

void TcpClient::startReconnectTimer() {
	//防重排守卫：同一次断线可能先后触发 errorOccurred 与 disconnected
	//（如拔网线先报 ConnectionResetError 再走 disconnected），两处都会调用本函数；
	//定时器已挂起说明本轮重连已排好，直接跳过（否则 attempts 一次跳两级、间隔翻倍）
	if (this->m_reconnectTimer->isActive()) {
		return;
	}

	//单次触发，根据重连次数，重排时间间隔
	//指数退避：3s * 2^n，60s 封顶（第1次3s、第2次6s、第3次12s、第4次24s、第5次48s、之后60s）
	int shift = qMin(this->m_reconnectAttempts, 5);
	int interval = qMin(3 * 1000 * (1 << shift), 60 * 1000);
	this->m_reconnectAttempts++;

	//开启定时器
	this->m_reconnectTimer->start(interval);

	qDebug() << QStringLiteral("[Reconnect] 第%1次重连将于%2ms后发起").arg(this->m_reconnectAttempts).arg(interval);
	emit this->signalReconnectStarted();		//通知 UI 进入"重连中"状态
}

void TcpClient::onProcessPacket(const QByteArray& packet) {
	//	TcpClient（业务层）：
	//		├─ 解析外层包头（魔数、版本、包类型、数据长度）
	//		├─ 校验包头合法性（魔数、版本、长度）
	//		└─ 按包类型分发业务

	// 1. 长度校验：至少要有完整外层包头
	if (packet.size() < PACKET_HEADER_SIZE) {
		qDebug() << QStringLiteral("数据包过短(%1字节)，丢弃").arg(packet.size());
		return;
	}

	// 2. 解析外层包头（均大端序）
	quint16 magic = qFromBigEndian<quint16>(packet.constData() + 0);			//魔数
	quint8  version = static_cast<quint8>(packet[2]);							//版本
	quint16 packetType = qFromBigEndian<quint16>(packet.constData() + 3);		//包类型
	quint16 dataLength = qFromBigEndian<quint16>(packet.constData() + 5);		//数据体长度

	// 3. 校验包头合法性
	if (magic != PACKET_MAGIC) {
		qDebug() << QStringLiteral("魔数错误:0x%1，丢弃").arg(magic, 4, 16, QChar('0'));
		return;
	}
	if (version != PACKET_VERSION) {
		qDebug() << QStringLiteral("版本不匹配:期望%1实际%2，丢弃").arg(PACKET_VERSION).arg(version);
		return;
	}
	if (dataLength != packet.size() - PACKET_HEADER_SIZE) {
		qDebug() << QStringLiteral("数据长度不一致:头中%1实际%2，丢弃")
			.arg(dataLength).arg(packet.size() - PACKET_HEADER_SIZE);
		return;
	}

	// 4. 提取数据体（剥离外层包头）
	QByteArray dataBody = packet.mid(PACKET_HEADER_SIZE);

	// 5. 按包类型分发业务 ============================================================================================
	if (packetType == static_cast<quint16>(PacketType::Message)) {
		// ===== 消息包 =====
		// 解析内层（群标志 + 发送者ID + 接收者ID + 消息类型 + 消息内容）
		if (dataBody.size() < 11) {
			//最小头：群聊 1+5+4+1 = 11字节
			qDebug() << QStringLiteral("[Message] 数据体过短(%1字节)，丢弃").arg(dataBody.size());
			return;
		}

		// 消息字段
		int groupFlag = dataBody[0] - '0';		//群标志：0私聊 1群聊
		int sendId = dataBody.mid(1, 5).toInt();	//发送者ID
		int recvId = 0;		//接收者ID
		int msgType = 0;	//消息类型
		QString msg;		//消息内容

		if (groupFlag == 0) {
			//私聊：接收者ID占5位，消息类型在偏移11
			if (dataBody.size() < 12) {
				qDebug() << QStringLiteral("[Message] 私聊数据体过短(%1字节)，丢弃").arg(dataBody.size());
				return;
			}
			recvId = dataBody.mid(6, 5).toInt();
			msgType = dataBody.mid(11, 1).toInt();
			msg = QString::fromUtf8(dataBody.mid(12));
		}
		else {
			//群聊：接收者为4位群ID，消息类型在偏移10
			recvId = dataBody.mid(6, 4).toInt();
			msgType = dataBody.mid(10, 1).toInt();
			msg = QString::fromUtf8(dataBody.mid(11));
		}

		qDebug() << QStringLiteral("[Message] 收到消息：%1 → %2，类型%3")
			.arg(sendId).arg(recvId).arg(msgType);

		//解析完成，发射信号交给 UI 层（TalkWindow）显示
		emit this->signalMessageReceived(groupFlag, sendId, recvId, msgType, msg);
	}
	else if (packetType == static_cast<quint16>(PacketType::LoginResponse)) {
		// ===== 登录响应包 =====
		// 解析内层（结果标志1B + 用户ID 5B）
		// 格式：成功 "1" + "10001"；失败 "0"
		if (dataBody.size() < 1) {
			qDebug() << QStringLiteral("[LoginResponse] 数据体过短(%1字节)，丢弃").arg(dataBody.size());
			return;
		}

		bool result = (dataBody[0] == '1');		//结果标志：'1'成功 '0'失败
		int empID = 0;							//用户 employeeID（成功时有效）

		if (result == true) {
			//成功：数据体至少要有 结果标志1 + uid 5 = 6字节
			if (dataBody.size() < 6) {
				qDebug() << QStringLiteral("[LoginResponse] 成功响应缺少uid字段，丢弃");
				return;
			}
			empID = dataBody.mid(1, 5).toInt();

			//成功登录
			this->m_loggedIn = true;
		}

		//解析完成，发射信号交给 UI 层（UserLogin）处理界面跳转
		emit this->signalLoginResponse(result, empID);
	}
	else if (packetType == static_cast<quint16>(PacketType::KickOut)) {
		// ===== 踢下线通知包 =====
		// 重复登录时服务端先发此包通知旧客户端，随后才断开连接
		// 收到即触发被踢信号，由 UI 层弹提示并切回登录窗（此时连接即将断开，不再发包）
		qDebug() << QStringLiteral("[KickOut] 收到踢下线通知");

		//断线意图
		this->m_intent = DisconnectIntent::KickOut;

		//发射信号交给 UI 层（CCMainWindow）处理
		emit this->signalKickedOut();
	}
	else if (packetType == static_cast<quint16>(PacketType::HeartbeatResponse)) {
		// ===== 心跳响应包 =====
		// 刷新时间戳
		this->m_lastPongTime = QDateTime::currentMSecsSinceEpoch();
	}
	// ================================================================================================================
}


//槽函数
void TcpClient::onReadyRead() {
	//	TcpClient（网络层）：
	//		├─ 读取字节流
	//		├─ 粘包处理
	//		├─ 拆出完整应用层协议包
	//		└─ 交由 onProcessPacket 解析分发

	// 1. 读取所有可用数据，追加到缓冲区
	this->m_buffer.append(this->m_tcpClientSocket->readAll());

	// 2. 循环切包：只要缓冲区里有完整包就取出
	while (this->m_buffer.size() >= PACKET_HEADER_SIZE) {
		// 2.1 校验魔数（偏移0，2字节，大端序）
		quint16 magic = qFromBigEndian<quint16>(this->m_buffer.constData());

		if (magic != PACKET_MAGIC) {
			//魔数错误：数据错位或非法数据
			//在缓冲区中查找下一个魔数位置，丢弃错误数据，重新对齐数据流
			static const QByteArray magicBytes = QByteArray::fromRawData("\x5A\x5A", 2);
			int nextMagicPos = this->m_buffer.indexOf(magicBytes, 1);

			if (nextMagicPos > 0) {
				//找到下一个魔数：丢弃错误数据，从魔数位置继续处理
				qDebug() << QStringLiteral("魔数错误，丢弃 %1 字节错误数据，从下一个魔数重新对齐")
					.arg(nextMagicPos);
				this->m_buffer.remove(0, nextMagicPos);
				continue;	//重新回到 while 开头，重新校验魔数
			}
			else {
				//找不到下一个魔数：全部丢弃
				qDebug() << QStringLiteral("魔数错误且未找到下一个魔数，丢弃缓冲区所有数据");
				this->m_buffer.clear();
				break;
			}
		}

		// 2.2 解析数据长度字段（偏移5，2字节，大端序）
		quint16 dataLength = qFromBigEndian<quint16>(this->m_buffer.constData() + 5);	//数据体长度
		quint16 packetSize = PACKET_HEADER_SIZE + dataLength;		//数据包总长度

		// 2.3 缓冲区数据不足一个完整包，等待下次数据到达
		if (this->m_buffer.size() < packetSize) {
			break;
		}

		// 2.4 取出一个完整包，交由业务层解析
		QByteArray packet = this->m_buffer.left(packetSize);
		this->m_buffer.remove(0, packetSize);

		this->onProcessPacket(packet);
	}
}

void TcpClient::onLoginResponseInternal(bool result, int empID) {
	//只有"曾登录成功 + 处于未完成的重连流程"的登录响应，
	//才视为自动重登的回音由本槽接管；其余（首次登录/手动再登录）交回 UserLogin 消费
	if (this->m_loggedIn == false || this->m_reconnectAttempts == 0) {
		return;
	}

	this->m_reconnectAttempts = 0;
	
	if (result == true) {
		//重登成功：会话真正恢复，重置退避计数（下次断线从 3s 重新起跳）
		qDebug() << QStringLiteral("[Reconnect] 重连+重登成功，uid=%1 会话已恢复").arg(empID);
		emit this->signalReconnected();
		//服务端重登成功时会自动推送离线消息，补齐断线期间的消息
	}
}
