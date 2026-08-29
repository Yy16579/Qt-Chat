#include "TcpServer.h"

#include <QDebug>
#include <QtEndian>
#include <QDateTime>


TcpServer::TcpServer(int port)
	: m_port(port)
{
	//初始化心跳超时扫描定时器，每 30s 巡检
	this->m_checkTimer = new QTimer(this);
	this->m_checkTimer->setInterval(30 * 1000);
	connect(this->m_checkTimer, &QTimer::timeout, this, &TcpServer::onCheckTimeout);
	this->m_checkTimer->start();

	//初始化 DB 任务线程池：主线程只做 IO 和快业务，MySQL 慢查询全部外包给池
	//（池线程常驻不过期：保证 DbConnPool"每线程一条连接"恒定复用，杜绝线程销毁后 tid 复用取到旧连接的竞态）
	this->m_taskPool = new QThreadPool(this);
	this->m_taskPool->setMaxThreadCount(4);		// 设置池线程数量
	this->m_taskPool->setExpiryTimeout(-1);		// -1 池线程常驻不过期

	//结果回传器：池线程 emit 信号 → 事件队列投递 → 下列结果槽在主线程执行（跨线程信号槽自动队列连接）
	this->m_taskSignals = new TaskSignals(this);
	connect(this->m_taskSignals, &TaskSignals::dbChecked, this, &TcpServer::onDbChecked);
	connect(this->m_taskSignals, &TaskSignals::loginVerified, this, &TcpServer::onLoginVerified);
	connect(this->m_taskSignals, &TaskSignals::msgStored, this, &TcpServer::onMsgStored);
	connect(this->m_taskSignals, &TaskSignals::groupMsgStored, this, &TcpServer::onGroupMsgStored);

	// TODO(你来实现)：连接拉取回执信号（PullTask 实现后启用）
	// connect(this->m_taskSignals, &TaskSignals::pullLoaded, this, &TcpServer::onPullLoaded);

	//启动自检：试连 MySQL，结果回主线程打印（fail-fast 提示；失败不终止——服务端降级运行，需重启服务端进程恢复）
	this->m_taskPool->start(new DbCheckTask(this->m_taskSignals));

	//注册业务表
	this->registerHandlers();
}

