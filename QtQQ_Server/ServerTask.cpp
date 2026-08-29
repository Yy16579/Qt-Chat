#include "ServerTask.h"

#include <QSettings>
#include <QCoreApplication>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QDebug>


DbConnPool& DbConnPool::getInstance() {
	static DbConnPool instance;
	return instance;
}

QSqlDatabase DbConnPool::get() {
	// 获取当前线程专属连接
	// 查表命中直接复用，未命中创建新连接

	Qt::HANDLE tid = QThread::currentThreadId();	//获取当前线程 tid

	// 加锁查表（锁只保护容器读写，临界区极小）（持锁：不同线程并发 insert 不同 key 时保护 QHash 结构完整）
	//   大括号的作用 = 限定 QMutexLocker 的生命周期：出了作用域 locker 析构、锁释放
	{
		QMutexLocker locker(&this->m_mutex);

		// 命中：连接已创建，直接返回 database
		if (this->m_connNames.contains(tid) == true) {
			return QSqlDatabase::database(this->m_connNames.value(tid));
		}
	}		
	//出作用域：析构自动解锁

	// 未命中：创建连接并登记
	// 生成连接名（pool_ + 线程id，保证全局唯一）
	QString connName = QStringLiteral("pool_%1").arg(reinterpret_cast<qulonglong>(tid));
	{
		QMutexLocker locker(&this->m_mutex);
		this->createConnection(connName);

		//无论成败都记入表：失败时后续 get() 返回一个未 open 的连接，
		//exec() 会失败并走任务的空结果分支，不崩溃（MySQL 未启动时服务端降级运行）
		this->m_connNames.insert(tid, connName);
	}

	return QSqlDatabase::database(connName);
}

bool DbConnPool::createConnection(const QString& connName) {
	//从 config.ini 的 [Database] 节读取连接参数（与客户端 [Tcp] 节同一文件风格）
	QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
	QSettings settings(configPath, QSettings::IniFormat);

	//命名连接：第二个参数不能省，省了就是默认连接（跨线程使用 = 未定义行为）
	QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", connName);
	db.setHostName(settings.value("Database/host").toString());
	db.setPort(settings.value("Database/port").toInt());
	db.setUserName(settings.value("Database/userName").toString());
	db.setPassword(settings.value("Database/password").toString());
	db.setDatabaseName(settings.value("Database/databaseName").toString());

	if (db.open() == true) {
		qDebug() << QStringLiteral("[DbConnPool] 线程%1 建立数据库连接 %2 成功")
			.arg(reinterpret_cast<qulonglong>(QThread::currentThreadId())).arg(connName);
		return true;
	}
	else {
		qDebug() << QStringLiteral("[DbConnPool] 线程%1 数据库连接失败：%2")
			.arg(reinterpret_cast<qulonglong>(QThread::currentThreadId())).arg(db.lastError().text());
		return false;
	}
}


TaskSignals::TaskSignals(QObject* parent)
	: QObject(parent)
{}


//====================================================== 池任务实现 =====================================================

//---------- DbCheckTask ----------

DbCheckTask::DbCheckTask(TaskSignals* taskSignals)
	: m_signals(taskSignals)
{
}

void DbCheckTask::run() {
	//试连数据库：结果（成败 + 错误信息）回传主线程打印
	//连接失败时 get() 返回未 open 的连接，isOpen() = false，不崩溃
	QSqlDatabase db = DbConnPool::getInstance().get();
	emit m_signals->dbChecked(db.isOpen(), db.lastError().text());
}


//---------- LoginTask ----------

LoginTask::LoginTask(int descriptor, const QString& account, const QString& password, TaskSignals* taskSignals)
	: m_descriptor(descriptor)
	, m_account(account)
	, m_password(password)
	, m_signals(taskSignals)
{
}

