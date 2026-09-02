#include "TcpClient.h"
#include "CommonUtils.h"
#include "ContactBook.h"
#include "WindowManager.h"

#include <QSettings>
#include <QHostAddress>
#include <QtEndian>
#include <QDebug>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>


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


				//重连计数器置零
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
				
				//断线原因判断（所有断线路径最终都汇到 disconnected，统一在此分流）
				if (this->m_intent == DisconnectIntent::Logout || this->m_intent == DisconnectIntent::KickOut) {
					//主动断开（登出/被踢）：不重连，清状态

					this->m_intent = DisconnectIntent::None;
					this->m_loggedIn = false;
					this->m_account = "";
					this->m_password = "";
					this->m_reconnectAttempts = 0;

					this->clearPending();				//会话终结：pending 表责任解除（未确认消息不再追投）+ 防泄漏
					this->m_reconnectTimer->stop();		//封存可能挂起的重连定时器
				}
				else {
					//pending 全体停表（挂起）：消息未确认责任未了，等重连 flush
					for (auto it = this->m_pending.begin(); it != this->m_pending.end(); ++it) {
						it.value().timer->stop();
					}
					
					//意外断线（服务端崩溃/网络故障/心跳超时abort）：自动重连，开启重连定时器
					this->startReconnectTimer();
				}
		});
	}

	//向服务端发起连接
	//		对应 socket API  --------  connect(fd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));
	this->m_tcpClientSocket->connectToHost(QHostAddress(host), port);
}

bool TcpClient::isConnected() const {
	//socket 存在且处于已连接状态才算在线（ConnectingState 等中间态不算）
	return this->m_tcpClientSocket && this->m_tcpClientSocket->state() == QAbstractSocket::ConnectedState;
}

bool TcpClient::sendMessage(bool groupFlag, int sendID, int recvID, int msgType, const QString& msg, const QString& file) {
	// 1. 拼接内层消息包（ msgId + seq + 群标识 + 发送方ID + 接收方ID + 消息类型 + 消息内容）
	// 2. 向服务端发送数据包
	// 3. 加入待确认表（启动超时重传计时器）

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
		return false;
	}

	//拼接内部数据
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
			return false;
		}
		QString strDataLength = QString::number(dataLength).rightJustified(5, '0');

		strData = strDataLength + msg;
	}
	else if (msgType == 2) {	//消息类型为 文件
		strDataType = QString::number(2);

	}

	// msgId 生成：m_empID后3位 + 毫秒时间戳后10位（13位定宽十进制）
	QString msgId = QString::number(WindowManager::getInstance().m_empID % 1000).rightJustified(3, '0')
		+ QString::number(QDateTime::currentMSecsSinceEpoch() % 10000000000LL).rightJustified(10, '0');

	// seq 取号：序列号在点击瞬间冻结
	quint64 seq = ++this->m_sendCounter[recvID];
	this->saveSeqState(recvID);				//取号后即时写入配置文件（防重启撞号）

	//组装数据体并发出
	QString strSend = strGroupFlag + strSendID + strRecvID + strDataType + strData;
	QByteArray body = msgId.toUtf8()
		+ QString::number(seq).rightJustified(SEQ_LEN, '0').toUtf8()
		+ strSend.toUtf8();
	this->sendPacket(static_cast<quint16>(PacketType::Message), body);

	//消息重传机制：
	//消息注册至待确认表 + 启动重传定时器（无 ACK 则 3/6/12s 有界重传 3 次）
	PendingMsg pending;
	pending.packet = body;
	pending.msgId = msgId;
	pending.attempts = 0;
	pending.timer = new QTimer(this);
	pending.timer->setSingleShot(true);
	connect(pending.timer, &QTimer::timeout, this, [this, msgId]() {
		//条目可能已被 ACK 移除（停表与 timeout 的竞态兜底）
		if (!this->m_pending.contains(msgId)) {
			return;
		}
		PendingMsg& p = this->m_pending[msgId];

		//断线中：不重排、不烧次数（挂起等重连 flush——消息未确认，责任未了）
		if (!this->isConnected()) {
			return;
		}

		//3 次耗尽：清理 + 通知失败（内嵌式提示，不弹窗；本地历史库不回滚——如实记录"我说过"）
		if (p.attempts >= 3) {
			qWarning() << QStringLiteral("[Send] msgId=%1 重传3次无ACK，放弃").arg(msgId);
			emit this->signalErrorOccurred(QStringLiteral("消息发送失败，对方可能未收到"));
			p.timer->stop();
			p.timer->deleteLater();
			this->m_pending.remove(msgId);		//remove 后不再碰 p（引用失效）
			return;
		}

		//重发：存的数据体再喂给 sendPacket（字节级一致 → 服务端幂等挡重复 → 照样回 ACK → 停手）
		p.attempts++;
		this->sendPacket(static_cast<quint16>(PacketType::Message), p.packet);

		//重排下一次（3→6→12s 有界）
		static const int intervals[3] = { 3 * 1000, 6 * 1000, 12 * 1000 };
		p.timer->start(intervals[qMin(p.attempts, 2)]);
		qDebug() << QStringLiteral("[Send] msgId=%1 第%2次重传").arg(msgId).arg(p.attempts);
	});
	pending.timer->start(3 * 1000);			//启动超时重传计时器
	this->m_pending.insert(msgId, pending);

	qDebug() << QStringLiteral("[Send] msgId=%1 conv=%2 seq=%3 已发出，pending=%4").arg(msgId).arg(recvID).arg(seq).arg(this->m_pending.size());

	return true;		//成功发出（bool 返回值：false = 未连接/超长，调用方据此决定是否入本地库）
}

