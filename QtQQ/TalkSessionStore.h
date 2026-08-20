#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QList>


//消息记录
struct MsgRecord {
	int groupFlag;		// 0私聊 1群聊
	int sendId;			// 发送者 employeeID
	int recvId;			// 接收者（私聊=我，群聊=群ID）
	int msgType;		// 0表情 1文本
	QString msg;		// wire 格式内容（已转义/已编码，可逆向还原显示）
	bool mine;			// true=我发的（渲染右侧气泡）
};


//会话仓库
//职责：接收网络消息 → 路由归档 → 广播通知
//生命周期：随进程常驻（单例），退出登录时需 close 防跨账号串数据
class TalkSessionStore : public QObject
{
	Q_OBJECT

public:
	static TalkSessionStore& getInstance();

	void open(int empID);		//登录成功后调用：打开/创建该账号的本地消息库 msg_<empID>.db
	QList<MsgRecord> records(int uid);		//获取会话记录（创建窗口时调用，用于加载历史消息记录）
	void appendSelfRecord(int uid, const MsgRecord& record);		//将自己发的消息追加至会话仓库
	void close();		//关闭本地消息库（退出登录时调用；数据文件保留，下次登录历史还在）

private:
	TalkSessionStore();
	~TalkSessionStore();
	TalkSessionStore(const TalkSessionStore&) = delete;
	TalkSessionStore& operator=(const TalkSessionStore&) = delete;

signals:
	//信号
	void signalMessageStored(int uid, int groupFlag, int sendId, int recvId, int msgType, const QString& msg);	//消息入仓广播：携带完整字段（窗口渲染需 sendId 定头像、msgType 做逆向转换）

private slots:
	//槽函数
	void onTcpMessage(int groupFlag, int sendId, int recvId, int msgType, const QString& msg);	// TcpClient 网络消息的唯一入口（路由判定 + 入库 + 广播）

private:
	//成员变量
	QSqlDatabase m_db;		//本地仓库
	bool m_isOpen;			//库是否打开

};