TcpServer::~TcpServer()
{
	// 1. 停池：丢弃排队中的任务，等待在跑的任务干完
	//（任务持有 m_taskSignals 指针，必须先收工再让 QObject 父子树析构 TaskSignals）
	this->m_taskPool->clear();
	this->m_taskPool->waitForDone();

	// 2. 断开所有客户端连接
	for (TcpSocket* socket : m_fdSocketMap) {
		socket->disconnectFromHost();
	}

	// 3. 清空映射表
	m_fdSocketMap.clear();
	m_uidSocketMap.clear();
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

	qDebug() << QStringLiteral("新的连接： fd=%1，总连接数：%2").arg(socketDescriptor).arg(m_fdSocketMap.size() + 1);

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

void TcpServer::registerHandlers() {
	// 注册业务表
	this->m_handlers.insert(PacketType::Message, &TcpServer::handleMessage);
	this->m_handlers.insert(PacketType::PullRequest, &TcpServer::handlePullRequest);
	this->m_handlers.insert(PacketType::Heartbeat, &TcpServer::handleHeartbeat);
	this->m_handlers.insert(PacketType::LoginRequest, &TcpServer::handleLoginRequest);
	this->m_handlers.insert(PacketType::RegisterRequest, &TcpServer::handleRegisterRequest);
	this->m_handlers.insert(PacketType::Logout, &TcpServer::handleLogout);
}

void TcpServer::handleMessage(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// 推拉模型：只入库，从不转发消息本体
	// 数据体布局 = [msgId 13B][群标志1B][发送者5B][接收者4~5B][类型1B][内容...]

	// 登录校验：未登录的连接不允许发消息
	TcpSocket* srcSocket = this->m_fdSocketMap.value(descriptor);
	if (srcSocket == nullptr || srcSocket->getUid() == -1) {
		qDebug() << QStringLiteral("[Message] fd=%1 未登录，拒绝消息").arg(descriptor);
		return;
	}

	// 长度校验：最小合法长度 = msgId 13B + 载荷头 12B（群标志1 + 发送者5 + 接收者5 + 类型1）
	if (dataBody.size() < MSGID_LEN + 12) {
		qDebug() << QStringLiteral("[Message] fd=%1 数据体过短(%2字节)，丢弃").arg(descriptor).arg(dataBody.size());
		return;
	}

	// 切分：前 13B 是 msgId，其余是载荷
	// 载荷 = [群标志1B|发送者5B|接收者4~5B|类型1B|内容...]（不含 msgId 头，入库存的就是它，Pull 时原样下发）
	QString msgId = QString::fromUtf8(dataBody.left(MSGID_LEN));
	QByteArray payload = dataBody.mid(MSGID_LEN);

	// 解析载荷定位字段（固定偏移切分，与客户端 sendMessage 拼包格式一一对应）
	int groupFlag = payload[0] - '0';			//群标志：0 私聊 / 1 群聊
	int sendId = payload.mid(1, 5).toInt();		//发送者 uid（5B 十进制）
	if (groupFlag == 0) {
		//私聊：接收者 uid（5B）
		int recvId = payload.mid(6, 5).toInt();

		//消息入库
		this->m_taskPool->start(new StoreMsgTask(descriptor, msgId, recvId, payload, this->m_taskSignals));
	}
	else {
		//群聊：群号（4B，即 departmentID）
		int groupId = payload.mid(6, 4).toInt();

		//群消息按成员展开入库
		this->m_taskPool->start(new GroupMembersTask(descriptor, msgId, sendId, groupId, payload, this->m_taskSignals));
	}
}

void TcpServer::handlePullRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {

}

void TcpServer::handleHeartbeat(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// TODO(你来实现)：心跳包 = 保活 + 账本对账 ------------------------------------------------------------------
	// 1. 回发 HeartbeatResponse（空数据体，保活语义，保留旧实现这一句）
	// 2. 对账（拉模型的兜底触发器）：
	//    a. 取 socket 判空 + 已登录（getUid() != -1）
	//    b. 账本 = dataBody 前 CURSOR_LEN 字节（右对齐补零的 10 位十进制，toInt/qulonglong 解析）
	//       （注意兼容旧格式：dataBody 为空 = 客户端没带账本，只保活不对账）
	//    c. m_userMaxId[uid] > 账本 → sendPacket(MsgNotify, 空体, socket)（敲门，让客户端来 Pull）
	//    d. 不落后 → 只回心跳响应即可
	// ------------------------------------------------------------------------------------------------------------
}

void TcpServer::handleLoginRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// 解析内层消息体（账号 + "|" + 密码）

	// 1. 解包，校验格式，提取包中的账号密码
	QList<QByteArray> fields = dataBody.split('|');
	if (fields.size() < 2 || fields.at(0).isEmpty() || fields.at(1).isEmpty()) {
		qDebug() << QStringLiteral("[LoginRequest] fd=%1 数据体格式错误，回发失败").arg(descriptor);
		this->sendPacket(static_cast<quint16>(PacketType::LoginResponse), "0", this->m_fdSocketMap.value(descriptor));
		return;
	}
	QString account = QString::fromUtf8(fields.at(0));		//账号
	QString password = QString::fromUtf8(fields.at(1));		//密码

	// 2. 验证 + 快照打包外包给池（最重的 DB 业务：最多 5 条 SQL + JSON 序列化）
	//结果（成败/uid/快照字节）经 TaskSignals 回 onLoginVerified，由主线程拼响应/互踢/绑路由
	this->m_taskPool->start(new LoginTask(descriptor, account, password, this->m_taskSignals));
}

void TcpServer::handleRegisterRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// TODO: 解析注册数据体，写入用户表，返回注册结果
}