void TcpClient::sendPullRequest(int singleConvId) {
	//拉取请求：获取账本快照 → 发送（触发源：敲门/登录/补拉/续拉）
	//singleConvId（会话ID） = -1 → 全表拉取；否则只拉取该会话（定点补拉用）

	//状态检查：断线瞬间触发的 pull 直接丢弃（敲门丢失无后果，心跳对账兜底）
	if (!m_tcpClientSocket || m_tcpClientSocket->state() != QAbstractSocket::ConnectedState) {
		return;
	}

	this->sendPacket(static_cast<quint16>(PacketType::PullRequest), QByteArray(this->buildCursorTable(singleConvId)));
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

	// 1. 发送注销包（告知服务端主动解绑，无需响应）
	this->sendPacket(static_cast<quint16>(PacketType::Logout), QByteArray());

	// 2. 断线意图
	this->m_intent = DisconnectIntent::Logout;

	// 3. 主动断开连接（会话终结）
	//   disconnectFromHost 会先 flush 发送缓冲再断开，Logout 包不会丢
	this->m_tcpClientSocket->disconnectFromHost();
}

void TcpClient::sendRegisterRequest(const QString& account, const QString& password, const QString& name) {

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

	//未超时：发送心跳包（未登录 = 空载荷纯保活；已登录 = 带账本对账，落后会被服务端敲门）
	if (this->m_loggedIn == false) {
		this->sendPacket(static_cast<quint16>(PacketType::Heartbeat), QByteArray());
	}
	else {
		this->sendPacket(static_cast<quint16>(PacketType::Heartbeat), this->buildCursorTable(-1));
	}
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
	if (packetType == static_cast<quint16>(PacketType::PullResponse)) {
		// ===== 1. 拉取响应包（消息本体唯一入口）=====
		// 数据体 = JSON {"count":N,"msgs":[{"convId":"..","seq":"..","msgId":"..","payload":"base64"}...]}
		// 逐条解析后交连续性校验：JSON 字段边界天然清晰，无需定长切分

		QJsonParseError parseErr;
		QJsonDocument doc = QJsonDocument::fromJson(dataBody, &parseErr);
		if (parseErr.error != QJsonParseError::NoError || doc.isObject() == false) {
			qDebug() << QStringLiteral("[PullResponse] JSON 解析失败(%1)，丢弃").arg(parseErr.errorString());
			return;
		}

		const QJsonArray msgs = doc.object().value("msgs").toArray();
		for (const QJsonValue& v : msgs) {
			QJsonObject msg = v.toObject();
			//convId/seq/msgId 均为字符串承载（服务端封包约定：quint64 防 JSON 数值精度损失）
			int convId = msg.value("convId").toString().toInt();
			quint64 seq = msg.value("seq").toString().toULongLong();
			QString msgId = msg.value("msgId").toString();
			QByteArray payload = QByteArray::fromBase64(msg.value("payload").toString().toUtf8());

			qDebug() << QStringLiteral("[PullResponse] conv=%1 seq=%2 msgId=%3（%4 字节载荷）")
				.arg(convId).arg(seq).arg(msgId).arg(payload.size());

			//逐条进入连续性校验（渲染落账 / 丢弃重复 / 空洞缓冲）
			this->handlePulledMsg(convId, seq, msgId, payload);
		}
	}
	else if (packetType == static_cast<quint16>(PacketType::LoginResponse)) {
		// ===== 2. 登录响应包 =====
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

			//提取通讯录快照：6字节定长头（结果1B+uid5B）之后的附加数据
			//必须先填缓存再 emit——emit 后 UserLogin 会立即 new CCMainWindow 查询缓存
			if (dataBody.size() > 6) {
				ContactBook::getInstance().loadFromJson(dataBody.mid(6));
			}
			
			//成功登录
			this->m_loggedIn = true;

			//消息发送 seq 表状态载入（必须先于自动 Pull：账本装载后游标才正确，否则发空表拉不到积压）
			this->loadSeqState(empID);

			//账本补零：按通讯录快照为缺失会话补游标 0（空账本开不出 Pull 清单的死锁解，须在自动 Pull 之前）
			this->seedLedgerFromContacts(empID);

			//登录成功自动 Pull：拉取离线期间积压（不等 30s 心跳对账敲门）
			//（也是服务端重启后 m_convMaxSeq 高水位丢失场景的兜底：按账本直查库，不依赖内存对账）
			this->sendPullRequest(-1);
		}

		//解析完成，发射信号交给 UI 层（UserLogin）处理界面跳转
		emit this->signalLoginResponse(result, empID);
	}
	else if (packetType == static_cast<quint16>(PacketType::KickOut)) {
		// ===== 3. 踢下线通知包 =====
		// 重复登录时服务端先发此包通知旧客户端，随后才断开连接
		// 收到即触发被踢信号，由 UI 层弹提示并切回登录窗（此时连接即将断开，不再发包）
		qDebug() << QStringLiteral("[KickOut] 收到踢下线通知");

		//断线意图
		this->m_intent = DisconnectIntent::KickOut;

		//发射信号交给 UI 层（CCMainWindow）处理
		emit this->signalKickedOut();
	}
	else if (packetType == static_cast<quint16>(PacketType::HeartbeatResponse)) {
		// ===== 4. 心跳响应包 =====
		// 刷新时间戳
		this->m_lastPongTime = QDateTime::currentMSecsSinceEpoch();
	}
	else if (packetType == static_cast<quint16>(PacketType::MessageAck)) {
		// ===== 5. 投递确认包 =====
		// 数据体 = msgId 13B：服务端入库成功的回执（含幂等命中——重传的重复包也算成功）
		// 收到即移除 pending 条目 + 停重传定时器
		if (dataBody.size() < MSGID_LEN) {
			qDebug() << QStringLiteral("[MessageAck] 数据体过短(%1字节)，丢弃").arg(dataBody.size());
			return;
		}

		QString msgId = QString::fromUtf8(dataBody.left(MSGID_LEN));
		if (this->m_pending.contains(msgId)) {
			//命中：停表销毁 + 移除条目
			qDebug() << QStringLiteral("[MessageAck] msgId=%1 已投递，移除 pending（剩 %2 条）")
				.arg(msgId).arg(this->m_pending.size() - 1);
			this->m_pending[msgId].timer->stop();
			this->m_pending[msgId].timer->deleteLater();
			this->m_pending.remove(msgId);
		}
		else {
			//迟到 ACK（对应已耗尽移除/已 flush 的条目），无害忽略
			qDebug() << QStringLiteral("[MessageAck] msgId=%1 无对应 pending（迟到ACK），忽略").arg(msgId);
		}
	}
	else if (packetType == static_cast<quint16>(PacketType::MsgNotify)) {
		// ===== 6. 敲门包 =====
		// 服务端通知"有新消息"（数据体为空，纯信令）：全表拉取
		// 触发源：消息入库 / 心跳对账发现落后 / 满页续拉（onPullLoaded 判 count ≥ 阈值）
		this->sendPullRequest(-1);
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
	//只有 "登录成功 + 处于未完成的重连流程" 的登录响应，
	//才视为自动重登的回音由本槽接管；其余（首次登录/手动再登录）交回 UserLogin 消费
	if (this->m_loggedIn == false || this->m_reconnectAttempts == 0) {
		return;
	}

	this->m_reconnectAttempts = 0;
	
	if (result == true) {
		//重登成功：会话恢复，重置退避计数（下次断线从 3s 重新起跳）
		qDebug() << QStringLiteral("[Reconnect] 重连+重登成功，uid=%1 会话已恢复").arg(empID);
		emit this->signalReconnected();

		//pending 全表 flush 重发：断线期间未确认的消息重新投递
		this->flushPending();
	}
}


