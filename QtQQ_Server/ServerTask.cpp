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
	//通讯录快照打包：两张表全量 → 紧凑 JSON（原 TcpServer::buildContactSnapshot 平移）

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


//---------- GroupMembersTask ----------

GroupMembersTask::GroupMembersTask(int descriptor, int sendId, int groupId,
	const QByteArray& fullPacket, const QByteArray& dataBody, TaskSignals* taskSignals)
	: m_descriptor(descriptor)
	, m_sendId(sendId)
	, m_groupId(groupId)
	, m_fullPacket(fullPacket)
	, m_dataBody(dataBody)
	, m_signals(taskSignals)
{
}

void GroupMembersTask::run() {
	//群成员查询：公司群查全部在职员工，普通群查该部门在职员工
	//原包随任务往返（查询期间"在途"），查完回主线程做在线转发 / 离线入库分发

	QSqlDatabase db = DbConnPool::getInstance().get();
	QSqlQuery query(db);

	QList<int> memberIds;

	// 1. 查询公司群 ID
	query.prepare("SELECT `departmentID` FROM `tab_department` WHERE `department_name` = ?");
	query.addBindValue(QStringLiteral("公司群"));
	query.exec();
	query.next();
	int compDepID = query.value(0).toInt();

	// 2. 按群类型查成员列表
	if (m_groupId == compDepID) {
		//公司群：查全部在职员工
		query.prepare("SELECT `employeeID` FROM `tab_employees` WHERE `status` = ?");
		query.addBindValue(1);
	}
	else {
		//普通群：查该部门在职员工
		query.prepare("SELECT `employeeID` FROM `tab_employees` WHERE `status` = ? AND `departmentID` = ?");
		query.addBindValue(1);
		query.addBindValue(m_groupId);
	}
	query.exec();

	//结果集先取出存列表（QSqlQuery 单结果集遍历，防止后续复用被破坏）
	while (query.next() == true) {
		memberIds << query.value(0).toInt();
	}

	// 3. 结果回传主线程（原包一并带回）
	emit m_signals->groupMembersLoaded(m_descriptor, m_sendId, m_groupId, memberIds, m_fullPacket, m_dataBody);
}


//---------- LoadOfflineTask ----------

LoadOfflineTask::LoadOfflineTask(int descriptor, int uid, TaskSignals* taskSignals)
	: m_descriptor(descriptor)
	, m_uid(uid)
	, m_signals(taskSignals)
{
}

void LoadOfflineTask::run() {
	//离线消息拉取：一次 SELECT 全部 + 一批 DELETE，结果回传主线程逐条发包

	QSqlDatabase db = DbConnPool::getInstance().get();
	QSqlQuery query(db);

	// 1. 查询该用户全部离线消息（按 id 升序保证推送顺序）
	query.prepare("SELECT `id`, `content` FROM `tab_offline_msg` WHERE `recv_id` = ? ORDER BY `id` ASC");
	query.addBindValue(m_uid);
	query.exec();

	QList<QByteArray> contents;		//消息正文列表（存的 dataBody 原文）
	QList<int> ids;					//消息主键列表（用于删除）

	while (query.next() == true) {
		ids << query.value(0).toInt();
		contents << query.value(1).toByteArray();
	}

	// 2. 批量删除（池线程里删，不卡主线程）
	for (int msgId : ids) {
		QSqlQuery delQuery(db);
		delQuery.prepare("DELETE FROM `tab_offline_msg` WHERE `id` = ?");
		delQuery.addBindValue(msgId);
		delQuery.exec();
	}

	// 3. 结果回传主线程逐条发包
	if (contents.size() > 0) {
		qDebug() << QStringLiteral("[Offline] uid=%1 登录，已拉取 %2 条离线消息待推送").arg(m_uid).arg(contents.size());
	}
	emit m_signals->offlineLoaded(m_descriptor, m_uid, contents);
}


//---------- InsertOfflineTask ----------

InsertOfflineTask::InsertOfflineTask(int recvId, const QByteArray& content)
	: m_recvId(recvId)
	, m_content(content)
{
}

void InsertOfflineTask::run() {
	//离线消息暂存：fire-and-forget，塞进池就不管，无结果回传

	QSqlDatabase db = DbConnPool::getInstance().get();
	QSqlQuery query(db);

	query.prepare("INSERT INTO `tab_offline_msg` (`recv_id`, `content`) VALUES (?, ?)");
	query.addBindValue(m_recvId);
	query.addBindValue(m_content);

	if (query.exec() == false) {
		qDebug() << QStringLiteral("[Offline] uid=%1 离线消息入库失败：%2")
			.arg(m_recvId).arg(query.lastError().text());
	}
}