void LoginTask::run() {
	//登录验证：双方式（employeeID / account 字段）+ 成功时打包通讯录快照
	//只产出数据（验证结论 + 快照字节），发包 / 互踢 / 绑路由留给主线程结果槽

	QSqlDatabase db = DbConnPool::getInstance().get();
	QSqlQuery query(db);		//★ 必须显式传 db，QSqlQuery 默认找 default 连接（本项目无默认连接）

	QString result = "0";		//结果标志：'1'成功 '0'失败
	QString empID = "";			//用户 employeeID（成功时有效）
	bool found = false;			//是否查到账号记录

	//账号为纯数字时，才按 employeeID 查询（int 列绑定数字，避免字符串隐式转换）
	bool isNumber = false;
	int accountNum = m_account.toInt(&isNumber);

	if (isNumber) {
		//方式一：按 employeeID 查询
		query.prepare("SELECT `code` FROM `tab_accounts` WHERE `employeeID` = ?");
		query.addBindValue(accountNum);
		query.exec();
		found = query.next();

		if (found == true && query.value(0).toString() == m_password) {
			//密码正确
			result = "1";
			empID = QString::number(accountNum);
		}
		//employeeID 未命中记录 → 继续方式二
	}

	if (found == false) {
		//方式二：按 account 字段查询（账号非数字，或方式一未命中记录）
		query.prepare("SELECT `code`, `employeeID` FROM `tab_accounts` WHERE `account` = ?");
		query.addBindValue(m_account);
		query.exec();
		if (query.next() == true) {
			found = true;
			if (query.value(0).toString() == m_password) {
				//密码正确
				result = "1";
				empID = query.value(1).toString();
			}
		}
	}

	//验证成功 → 打包通讯录快照（随登录响应一并下发）
	if (result == "1") {
		QByteArray snapshot = this->buildContactSnapshot();
		qDebug() << QStringLiteral("[LoginTask] fd=%1 登录验证成功，uid=%2").arg(m_descriptor).arg(empID);
		emit m_signals->loginVerified(m_descriptor, true, empID.toInt(), snapshot);
	}
	else {
		qDebug() << QStringLiteral("[LoginTask] fd=%1 账号密码验证失败").arg(m_descriptor);
		emit m_signals->loginVerified(m_descriptor, false, -1, QByteArray());
	}
}

QByteArray LoginTask::buildContactSnapshot() {
	//通讯录快照打包：两张表全量 → 紧凑 JSON

	// 打包 tab_employees 全量表（过滤 status = 0 的用户）
	QJsonArray employees;

	QSqlDatabase db = DbConnPool::getInstance().get();
	QSqlQuery query(db);
	query.prepare("SELECT `employeeID`, `employee_name`, `employee_sign`, `picture`, `departmentID` FROM `tab_employees` WHERE `status` = ?");
	query.addBindValue(1);
	query.exec();
	while (query.next() == true) {
		QJsonObject emp;
		emp.insert("employeeID", query.value(0).toInt());
		emp.insert("employee_name", query.value(1).toString());
		emp.insert("employee_sign", query.value(2).toString());
		emp.insert("picture", query.value(3).toString());
		emp.insert("departmentID", query.value(4).toInt());

		employees.append(emp);
	}

	// 打包 tab_department 全量表（无过滤）
	QJsonArray department;

	query.prepare("SELECT `departmentID`, `department_name`, `sign`, `picture` FROM `tab_department`");
	query.exec();
	while (query.next() == true) {
		QJsonObject dep;
		dep.insert("departmentID", query.value(0).toInt());
		dep.insert("department_name", query.value(1).toString());
		dep.insert("sign", query.value(2).toString());
		dep.insert("picture", query.value(3).toString());

		department.append(dep);
	}

	// 数组挂到 root 对象的 key 下
	QJsonObject root;
	root.insert("tab_employees", employees);
	root.insert("tab_department", department);

	// Document 打包成 QByteArray（网络发送的形态）
	QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);		// Compact = 压缩无空格（省流量）

	qDebug() << QStringLiteral("[Snapshot] 通讯录快照已打包：%1名员工 / %2个部门").arg(employees.size()).arg(department.size());
	return bytes;
}


//---------- StoreMsgTask ----------

StoreMsgTask::StoreMsgTask(int descriptor, QString msgId, int recvId, QByteArray content, TaskSignals* taskSignals)
	: m_descriptor(descriptor)
	, m_msgId(msgId)
	, m_recvId(recvId)
	,m_content(content)
	,m_signals(taskSignals)
{}

void StoreMsgTask::run() {
	//私聊消息入库：INSERT IGNORE（幂等，msg_id 唯一键冲突时静默跳过）
	//只产出数据（成败 + 本行 id），ACK / 高水位 / 敲门留给主线程结果槽

	QSqlDatabase db = DbConnPool::getInstance().get();
	QSqlQuery query(db);		//★ 必须显式传 db（本项目无默认连接）

	query.prepare("INSERT IGNORE INTO `tab_msg` (`recv_id`, `msg_id`, `content`) VALUES (?, ?, ?)");
	query.addBindValue(m_recvId);
	query.addBindValue(m_msgId);
	query.addBindValue(m_content);

	bool ok = query.exec();

	//lastInsertId()：插了新行 → 本行自增 id；撞唯一键被 IGNORE → 0（重传重复包的暗号）
	//（exec 成功 + rowId 0 = 幂等命中，也算"成功"——货已在档案馆，主线程据此跳过重复敲门）
	quint64 rowId = 0;
	if (ok == true) {
		rowId = query.lastInsertId().toULongLong();
	}
	else {
		qDebug() << QStringLiteral("[StoreMsg] fd=%1 uid=%2 入库失败：%3")
			.arg(m_descriptor).arg(m_recvId).arg(query.lastError().text());
	}

	emit m_signals->msgStored(m_descriptor, m_msgId, ok, m_recvId, rowId);
}