// ===== seq 可靠性（发送取号机 + 接收账本）===============================================================================

void TcpClient::loadSeqState(int empID) {
	//登录成功时读取配置文件（seq_<empID>.ini，按账号分文件换号天然隔离）
	//首次登录无键 → 空表 → 取号从 1 起 / 账本从 0 起（新会话新序号空间）

	this->m_sendCounter.clear();
	this->m_ledger.clear();		//★ 先清空再加载（防上一账号残留）

	//ini 与 config.ini 同放 exe 目录（双击 exe / IDE 启动的工作目录不同，相对路径会散落两处）
	QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/seq_%1.ini").arg(empID);
	QSettings settings(path, QSettings::IniFormat);
	for (const QString& key : settings.allKeys()) {
		//键格式：Send/<convId> 或 Ledger/<convId>
		QStringList parts = key.split('/');
		if (parts.size() != 2) {
			continue;
		}
		if (parts.at(0) == QStringLiteral("Send")) {
			this->m_sendCounter[parts.at(1).toInt()] = settings.value(key).toULongLong();
		}
		else if (parts.at(0) == QStringLiteral("Ledger")) {
			this->m_ledger[parts.at(1).toInt()] = settings.value(key).toULongLong();
		}
	}
	qDebug() << QStringLiteral("[Seq] seq 表载入完成：uid=%1，取号机 %2 个会话，账本 %3 个会话")
			.arg(empID).arg(this->m_sendCounter.size()).arg(this->m_ledger.size());
}

