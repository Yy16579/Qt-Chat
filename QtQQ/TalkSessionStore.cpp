#include "TalkSessionStore.h"
#include "TcpClient.h"
#include "WindowManager.h"

#include <QSqlQuery>
#include <QSqlError>


TalkSessionStore::TalkSessionStore()
	: QObject(nullptr)
	, m_isOpen(false)
{
	// 监听 TcpClient 的消息接收信号（仓库是网络消息的第一站）
	connect(&TcpClient::getInstance(), &TcpClient::signalMessageReceived, this, &TalkSessionStore::onTcpMessage);
}

TalkSessionStore::~TalkSessionStore()
{}

TalkSessionStore& TalkSessionStore::getInstance() {
	static TalkSessionStore instance;
	return instance;
}

void TalkSessionStore::open(int empID) {
	//SQLite 命名连接
	this->m_db = QSqlDatabase::addDatabase("QSQLITE", "localMsg");

	//按账号分库：msg_10001.db，换账号天然隔离，文件在 exe 工作目录
	this->m_db.setDatabaseName(QString("msg_%1.db").arg(empID));
	this->m_isOpen = this->m_db.open();
	if (!this->m_isOpen) {
		//打开失败的兜底：消息收发不受影响（广播照常），仅丢失持久化
		qDebug() << QStringLiteral("[LocalStore] 本地库打开失败：") << this->m_db.lastError().text();
		return;
	}

	//建表（IF NOT EXISTS：老库文件已存在则跳过，新库首次创建）
	QSqlQuery query(this->m_db);
	query.exec("CREATE TABLE IF NOT EXISTS `tab_msg` ("
		"`id`         INTEGER PRIMARY KEY AUTOINCREMENT,"
		"`talk_id`    INTEGER NOT NULL,"			//会话ID（私聊=对方employeeID，群聊=群ID）
		"`send_id`    INTEGER NOT NULL,"
		"`recv_id`    INTEGER NOT NULL,"
		"`msg_type`   INTEGER NOT NULL,"
		"`content`    TEXT    NOT NULL,"			//wire 格式（与网络传输同构，存取零转换）
		"`mine`       INTEGER NOT NULL,"			//1=我发的（右侧气泡）
		"`created_at` DATETIME DEFAULT CURRENT_TIMESTAMP"
		")");
}

QList<MsgRecord> TalkSessionStore::records(int uid) {
	//查询该会话的全部历史记录（按 id 升序 = 消息时间序），供窗口重放
	QList<MsgRecord> list;

	if (!this->m_isOpen) {
		return list;		//库未打开（未登录/open失败），返回空列表
	}

	QSqlQuery query(this->m_db);
	query.prepare("SELECT `send_id`, `recv_id`, `msg_type`, `content`, `mine` FROM `tab_msg` WHERE `talk_id` = ? ORDER BY `id` ASC");
	query.addBindValue(uid);
	query.exec();

	while (query.next()) {
		MsgRecord record;
		record.groupFlag = 0;		//表未存此列（renderRecord 不读它，占位即可）
		record.sendId = query.value(0).toInt();
		record.recvId = query.value(1).toInt();
		record.msgType = query.value(2).toInt();
		record.msg = query.value(3).toString();
		record.mine = query.value(4).toBool();
		list.append(record);
	}

	return list;
}

void TalkSessionStore::appendSelfRecord(int uid, const MsgRecord& record) {
	//将自己发的消息入库（发送路径已即时显示右侧气泡，此处静默入库，不广播）
	//库未打开时跳过——消息收发不受影响，仅不持久化
	if (!this->m_isOpen) {
		return;
	}

	QSqlQuery query(this->m_db);
	query.prepare("INSERT INTO `tab_msg` (`talk_id`, `send_id`, `recv_id`, `msg_type`, `content`, `mine`) "
		"VALUES (?, ?, ?, ?, ?, ?)");
	query.addBindValue(uid);
	query.addBindValue(record.sendId);
	query.addBindValue(record.recvId);
	query.addBindValue(record.msgType);
	query.addBindValue(record.msg);
	query.addBindValue(record.mine ? 1 : 0);
	query.exec();
}

void TalkSessionStore::close() {
	//退出登录：关闭本地库连接（数据文件保留，下次登录历史还在）
	if (this->m_isOpen) {
		this->m_db.close();

		//命名连接必须显式移除，否则下次登录 addDatabase 同名连接报"重复连接"警告
		//注意：本函数内没有存活的全局 QSqlQuery 对象，close 后立即移除是安全的
		this->m_db = QSqlDatabase();		//先释放对连接的引用，再移除连接
		QSqlDatabase::removeDatabase("localMsg");

		this->m_isOpen = false;
	}
}


//槽函数
void TalkSessionStore::onTcpMessage(int groupFlag, int sendId, int recvId, int msgType, const QString& msg) {
	//网络消息入口

	// 1. 路由判定
	int uid = -1;		//消息归属的 uid

	if (groupFlag == 0) {
		//私聊：uid = sendId
		//recvId 必须是我（过滤"对方发给别人"的消息）
		int myEmpID = WindowManager::getInstance().m_empID;
		if (recvId != myEmpID) {
			return;
		}
		uid = sendId;
	}
	else {
		//群聊：uid = 群ID
		//丢弃自己发送的消息（避免自己消息双显）
		int myEmpID = WindowManager::getInstance().m_empID;
		if (sendId == myEmpID) {
			return;
		}
		uid = recvId;
	}

	// 2. 入库（wire 格式原样保存，网络收到的消息 mine=false，渲染左侧气泡）
	//库未打开时跳过
	if (this->m_isOpen) {
		QSqlQuery query(this->m_db);
		query.prepare("INSERT INTO `tab_msg` (`talk_id`, `send_id`, `recv_id`, `msg_type`, `content`, `mine`) VALUES (?, ?, ?, ?, ?, ?)");
		query.addBindValue(uid);
		query.addBindValue(sendId);
		query.addBindValue(recvId);
		query.addBindValue(msgType);
		query.addBindValue(msg);
		query.addBindValue(0);		//网络收到的消息 mine=false
		query.exec();
	}

	// 3. 广播
	emit this->signalMessageStored(uid, groupFlag, sendId, recvId, msgType, msg);
}