//---------- GroupMembersTask ----------

GroupMembersTask::GroupMembersTask(int descriptor, QString msgId, int sendId, int groupId, QByteArray content, TaskSignals* taskSignals)
	: m_descriptor(descriptor)
	, m_msgId(msgId)
	, m_sendId(sendId)
	, m_groupId(groupId)
	, m_content(content)
	, m_signals(taskSignals)
{}

void GroupMembersTask::run() {
	//群消息分发入库：查群成员 → 按成员批量 INSERT（一人一行，跳过发送者本人）
	//与旧实现的区别——不再"在线转发/离线入库"分流，全部入库（拉模型下成员上线自己 Pull）

	QSqlDatabase db = DbConnPool::getInstance().get();
	QSqlQuery query(db);

	QList<int> memberIds;		//实际入库的成员列表（跳过发送者后）

	// 1. 查询公司群 ID（departmentID）
	query.prepare("SELECT `departmentID` FROM `tab_department` WHERE `department_name` = ?");
	query.addBindValue(QStringLiteral("公司群"));
	query.exec();
	query.next();
	int compDepID = query.value(0).toInt();

	// 2. 按群类型查成员列表（公司群 = 全部在职 / 普通群 = 该部门在职）
	if (m_groupId == compDepID) {
		query.prepare("SELECT `employeeID` FROM `tab_employees` WHERE `status` = ?");
		query.addBindValue(1);
	}
	else {
		query.prepare("SELECT `employeeID` FROM `tab_employees` WHERE `status` = ? AND `departmentID` = ?");
		query.addBindValue(1);
		query.addBindValue(m_groupId);
	}
	query.exec();

	//结果集先取出存列表（QSqlQuery 单结果集遍历，防止后续复用被破坏）
	while (query.next() == true) {
		int memberId = query.value(0).toInt();

		//跳过发送者本人（自己的消息不入库——客户端已本地渲染）
		if (memberId == m_sendId) {
			continue;
		}
		memberIds << memberId;
	}

	// 3. 批量入库：循环 INSERT IGNORE（一人一行，recv_id = 各成员 uid，msg_id / content 相同）
	bool ok = true;				//批量成败标志（任一行失败即 false）
	quint64 maxRowId = 0;		//本批各行 id 的最大值（整批幂等命中时保持 0）

	for (int memberId : memberIds) {
		QSqlQuery insertQuery(db);
		insertQuery.prepare("INSERT IGNORE INTO `tab_msg` (`recv_id`, `msg_id`, `content`) VALUES (?, ?, ?)");
		insertQuery.addBindValue(memberId);
		insertQuery.addBindValue(m_msgId);
		insertQuery.addBindValue(m_content);

		if (insertQuery.exec() == false) {
			ok = false;
			qDebug() << QStringLiteral("[GroupStore] uid=%1 入库失败：%2")
				.arg(memberId).arg(insertQuery.lastError().text());
			continue;
		}

		//lastInsertId()：新行 → 本行 id；幂等命中 → 0（不计入 maxRowId）
		quint64 rowId = insertQuery.lastInsertId().toULongLong();
		if (rowId > maxRowId) {
			maxRowId = rowId;
		}
	}

	qDebug() << QStringLiteral("[GroupStore] 群%1 消息入库完成：%2 人，本批最大 id=%3")
		.arg(m_groupId).arg(memberIds.size()).arg(maxRowId);

	//回执带 memberIds：主线程据此对每个入库成员更新高水位 + 敲门（在线者），零 DB 查询
	emit m_signals->groupMsgStored(m_descriptor, m_msgId, ok, memberIds, maxRowId);
}


//---------- PullTask ----------
// TODO(你来实现)：分页拉取任务
// SQL：SELECT `id`, `content` FROM `tab_msg` WHERE `recv_id` = ? AND `id` > ? ORDER BY `id` ASC LIMIT 20
// 遍历结果集 → QList<QByteArray> contents（content 原文列表）
// newCursor = 最后一条的 id；空结果集 → newCursor = 传入的原账本（客户端停止续拉的信号）
// → emit m_signals->pullLoaded(descriptor, newCursor, contents)
// ------------------------------------------------------------------------------------------------------------------------------
