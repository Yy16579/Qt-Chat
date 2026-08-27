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

	//群成员查询完成：原包 fullPacket / dataBody 随任务往返（查询期间消息"在途"，回主线程才继续转发）
	void groupMembersLoaded(int descriptor, int sendId, int groupId,
		const QList<int>& memberIds,
		const QByteArray& fullPacket, const QByteArray& dataBody);

	//离线消息拉取完成：contents = 该用户全部离线消息的 dataBody 原文列表
	void offlineLoaded(int descriptor, int uid, const QList<QByteArray>& contents);
};


//====================================================== 池任务（QRunnable）========================================================
// 公共约定：
//   1. 任务类不继承 QObject（QRunnable 无信号槽能力），结果经 TaskSignals 信号回传主线程
//   2. 构造参数全部值拷贝存成员（跨线程数据传递，绝不存引用）
//   3. run() 第一行拿本线程专属连接，QSqlQuery 必须显式传 db（本项目无默认连接）
//   4. 任务由 QThreadPool 接管，run() 结束后自动 delete（autoDelete 默认 true）
//   5. 任务只碰数据库和纯数据，绝不碰 socket / 路由表（线程亲和性铁律）

//---------- DbCheckTask：启动自检任务 ----------
// 服务端启动时投一个到池：试连 MySQL，结果回传主线程打印（替代原 initDatabase 的 fail-fast 报错）
class DbCheckTask : public QRunnable
{
public:
	explicit DbCheckTask(TaskSignals* taskSignals);

	void run() override;

private:
	TaskSignals* m_signals;		//结果回传器（主线程对象，只 emit 不 delete）
};


//---------- LoginTask：登录验证任务 ----------
// 双方式账密验证（employeeID / account 字段）+ 验证成功时打包通讯录快照
// 原 handleLoginRequest 的 DB 段 + buildContactSnapshot 平移至此，只产出数据，不含发包逻辑
class LoginTask : public QRunnable
{
public:
	LoginTask(int descriptor, const QString& account, const QString& password, TaskSignals* taskSignals);

	void run() override;

private:
	QByteArray buildContactSnapshot();		//打包通讯录快照 JSON（两张表全量 → 紧凑 JSON）

private:
	int m_descriptor;			//来源连接的 fd（结果回传时的关联键）
	QString m_account;			//账号（employeeID 或 account 字段）
	QString m_password;			//密码
	TaskSignals* m_signals;		//结果回传器
};


//---------- GroupMembersTask：群成员查询任务 ----------
// 查询群成员列表（公司群 = 全部在职员工 / 普通群 = 该部门在职员工）
// 原 handleMessage 群聊分支的 DB 段平移；原包随任务往返，查完回主线程做在线转发/离线入库
class GroupMembersTask : public QRunnable
{
public:
	GroupMembersTask(int descriptor, int sendId, int groupId,
		const QByteArray& fullPacket, const QByteArray& dataBody, TaskSignals* taskSignals);

	void run() override;

private:
	int m_descriptor;			//来源连接的 fd
	int m_sendId;				//发送者 uid（主线程分发时跳过本人用）
	int m_groupId;				//群号（departmentID）
	QByteArray m_fullPacket;	//完整原始包（含外层包头）——查询期间"在途"，带回主线程转发用
	QByteArray m_dataBody;		//数据体——离线成员入库用
	TaskSignals* m_signals;		//结果回传器
};


//---------- LoadOfflineTask：离线消息拉取任务 ----------
// 一次 SELECT 全部 + 一批 DELETE，结果回传主线程逐条发包
// 原 pushOfflineMessages 平移；行为变化（方案已确认）：由"主线程发一条删一条"改为"任务里先删干净 → 才 emit 回主线程发"
class LoadOfflineTask : public QRunnable
{
public:
	LoadOfflineTask(int descriptor, int uid, TaskSignals* taskSignals);

	void run() override;

private:
	int m_descriptor;			//来源连接的 fd
	int m_uid;					//要拉取离线消息的用户 uid
	TaskSignals* m_signals;		//结果回传器
};


//---------- InsertOfflineTask：离线消息暂存任务 ----------
// fire-and-forget：塞进池就不管，无结果回传
// 原 handleMessage 离线分支的 INSERT 平移（私聊对方离线 / 群聊离线成员暂存共用）
class InsertOfflineTask : public QRunnable
{
public:
	InsertOfflineTask(int recvId, const QByteArray& content);

	void run() override;

private:
	int m_recvId;				//离线接收者 uid
	QByteArray m_content;		//离线消息的 dataBody 原文
};