void TcpServer::handleLogout(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// 收到注销包：解绑该连接的用户路由映射

	// 1. 取出通信 socket，校验连接有效且已登录
	TcpSocket* socket = this->m_fdSocketMap.value(descriptor);
	if (socket == nullptr || socket->getUid() == -1) {
		return;		//连接不存在或本就未登录，无需解绑
	}

	// 2. 从路由表移除（校验映射仍指向本连接，防止误删新登录建立的映射）
	int uid = socket->getUid();
	if (this->m_uidSocketMap.value(uid) == socket) {
		this->m_uidSocketMap.remove(uid);
	}

	// 3. 重置 uid，后续 onClientDisconnected 不再重复清理
	socket->setUid(-1);

	qDebug() << QStringLiteral("[Logout] fd=%1 uid=%2 已解绑路由").arg(descriptor).arg(uid);
}

void TcpServer::sendPacket(quint16 packetType, const QByteArray& dataBody, TcpSocket* target) {
	//目标 socket 判空防御：连接可能已断开被移出映射表，避免空指针崩溃
	if (target == nullptr) {
		qDebug() << QStringLiteral("[sendPacket] 目标 socket 为空，丢弃 type=0x%1 的包")
			.arg(packetType, 4, 16, QChar('0'));
		return;
	}

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
	target->write(packet);
}


//槽函数
void TcpServer::onPacketReady(const QByteArray& data, int descriptor) {
	//	TcpServer（业务层）
	//		├─ 接收完整包
	//		├─ 解析外层包头（魔数、版本、包类型、数据长度）
	//		├─ 校验包头合法性（魔数、版本、长度）
	//		├─ 提取数据体
	//		├─ 按包类型分发到对应处理函数
	//		└─ 各处理函数执行具体业务（消息入库 / 登录 / 拉取请求等）

	// 1. 长度校验：至少要有完整外层包头
	if (data.size() < PACKET_HEADER_SIZE) {
		qDebug() << QStringLiteral("数据包过短(%1字节)，丢弃").arg(data.size());
		return;
	}

	// 2. 解析外层包头（均大端序）
	quint16 magic = qFromBigEndian<quint16>(data.constData() + 0);	// 魔数
	quint8  version = static_cast<quint8>(data[2]);					// 版本
	quint16 packetType = qFromBigEndian<quint16>(data.constData() + 3);	// 包类型
	quint16 dataLength = qFromBigEndian<quint16>(data.constData() + 5);	// 数据体长度

	// 3. 校验包头合法性
	if (magic != PACKET_MAGIC) {
		qDebug() << QStringLiteral("魔数错误:0x%1，丢弃").arg(magic, 4, 16, QChar('0'));
		return;
	}
	if (version != PACKET_VERSION) {
		qDebug() << QStringLiteral("版本不匹配:期望%1实际%2，丢弃").arg(PACKET_VERSION).arg(version);
		return;
	}
	if (dataLength != data.size() - PACKET_HEADER_SIZE) {
		qDebug() << QStringLiteral("数据长度不一致:头中%1实际%2，丢弃")
			.arg(dataLength).arg(data.size() - PACKET_HEADER_SIZE);
		return;
	}

	// 提取数据体（剥离外层包头）
	QByteArray dataBody = data.mid(PACKET_HEADER_SIZE);

	// 刷新 socket 最后活跃时间
	TcpSocket* srcSocket = this->m_fdSocketMap.value(descriptor);
	if (srcSocket) {
		srcSocket->touch();
	}

	// 查业务表分发业务
	auto it = this->m_handlers.find(static_cast<PacketType>(packetType));
	if (it != m_handlers.end()) {
		(this->*it.value())(data, dataBody, descriptor);
	}
	else {
		qDebug() << QStringLiteral("未知包类型:0x%1，fd=%2，丢弃")
			.arg(packetType, 4, 16, QChar('0')).arg(descriptor);
	}
}

void TcpServer::onClientDisconnected(int descriptor) {
	// 1. 取出通信 socket 对象
	TcpSocket* socket = m_fdSocketMap.value(descriptor);
	
	// 2. 如果已登录，从路由表移除
	if (socket && socket->getUid() != -1) {
		if (m_uidSocketMap.value(socket->getUid()) == socket) {
			m_uidSocketMap.remove(socket->getUid());
			qDebug() << QStringLiteral("用户 uid=%1 已从路由表移除").arg(socket->getUid());
		}
	}

	// 3. 从连接表移除
	m_fdSocketMap.remove(descriptor);

	// 4. 安全释放 socket 对象
	if (socket) {
		socket->deleteLater();
	}

	qDebug() << QStringLiteral("客户端断开 fd=%1，剩余连接数：%2")
		.arg(descriptor).arg(m_fdSocketMap.size());
}

