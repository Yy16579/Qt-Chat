#include "TcpServer.h"

#include <QDebug>
#include <QtEndian>
#include <QSqlQuery> 
#include <QSqlError>


TcpServer::TcpServer(int port)
	: m_port(port)
{
	this->registerHandlers();
}

TcpServer::~TcpServer()
{
	// 1. 断开所有客户端连接
	for (TcpSocket* socket : m_fdSocketMap) {
		socket->disconnectFromHost();
	}

	// 2. 清空映射表
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
		
		// 查路由表精准转发（转发原包，服务端不关心载荷内容）
		TcpSocket* targetSocket = this->m_uidSocketMap.value(recvId);
		if (targetSocket != nullptr) {
			//对方在线
			targetSocket->write(fullPacket);
			qDebug() << QStringLiteral("[Message] %1 → %2 转发成功").arg(sendId).arg(recvId);
		}
		else {
			//对方不在线
			
		}
	}
	else {
		//为群聊
		recvId = dataBody.mid(6, 4).toInt();

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

	// 2. 查询数据库验证（双方式登录）
	QString result = "0";		//结果标志：'1'成功 '0'失败
	QString empID = "";			//用户 employeeID（成功时有效）
	bool found = false;			//是否查到账号记录

	//账号为纯数字时，才按 employeeID 查询（int 列绑定数字，避免字符串隐式转换）
	bool isNumber = false;
	int accountNum = account.toInt(&isNumber);

	QSqlQuery query;
	if (isNumber) {
		//方式一：按 employeeID 查询
		query.prepare("SELECT `code` FROM `tab_accounts` WHERE `employeeID` = ?");
		query.addBindValue(accountNum);
		query.exec();
		found = query.next();

		if (found == true && query.value(0).toString() == password) {
			//密码正确
			result = "1";
			empID = QString::number(accountNum);
		}
		//employeeID 未命中记录 → 继续方式二
	}

	if (found == false) {
		//方式二：按 account 字段查询（账号非数字，或方式一未命中记录）
		query.prepare("SELECT `code`, `employeeID` FROM `tab_accounts` WHERE `account` = ?");
		query.addBindValue(account);
		query.exec();
		if (query.next() == true) {
			found = true;
			if (query.value(0).toString() == password) {
				//密码正确
				result = "1";
				empID = query.value(1).toString();
			}
		}
	}

	// 3. 打包回发登录响应
	//格式：成功 = 结果标志1B + uid5B（客户端按 mid(1,5) 定长解析，uid 不足5位需补零）；失败 = "0"
	QString data;
	if (result == "1") {
		data = result + empID.rightJustified(5, '0');
		qDebug() << QStringLiteral("[LoginRequest] fd=%1 登录成功，uid=%2").arg(descriptor).arg(empID);
	}
	else {
		data = result;
		qDebug() << QStringLiteral("[LoginRequest] fd=%1 账号密码验证失败").arg(descriptor);
	}
	this->sendPacket(static_cast<quint16>(PacketType::LoginResponse), data.toUtf8(), this->m_fdSocketMap.value(descriptor));

	// 4. 若成功登录，添加至路由表
	if (result == "1") {
		int uid = empID.toInt();
		TcpSocket* socket = this->m_fdSocketMap.value(descriptor);
		
		if (this->m_uidSocketMap.contains(uid) == true) {
			// 路由表中存在登录记录
			// 重复登录：断开旧连接（触发 onClientDisconnected 清理）
			TcpSocket* oldSocket = this->m_uidSocketMap.value(uid);
			
			if (oldSocket == socket) {
				return;
			}	
			oldSocket->disconnectFromHost();
		}

		// 同连接切换账号：先移除本连接旧 uid 的映射，避免路由表残留悬空指针
		int oldUid = socket->getUid();
		if (oldUid != -1 && oldUid != uid) {
			this->m_uidSocketMap.remove(oldUid);
		}

		socket->setUid(uid);				//设置 socket 的 uid 字段
		this->m_uidSocketMap.insert(uid, socket);	//添加至路由表
	}
}

void TcpServer::handleRegisterRequest(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// TODO: 解析注册数据体，写入用户表，返回注册结果
}

void TcpServer::handleDbQuery(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// TODO: 解析查询请求（SQL+参数），执行数据库查询，打包结果回发
}

void TcpServer::handleHeartbeat(const QByteArray& fullPacket, const QByteArray& dataBody, int descriptor) {
	// TODO: 更新客户端最后活跃时间（后期做心跳超时检测）
}

void TcpServer::sendPacket(quint16 packetType, const QByteArray& dataBody, TcpSocket* target) {
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

	// 4. 提取数据体（剥离外层包头）
	QByteArray dataBody = data.mid(PACKET_HEADER_SIZE);

	// 5. 查业务表分发业务
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