void TcpClient::seedLedgerFromContacts(int empID) {
	//账本补零：遍历通讯录快照，账本缺失的会话显式补游标 0
	//（空账本 → buildCursorTable 报 "00" → 服务端按表办事返回空 → 敲门/拉取死循环；补零后清单含全部会话，从 0 起拉）
	//注意：不覆盖已有条目——只补缺，已推进的游标原样保留

	//路径用参数 empID 直接构造（不能复用 saveLedgerState：其路径取 WindowManager.m_empID，
	//而该值在 emit 登录响应、UserLogin new CCMainWindow 之后才设置，此刻仍是旧值/初始值 -1）
	QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/seq_%1.ini").arg(empID);
	QSettings settings(path, QSettings::IniFormat);

	for (int convId : ContactBook::getInstance().allConvIds()) {
		if (this->m_ledger.contains(convId) == false) {
			this->m_ledger[convId] = 0;
			settings.setValue(QStringLiteral("Ledger/%1").arg(convId), QStringLiteral("0"));
		}
	}
	settings.sync();		//立即落盘（防崩溃窗口，与 saveLedgerState 同策略）
	qDebug() << QStringLiteral("[Seq] 账本补零完成：uid=%1，账本 %2 个会话")
		.arg(empID).arg(this->m_ledger.size());
}

void TcpClient::saveSeqState(int convId) {
	//发送消息时同步写入配置文件（防崩溃窗口：取了号没写盘就崩溃，重启读回旧号 → 撞号）
	//（与 loadSeqState 同放 exe 目录，防工作目录漂移导致读写两个文件）
	QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/seq_%1.ini")
		.arg(WindowManager::getInstance().m_empID);
	QSettings settings(path, QSettings::IniFormat);
	settings.setValue(QStringLiteral("Send/%1").arg(convId),
		QString::number(this->m_sendCounter.value(convId)));
	settings.sync();		//立即落盘
}

void TcpClient::saveLedgerState(int convId) {
	//消息渲染落账时同步写入（防崩溃窗口：账本内存推进了没写盘就崩溃，重启读回旧游标 → 重复拉取，靠 msgId 去重兜底）
	QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/seq_%1.ini")
		.arg(WindowManager::getInstance().m_empID);
	QSettings settings(path, QSettings::IniFormat);
	settings.setValue(QStringLiteral("Ledger/%1").arg(convId),
		QString::number(this->m_ledger.value(convId)));
	settings.sync();		//立即落盘
}