void TcpServer::onCheckTimeout() {
	//遍历连接表，将心跳超时的 socket 踢除

	qint64 now = QDateTime::currentMSecsSinceEpoch();

	//遍历中可能触发 onClientDisconnected 修改 m_fdSocketMap，先取出待踢列表再动手
	QList<TcpSocket*> deadSockets;
	for (TcpSocket* socket : this->m_fdSocketMap) {
		if (now - socket->lastActive() > 30 * 1000) {
			deadSockets.append(socket);
		}
	}

	for (TcpSocket* socket : deadSockets) {
		qDebug() << QStringLiteral("[心跳超时] fd=%1 uid=%2 超过30s无活动，踢除").arg(socket->socketDescriptor()).arg(socket->getUid());

		// 服务端可能还往它身上写过东西 ：敲门包、LoginResponse、KickOut 包……
		// 这些可能还躺在发送缓冲里没被对方 ACK。如果用 abort，这些数据直接扔了；
		// 用 disconnectFromHost，TCP 会尽力重传投递（对端若只是网络抖动真还活着，数据能到）。
		socket->disconnectFromHost();		//触发现有 onClientDisconnected 清理链
	}
}

void TcpServer::onDbChecked(bool ok, const QString& error) {
	//启动自检结果：成功打印就绪；失败打印错误（降级运行，不终止——失败连接仍被登记复用，需重启服务端进程恢复）

	if (ok == true) {
		qDebug() << QStringLiteral("[DbCheck] 数据库连接自检通过，服务端就绪");
	}
	else {
		qWarning() << QStringLiteral("[DbCheck] 数据库连接失败（服务端降级运行）：%1").arg(error);
	}
}

void TcpServer::onLoginVerified(int descriptor, bool ok, int uid, const QByteArray& snapshot) {
	//LoginTask 结果处理：回发响应包 / 互踢 / 绑路由
	//（DB 验证和快照打包已在池线程完成，此处只做发包和内存路由操作）

	// 竞态防护：验证在途期间（MySQL 慢）客户端可能已断开，结果作废
	TcpSocket* socket = this->m_fdSocketMap.value(descriptor);
	if (socket == nullptr) {
		qDebug() << QStringLiteral("[Login] fd=%1 验证完成但连接已断开，结果作废").arg(descriptor);
		return;
	}

	// 1. 回发登录响应
	if (ok == false) {
		this->sendPacket(static_cast<quint16>(PacketType::LoginResponse), "0", socket);
		return;
	}
	else {
		QByteArray body = "1" + QString::number(uid).rightJustified(5, '0').toUtf8() + snapshot;
		this->sendPacket(static_cast<quint16>(PacketType::LoginResponse), body, socket);
	}

	// 2. 绑定路由表
	//	  重复登录踢下线
	TcpSocket* oldSocket = this->m_uidSocketMap.value(uid);
	if (oldSocket != nullptr && oldSocket != socket) {
		qDebug() << QStringLiteral("[KickOut] uid=%1 重复登录，踢除旧连接 fd=%2").arg(uid).arg(oldSocket->socketDescriptor());

		this->sendPacket(static_cast<quint16>(PacketType::KickOut), QByteArray(), oldSocket);
		oldSocket->disconnectFromHost();
	}
	socket->setUid(uid);
	this->m_uidSocketMap.insert(uid, socket);
}

