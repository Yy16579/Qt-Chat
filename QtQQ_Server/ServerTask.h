#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QMutex>
#include <QHash>
#include <QRunnable>
#include <QList>
#include <QtGlobal>


//====================================================== DbConnPool（连接管家）=====================================================
// 池线程专用的 MySQL 连接管理器：每线程惰性创建一个命名连接，之后复用
// QSqlDatabase 铁律：连接在哪个线程创建，就只能在哪个线程使用（一个线程一个连接，连接不能跨线程使用）
// （QThreadPool 的线程没有事件循环且会先后跑多个任务，故按"线程"而非"任务"管理连接）
class DbConnPool
{
public:
	static DbConnPool& getInstance();

	//获取当前线程专属的数据库连接（首次调用时创建，之后直接复用）
	//返回 QSqlDatabase 句柄副本（内部共享，轻量），调用方须用它显式构造 QSqlQuery
	QSqlDatabase get();

private:
	//创建一个命名数据库连接
	bool createConnection(const QString& connName);

private:
	DbConnPool() = default;
	~DbConnPool() = default;
	DbConnPool(const DbConnPool&) = delete;
	DbConnPool& operator=(const DbConnPool&) = delete;

private:
	//成员变量
	QMutex m_mutex;				//保证 m_connNames  hash 容器的互斥访问
	QHash<Qt::HANDLE, QString> m_connNames;		//线程 tid → 数据库连接名 的映射
};


//====================================================== TaskSignals（结果回传器）===================================================
// 跨线程结果回传的"管子"：主线程创建（挂 TcpServer 为父），池线程里 emit 信号
// Qt 跨线程信号槽机制：信号从哪个线程 emit 无所谓，槽函数一定在接收者所属线程（主线程）执行
// emit 瞬间参数被拷入事件队列，Qt 自动唤醒主线程事件循环（等价于 muduo 的 runInLoop + eventfd）
class TaskSignals : public QObject
{
	Q_OBJECT

public:
	explicit TaskSignals(QObject* parent = nullptr);

signals:
	//启动自检：服务端启动时试连一次 MySQL，替代原 initDatabase 的 fail-fast 报错
	void dbChecked(bool ok, const QString& error);

	//登录验证完成：ok = 账密是否验证通过；snapshot = 通讯录快照 JSON（验证失败为空）
	void loginVerified(int descriptor, bool ok, int uid, const QByteArray& snapshot);

	//私聊消息入库完成（StoreMsgTask 的结果回执）
	//ok = INSERT 成败；rowId = 本行自增 id（为 0 = INSERT IGNORE 幂等命中 → 重传的重复包，槽里跳过高水位更新和敲门）
	//主线程据 recvId + rowId 完成更新 m_userMaxId + 敲门收件人，零 DB 查询
	void msgStored(int descriptor, const QString& msgId, bool ok, int recvId, quint64 rowId);

	//群消息批量入库完成（GroupMembersTask 的结果回执）
	//memberIds = 实际入库的成员列表（已跳过发送者本人）；rowId = 本批各行 id 的最大值（0 = 整批幂等命中）
	void groupMsgStored(int descriptor, const QString& msgId, bool ok, const QList<int>& memberIds, quint64 rowId);

	// TODO(你来实现)：pullLoaded —— PullTask（分页拉取查询）的结果回执
	//    参数建议：int descriptor、quint64 newCursor（本页最大 id）、QList<QByteArray> contents（消息载荷列表）
	//    语义：主线程封 PullResponse 包（[新账本10B][条数1B]+N×[载荷]）发给客户端
};


//====================================================== 池任务（QRunnable）========================================================
// 公共约定：
//   1. 任务类不继承 QObject（QRunnable 无信号槽能力），结果经 TaskSignals 信号回传主线程
//   2. 构造参数全部值拷贝存成员（跨线程数据传递，绝不存引用）
//   3. run() 第一行拿本线程专属连接，QSqlQuery 必须显式传 db（本项目无默认连接）
//   4. 任务由 QThreadPool 接管，run() 结束后自动 delete（autoDelete 默认 true）
//   5. 任务只碰数据库和纯数据，绝不碰 socket / 路由表（线程亲和性铁律）

//---------- DbCheckTask：启动自检任务 ----------
// 服务端启动时投一个到池：试连 MySQL，结果回传主线程打印
class DbCheckTask : public QRunnable
{
public:
	explicit DbCheckTask(TaskSignals* taskSignals);

	void run() override;

private:
	TaskSignals* m_signals;		
};


//---------- LoginTask：登录验证任务 ----------
// 双方式账密验证（employeeID / account 字段）+ 验证成功时打包通讯录快照
class LoginTask : public QRunnable
{
public:
	LoginTask(int descriptor, const QString& account, const QString& password, TaskSignals* taskSignals);

	void run() override;

private:
	QByteArray buildContactSnapshot();		//打包通讯录快照 JSON（两张表全量 → 紧凑 JSON）

private:
	int m_descriptor;			//来源连接的 fd
	QString m_account;			//账号（employeeID 或 account 字段）
	QString m_password;			//密码
	TaskSignals* m_signals;		//结果回传器
};


//---------- StoreMsgTask：私聊消息入库任务 ----------
// INSERT IGNORE（幂等：msg_id 唯一键冲突时静默跳过，发送端重传不会造成重复入库）
// 注意：content 存的是"消息载荷"（[群标志|发送者|接收者|类型|内容]，不含 msgId 头），Pull 时原样下发
class StoreMsgTask : public QRunnable {
public:
	StoreMsgTask(int descriptor, QString msgId, int recvId, QByteArray content, TaskSignals* taskSignals);

	void run() override;

private:
	int m_descriptor;			//来源连接的 fd
	QString m_msgId;			//消息幂等键（tab_msg.msg_id 唯一索引，重传挡板）
	int m_recvId;				//收件人 uid（tab_msg.recv_id）
	QByteArray m_content;		//消息载荷原文
	TaskSignals* m_signals;		//结果回传器
};


//---------- GroupMembersTask：群消息分发入库任务 ----------
// 查群成员（公司群 = 全部在职 / 普通群 = 该部门在职）→ 按成员批量 INSERT（一人一行，跳过发送者本人）
class GroupMembersTask : public QRunnable {
public:
	GroupMembersTask(int descriptor, QString msgId, int sendId, int groupId, QByteArray content, TaskSignals* taskSignals);

	void run() override;

private:
	int m_descriptor;			//来源连接的 fd
	QString m_msgId;			//消息幂等键（本批各行共用，撞唯一键 = 整批幂等命中）
	int m_sendId;				//发送者 uid（入库时跳过本人——自己的消息客户端已本地渲染）
	int m_groupId;				//群号（departmentID）
	QByteArray m_content;		//消息载荷原文（一人一份，内容相同）
	TaskSignals* m_signals;		//结果回传器
};



//---------- PullTask：分页拉取任务 ----------
// 职责：SELECT id, content FROM tab_msg WHERE recv_id = ? AND id > ?(账本) ORDER BY id ASC LIMIT 20
//       → emit m_signals->pullLoaded(descriptor, 本页最大id, 载荷列表)
// 构造参数建议：int descriptor、int uid（= socket->getUid()）、quint64 cursor（客户端账本）、TaskSignals*
// 注意：空结果也要 emit（newCursor 回传原账本，客户端据此知道"没有更多了"停止续拉）
// ----------------------------------------------------------------------------------------------------------------------