void TcpClient::handlePulledMsg(int convId, quint64 seq, const QString& msgId, const QByteArray& payload) {
	//连续性校验三分支：命中渲染落账 / 落后丢弃 / 超前缓冲补洞
	//（msgId 本阶段不使用，B7 msgId 去重集会用——重复拉取由账本比较兜底）
	quint64 cur = this->m_ledger.value(convId, 0);		//无记录 = 0（value 不插表，账本零污染）

	if (seq == cur + 1) {
		//① 命中：渲染 + 账本推进
		this->dispatchMsg(payload);
		this->m_ledger[convId] = seq;

		//② 回看乱序缓冲区：超前暂存的消息若恰好接上，连发排空（QMap 按 seq 有序，firstKey 即队首）
		QMap<quint64, QByteArray>& buf = this->m_reorderBuf[convId];
		while (buf.isEmpty() == false && buf.firstKey() == this->m_ledger.value(convId) + 1) {
			this->dispatchMsg(buf.first());
			this->m_ledger[convId] = buf.firstKey();
			buf.erase(buf.begin());
		}
		if (buf.isEmpty() == true) {
			this->m_reorderBuf.remove(convId);		//排空清理空表项
		}

		this->saveLedgerState(convId);		//循环外一次落盘（含连发推进的最终值）
	}
	else if (seq <= cur) {
		//③ 落后（重复拉取）：丢弃——账本已是更高值，说明这条早收过
		qDebug() << QStringLiteral("[Pull] conv=%1 seq=%2 落后于账本(%3)，丢弃重复")
			.arg(convId).arg(seq).arg(cur);
	}
	else {
		//④ 超前（发现空洞 seq ∈ (cur+1, seq)）：暂存 + 定点补拉
		this->m_reorderBuf[convId].insert(seq, payload);
		qWarning() << QStringLiteral("[Pull] conv=%1 空洞：账本=%2 收到=%3，缓冲并补拉")
			.arg(convId).arg(cur).arg(seq);
		// TODO(B5)：sendGapPull(convId)——单会话定点补拉（游标=当前账本，500ms×5 次超时跳账）
	}
}

void TcpClient::dispatchMsg(const QByteArray& payload) {
	//载荷切分（与 sendMessage 封包互逆）：[群标志1B][发送者5B][接收者(私5/群4)][类型1B][内容...]
	//内容 wire 格式原样透传（文本含 5B 长度前缀）——入库/渲染零转换，下游自行解析
	if (payload.size() < 11) {		//群聊头最小 11B（私聊 12B 更长天然覆盖）
		qWarning() << QStringLiteral("[Pull] 载荷过短(%1字节)，丢弃").arg(payload.size());
		return;
	}
	int groupFlag = QString::fromUtf8(payload.left(1)).toInt();
	int sendId = QString::fromUtf8(payload.mid(1, 5)).toInt();
	int recvLen = (groupFlag != 0) ? 4 : 5;
	int recvId = QString::fromUtf8(payload.mid(6, recvLen)).toInt();
	int msgType = QString::fromUtf8(payload.mid(6 + recvLen, 1)).toInt();
	QString msg = QString::fromUtf8(payload.mid(6 + recvLen + 1));

	emit this->signalMessageReceived(groupFlag, sendId, recvId, msgType, msg);		//→ TalkSessionStore 入库+广播
}

QByteArray TcpClient::buildCursorTable(int singleConvId) {
	//创建账本快照：[会话数 2B] + N × [convId 5B][游标 10B]，全部十进制补零
	//singleConvId（会话ID） = -1 → 全表创建；否则只创建该会话（定点补拉用）

	QMap<int, quint64> report;		//本次上报的会话子集
	if (singleConvId == -1) {
		report = this->m_ledger;
	}
	else {
		report.insert(singleConvId, this->m_ledger.value(singleConvId, 0));		//无记录 = 游标 0（从头拉）
	}

	QString strCount = QString::number(report.size()).rightJustified(2, '0');	//会话数 2B
	QStringList entries;
	for (auto it = report.begin(); it != report.end(); ++it) {
		entries << QString::number(it.key()).rightJustified(CONV_LEN, '0')
			+ QString::number(it.value()).rightJustified(SEQ_LEN, '0');
	}
	return (strCount + entries.join(QString())).toUtf8();
}

void TcpClient::flushPending() {
	//重连+重登成功后：全表重发（attempts 归零，重新起 3s——重连后给满 3 次新机会）
	for (auto it = this->m_pending.begin(); it != this->m_pending.end(); ++it) {
		PendingMsg& p = it.value();
		this->sendPacket(static_cast<quint16>(PacketType::Message), p.packet);
		p.attempts = 0;
		p.timer->start(3 * 1000);
	}
	if (!this->m_pending.isEmpty()) {
		qDebug() << QStringLiteral("[Send] 重连恢复：flush 重发 %1 条待确认消息").arg(this->m_pending.size());
	}
}

void TcpClient::clearPending() {
	//会话终结（Logout/KickOut）清空全表（责任解除 + 防泄漏）
	for (auto it = this->m_pending.begin(); it != this->m_pending.end(); ++it) {
		if (it.value().timer != nullptr) {
			it.value().timer->stop();
			it.value().timer->deleteLater();
		}
	}
	this->m_pending.clear();
}
// ======================================================================================================================