void TcpServer::onMsgStored(int descriptor, const QString& msgId, bool ok, int recvId, quint64 rowId) {
	//StoreMsgTask 结果处理：回 ACK 给发送者 / 更新收件人高水位 / 敲门收件人

	// 入库失败：不回 ACK（发送端超时重传，INSERT IGNORE 幂等自愈），也不敲门（库里没货）
	if (ok == false) {
		qDebug() << QStringLiteral("[StoreMsg] fd=%1 msgId=%2 入库失败，等待发送端重传").arg(descriptor).arg(msgId);
		return;
	}

	// 回 ACK 给发送者（fd 判空：断线只影响 ACK；消息已落库，收件人的敲门不受影响，照发）
	TcpSocket* srcSocket = this->m_fdSocketMap.value(descriptor);
	if (srcSocket != nullptr) {
		this->sendPacket(static_cast<quint16>(PacketType::MessageAck), msgId.toUtf8(), srcSocket);
	}

	// rowId == 0：INSERT IGNORE 幂等命中（重传的重复包）——首次入库时已敲门，到此为止
	if (rowId == 0) {
		return;
	}

	// 更新收件人的高水位（心跳对账数据源：m_userMaxId[uid] > 账本 → 补敲门）
	if (rowId > this->m_userMaxId.value(recvId, 0)) {
		this->m_userMaxId[recvId] = rowId;
	}

	// 收件人在线则敲门
	TcpSocket* targetSocket = this->m_uidSocketMap.value(recvId);
	if (targetSocket != nullptr) {
		this->sendPacket(static_cast<quint16>(PacketType::MsgNotify), QByteArray(), targetSocket);
		qDebug() << QStringLiteral("[Notify] uid=%1 在线，已敲门（新消息 id=%2）").arg(recvId).arg(rowId);
	}
}

void TcpServer::onGroupMsgStored(int descriptor, const QString& msgId, bool ok, const QList<int>& memberIds, quint64 rowId) {
	//GroupMembersTask 结果处理：回 ACK 给发送者 / 对在线成员逐个敲门

	// 批量入库失败：不回 ACK（发送端超时重传），不敲门
	if (ok == false) {
		qDebug() << QStringLiteral("[GroupStore] fd=%1 msgId=%2 批量入库失败，等待发送端重传").arg(descriptor).arg(msgId);
		return;
	}

	// 回 ACK 给发送者（fd 判空：断线只影响 ACK；消息已落库，成员的敲门照发）
	TcpSocket* srcSocket = this->m_fdSocketMap.value(descriptor);
	if (srcSocket != nullptr) {
		this->sendPacket(static_cast<quint16>(PacketType::MessageAck), msgId.toUtf8(), srcSocket);
	}

	// rowId == 0：整批幂等命中（重传的重复包）——首次入库时已敲过门
	if (rowId == 0) {
		return;
	}

	// 对每个入库成员：更新高水位 + 在线则敲门（敲门是"通知在线者来拉"，查路由表即可）
	int knocked = 0;		//敲门计数（日志用）
	for (int memberId : memberIds) {
		//高水位：各成员实际行 id ≤ 本批最大 rowId，用最大值是安全近似（最坏引发一次空 Pull，幂等无害）
		if (rowId > this->m_userMaxId.value(memberId, 0)) {
			this->m_userMaxId[memberId] = rowId;
		}

		TcpSocket* memberSocket = this->m_uidSocketMap.value(memberId);
		if (memberSocket != nullptr) {
			this->sendPacket(static_cast<quint16>(PacketType::MsgNotify), QByteArray(), memberSocket);
			knocked++;
		}
	}

	qDebug() << QStringLiteral("[GroupNotify] 群消息分发完成：%1 人入库，%2 人在线已敲门").arg(memberIds.size()).arg(knocked);
}


// TODO(你来实现)：以下两个拉取相关实现，按 TcpServer.h 对应 TODO 注释的职责说明逐个实现 ----------
//
// void TcpServer::handlePullRequest(...)
// void TcpServer::onPullLoaded(...)
//
// 提示（封包格式速查，Protocol.h 也有注释）：
//   MessageAck 数据体   = msgId 13B 原文
//   MsgNotify 数据体    = 空
//   PullResponse 数据体 = QString::number(newCursor).rightJustified(CURSOR_LEN, '0')
//                         + QChar(条数) + N × (msgId 13B + 载荷原文)
//   解析 PullRequest 数据体 = 前 CURSOR_LEN 字节 toInt()（注意 QString::toLongLong 更稳，id 是 quint64）
// ----------------------------------------------------------------------------------------------------------------
