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
	//启动自检：服务端启动时试连一次 MySQL
	void dbChecked(bool ok, const QString& error);

	//登录验证完成：ok = 账密是否验证通过；snapshot = 通讯录快照 JSON（验证失败为空）
	void loginVerified(int descriptor, bool ok, int uid, const QByteArray& snapshot);

	//私聊消息入库完成：
	//ok = INSERT 成败；convId = 会话键（私聊=发送者 uid）；seq = 会话内序号（客户端取号机分配，服务端只透传）
	void msgStored(int descriptor, const QString& msgId, bool ok, int recvId, int convId, quint64 seq);

	//群消息批量入库完成：
	//memberIds = 实际入库的成员列表（已跳过发送者本人）；convId = 群号；seq = 整批共用的会话内序号
	void groupMsgStored(int descriptor, const QString& msgId, bool ok, const QList<int>& memberIds, int convId, quint64 seq);

	// TODO(你来实现)：pullLoaded —— PullTask（分页拉取查询）的结果回执
	//    建议配套：struct PullMsg { int convId; quint64 seq; QString msgId; QByteArray content; } + Q_DECLARE_METATYPE
	//    信号签名：void pullLoaded(int descriptor, const QList<PullMsg>& messages);
	//    语义：主线程封 PullResponse 包（[条数1B]+N×[convId5B|seq10B|msgId13B|载荷]）发给客户端
	//    ★ 跨线程信号传 QList 自定义类型：需在 TcpServer 构造里 qRegisterMetaType<QList<PullMsg>>()（否则队列连接丢参数）
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
// INSERT IGNORE（ uk_recv_msg ( recv_id, msg_id ) 唯一键冲突时静默跳过，发送端重传不会造成重复入库）
// content 存的是"消息载荷"（[群标志|发送者|接收者|类型|内容]，不含 msgId/seq 头），Pull 时原样下发
class StoreMsgTask : public QRunnable {
public:
	StoreMsgTask(int descriptor, QString msgId, int recvId, int convId, quint64 seq, QByteArray content, TaskSignals* taskSignals);

	void run() override;

private:
	int m_descriptor;			//来源连接的 fd
	QString m_msgId;			//消息幂等键（uk_recv_msg 唯一索引的组成部分，重传挡板）
	int m_recvId;				//收件人 uid（tab_msg.recv_id）
	int m_convId;				//会话键（私聊 = 发送者 uid；客户端账本/游标按此会话记账）
	quint64 m_seq;				//会话内序号（客户端取号机分配，服务端只透传不重分配）
	QByteArray m_content;		//消息载荷原文
	TaskSignals* m_signals;		//结果回传器
};


//---------- GroupMembersTask：群消息分发入库任务 ----------
// 查群成员（公司群 = 全部在职 / 普通群 = 该部门在职）→ 按成员批量 INSERT（一人一行，跳过发送者本人）
// 会话键 conv_id = m_groupId（群号，复用现有成员不单独存）；seq 整批共用（群是同一序号空间）
class GroupMembersTask : public QRunnable {
public:
	GroupMembersTask(int descriptor, QString msgId, int sendId, int groupId, quint64 seq, QByteArray content, TaskSignals* taskSignals);

	void run() override;

private:
	int m_descriptor;			//来源连接的 fd
	QString m_msgId;			//消息幂等键（本批各行共用，撞唯一键 = 幂等命中）
	int m_sendId;				//发送者 uid（入库时跳过本人——自己的消息客户端已本地渲染）
	int m_groupId;				//群号（departmentID，兼作会话键 conv_id）
	quint64 m_seq;				//会话内序号（客户端取号机分配，整批各行共用）
	QByteArray m_content;		//消息载荷原文（一人一份，内容相同）
	TaskSignals* m_signals;		//结果回传器
};



//---------- PullTask：分页拉取任务（TODO 你来实现，后续完善） ----------
// 职责：逐会话分页拉取（seq 方案，N 会话 N 条 SQL）
// 构造参数建议：int descriptor、int uid（= socket->getUid()）、QHash<int, quint64> cursors（客户端整张游标表）、TaskSignals*
// run()：遍历 cursors，逐会话执行
//   SELECT `msg_id`, `seq`, `content` FROM `tab_msg`
//     WHERE `recv_id` = ? AND `conv_id` = ? AND `seq` > ? ORDER BY `seq` ASC LIMIT 20
//   → 汇总 QList<PullMsg>（配套 struct 见 TaskSignals 的 pullLoaded TODO）→ emit m_signals->pullLoaded(descriptor, messages)
// 注意：空结果也要 emit（客户端据"条数 0"知道没有更多了，停止续拉）
// ----------------------------------------------------------------------------------------------------------------------
