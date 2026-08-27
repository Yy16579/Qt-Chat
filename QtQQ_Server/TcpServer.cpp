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
	connect(this->m_taskSignals, &TaskSignals::groupMembersLoaded, this, &TcpServer::onGroupMembersLoaded);
	connect(this->m_taskSignals, &TaskSignals::offlineLoaded, this, &TcpServer::onOfflineLoaded);

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
	this->m_handlers.insert(PacketType::LoginRequest, &TcpServer::handleLoginRequest);
	this->m_handlers.insert(PacketType::RegisterRequest, &TcpServer::handleRegisterRequest);
	this->m_handlers.insert(PacketType::DbQuery, &TcpServer::handleDbQuery);
	this->m_handlers.insert(PacketType::Heartbeat, &TcpServer::handleHeartbeat);
	this->m_handlers.insert(PacketType::Logout, &TcpServer::handleLogout);
}

void TcpServer::handleMessage(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// 解析内层消息体（群标志+发送者ID+接收者ID+消息类型+消息内容），查路由表转发给目标用户
	
	// 1. 登录校验：未登录的连接不允许发消息
	TcpSocket* srcSocket = this->m_fdSocketMap.value(descriptor);
	if (srcSocket == nullptr || srcSocket->getUid() == -1) {
		qDebug() << QStringLiteral("[Message] fd=%1 未登录，拒绝转发").arg(descriptor);
		return;
	}

	// 2. 长度校验：私聊最小头 = 群标志1 + 发送者5 + 接收者5 + 消息类型1 = 12字节
	if (dataBody.size() < 12) {
		qDebug() << QStringLiteral("[Message] 数据体过短(%1字节)，丢弃").arg(dataBody.size());
		return;
	}

	// 3. 解析地址字段（固定位置切分，与客户端 sendMessage 拼接格式对应）
	int groupFlag = dataBody[0] - '0';
	int sendId = dataBody.mid(1, 5).toInt();
	int recvId;
	if (groupFlag == 0) {
		//为私聊
		recvId = dataBody.mid(6, 5).toInt();

		//查路由表精准转发（转发原包，服务端不关心载荷内容）
		TcpSocket* targetSocket = this->m_uidSocketMap.value(recvId);
		if (targetSocket != nullptr) {
			//对方在线：主线程直接转发（零 DB，快业务）
			targetSocket->write(fullPacket);
			qDebug() << QStringLiteral("[Message] %1 → %2 转发成功").arg(sendId).arg(recvId);
		}
		else {
			//对方不在线：入库暂存外包给池
			this->m_taskPool->start(new InsertOfflineTask(recvId, dataBody));
			qDebug() << QStringLiteral("[Offline] uid=%1 离线，消息入库暂存").arg(recvId);
		}
	}
	else {
		//为群聊：recvId 为 4 位群号（即 departmentID）
		recvId = dataBody.mid(6, 4).toInt();

		//群成员查询外包给池
		//原包 fullPacket/dataBody 随任务往返（查询期间消息"在途"），查完回 onGroupMembersLoaded 分发
		this->m_taskPool->start(new GroupMembersTask(descriptor, sendId, recvId, fullPacket, dataBody, this->m_taskSignals));
	}
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

void TcpServer::handleRegisterRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// TODO: 解析注册数据体，写入用户表，返回注册结果
}

void TcpServer::handleDbQuery(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// TODO: 解析查询请求（SQL+参数），执行数据库查询，打包结果回发
}

void TcpServer::handleHeartbeat(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// 收到心跳包，回发心跳响应
	this->sendPacket(static_cast<quint16>(PacketType::HeartbeatResponse), QByteArray(), m_fdSocketMap.value(descriptor));
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
	//		└─ 各处理函数执行具体业务（消息转发 / 登录 / 数据库查询等）

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

		// 服务端可能还往它身上写过东西 ：踢线前刚转发的消息、LoginResponse、KickOut 包……
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
	//LoginTask 结果处理： 回发响应包 / 绑路由 / 推送离线表
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

	// 3. 拉取离线消息：外包给池（结果回 onOfflineLoaded 逐条推送）
	this->m_taskPool->start(new LoadOfflineTask(descriptor, uid, this->m_taskSignals));
}

void TcpServer::onGroupMembersLoaded(int descriptor, int sendId, int groupId, const QList<int>& memberIds,
	const QByteArray& fullPacket, const QByteArray& dataBody) {
	//GroupMembersTask 结果处理：逐成员分发（在线直转 / 离线入库）
	//特例说明：不判空来源 socket——消息已到达服务端，发送者断线不影响其他成员收消息

	int onlineCount = 0;		//在线直转计数
	int offlineCount = 0;		//离线入库计数

	for (int memberId : memberIds) {
		//跳过发送者本人（自己的气泡由本地渲染，服务端不回发）
		if (memberId == sendId) {
			continue;
		}

		TcpSocket* memberSocket = this->m_uidSocketMap.value(memberId);
		if (memberSocket != nullptr) {
			//在线：转发原始包
			memberSocket->write(fullPacket);
			onlineCount++;
		}
		else {
			//离线：入库暂存，等其下次登录推送
			this->m_taskPool->start(new InsertOfflineTask(memberId, dataBody));
			offlineCount++;
		}
	}

	qDebug() << QStringLiteral("[Group] 群%1 消息分发完成：%2人在线直转，%3人离线入库")
		.arg(groupId).arg(onlineCount).arg(offlineCount);
}

void TcpServer::onOfflineLoaded(int descriptor, int uid, const QList<QByteArray>& contents) {
	//LoadOfflineTask 结果处理：逐条推送离线消息

	//竞态防护：拉取在途期间客户端可能已断开，结果作废
	TcpSocket* socket = this->m_fdSocketMap.value(descriptor);
	if (socket == nullptr) {
		qDebug() << QStringLiteral("[Offline] fd=%1 离线消息就绪但连接已断开，放弃推送").arg(descriptor);
		return;
	}

	//逐条推送（存的是 dataBody 原文，此处重新封 Message 包头）
	for (const QByteArray& content : contents) {
		this->sendPacket(static_cast<quint16>(PacketType::Message), content, socket);
	}
}

