#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QMutex>
#include <QHash>
#include <QRunnable>
#include <QList>
#include <QByteArray>
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
	//maxSeqs = 该用户各会话 DB 权威高水位（convId → MAX(seq)，验证失败为空表）——
	//          用于登录时重建 m_convMaxSeq（服务端重启丢内存高水位后，靠每个用户登录增量自愈）
	//ledger = 该用户账本镜像（tab_ledger 持久化的接收进度，convId → 游标，验证失败为空表）——
	//          随登录响应下发，客户端镜像后拉取"上次进度之后"的消息（离线未读可见、历史不重拉）
	void loginVerified(int descriptor, bool ok, int uid, const QByteArray& snapshot, const QHash<int, quint64>& maxSeqs, const QHash<int, quint64>& ledger);

	//私聊消息入库完成：
	//ok = INSERT 成败；convId = 会话键（私聊=发送者 uid）；seq = 会话内序号（服务端号池分配）
	void msgStored(int descriptor, const QString& msgId, bool ok, int recvId, int convId, quint64 seq);

	//群消息批量入库完成：
	//memberIds = 实际入库的成员列表（含发送者本人）；convId = 群号；seq = 整批共用的会话内序号（服务端群号池分配）
	void groupMsgStored(int descriptor, const QString& msgId, bool ok, const QList<int>& memberIds, int convId, quint64 seq);

	//PullTask 拉取结果回执：dataBody = 已封好的完整 JSON 数据体（主线程纯转发不加工）
	//格式 = {"count":N,"msgs":[{"convId":"..","seq":"..","msgId":"..","payload":"base64"}...]}
	void pullLoaded(int descriptor, const QByteArray& dataBody);

	//SeqPoolInitTask 号池重建结果：服务端启动时从 DB 恢复双号池断点（防重启从 1 重取号重演撞号）
	void seqPoolLoaded(const QHash<qint64, quint64>& privPool, const QHash<int, quint64>& groupPool);
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
// 双方式账密验证（employeeID / account 字段）+ 验证成功时打包通讯录快照 + 服务端同步账本（重启自愈）
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
// content 存的是"消息载荷"（[群标志|发送者|接收者|类型|内容]，不含 msgId 头），Pull 时原样下发
class StoreMsgTask : public QRunnable {
public:
	StoreMsgTask(int descriptor, QString msgId, int recvId, int convId, quint64 seq, QByteArray content, TaskSignals* taskSignals);

	void run() override;

private:
	int m_descriptor;			//来源连接的 fd
	QString m_msgId;			//消息幂等键（uk_recv_msg 唯一索引的组成部分，重传挡板）
	int m_recvId;				//收件人 uid（tab_msg.recv_id）
	int m_convId;				//会话键（私聊 = 发送者 uid；客户端账本/游标按此会话记账）
	quint64 m_seq;				//会话内序号（服务端私聊号池分配——主线程取号后传入，池线程只管写库）
	QByteArray m_content;		//消息载荷原文
	TaskSignals* m_signals;		//结果回传器
};


//---------- GroupMembersTask：群消息分发入库任务 ----------
// 查群成员（公司群 = 全部在职 / 普通群 = 该部门在职）→ 按成员批量 INSERT（一人一行，含发送者本人）
// 含本人：自己视角账本推进全靠这条流水（客户端 m_seenMsgId 去重防重复渲染）
// 会话键 conv_id = m_groupId（群号，复用现有成员不单独存）；seq 整批共用（服务端群号池分配，群是同一序号空间）
class GroupMembersTask : public QRunnable {
public:
	GroupMembersTask(int descriptor, QString msgId, int sendId, int groupId, quint64 seq, QByteArray content, TaskSignals* taskSignals);

	void run() override;

private:
	int m_descriptor;			//来源连接的 fd
	QString m_msgId;			//消息幂等键（本批各行共用，撞唯一键 = 幂等命中）
	int m_sendId;				//发送者 uid（仅日志用；扇出含本人——自己也要一行推进账本）
	int m_groupId;				//群号（departmentID，兼作会话键 conv_id）
	quint64 m_seq;				//会话内序号（服务端群号池分配，整批各行共用）
	QByteArray m_content;		//消息载荷原文（一人一份，内容相同）
	TaskSignals* m_signals;		//结果回传器
};


//---------- PullTask：增量拉取任务 ----------
// 职责：逐会话增量拉取（seq 方案，N 会话 N 条 SQL）→ 池线程直接封装完整 JSON 数据体回传
// {"count":N,"msgs":[{"convId":"..","seq":"..","msgId":"..","payload":"base64"}...]}，主线程纯转发不加工
class PullTask : public QRunnable {
public:
	PullTask(int descriptor, int uid, const QHash<int, quint64>& cursors, TaskSignals* taskSignals);

	void run() override;

private:
	int m_descriptor;					//来源连接的 fd
	int m_uid;							//发起拉取的用户 uid（= socket->getUid()）
	QHash<int, quint64> m_cursors;		//接收端账本（会话ID → 消息接收 seq ）
	TaskSignals* m_signals;				//结果回传器
};


//---------- SeqPoolInitTask：取号池初始化任务 ----------
// 私聊 / 群聊 取号池初始化（重启自愈）
// 查 DB 每 (收者,会话) 的 MAX(seq) 恢复双号池断点
class SeqPoolInitTask : public QRunnable {
public:
	explicit SeqPoolInitTask(TaskSignals* taskSignals);

	void run() override;

private:
	TaskSignals* m_signals;		//结果回传器
};


//---------- LedgerUpsertTask：账本进度持久化任务 ----------
// 客户端接收进度上报（心跳 / PullRequest / 渲染即时心跳三入口投递）→ tab_ledger upsert
// GREATEST 取大：迟到心跳防回退（被踢设备的旧游标不拉低新设备进度）
// 无结果回传：DB 写失败仅丢一次上报，下次心跳覆盖（尽力而为，无自愈必要）
class LedgerUpsertTask : public QRunnable {
public:
	LedgerUpsertTask(int uid, const QHash<int, quint64>& ledger);

	void run() override;

private:
	int m_uid;							//上报用户 uid
	QHash<int, quint64> m_ledger;		//客户端账本快照（会话ID → 游标）
};

//==================================================================================================================================


