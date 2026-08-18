#pragma once

#include <QObject>
#include <QMap>
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

//会话（追加式消息日志）
struct TalkSession {
	QList<MsgRecord> messages;
};


//会话仓库
//职责：接收网络消息 → 路由归档 → 广播通知；为窗口提供历史重放
//生命周期：随进程常驻（单例），退出登录时需 clear 防跨账号串数据
class TalkSessionStore : public QObject
{
	Q_OBJECT

public:
	static TalkSessionStore& getInstance();

	QList<MsgRecord> records(int talkId) const;		//获取会话全部历史记录（打开窗口时全量重放用）
	void appendSelfRecord(int talkId, const MsgRecord& record);		//追加一条记录到自己发的会话（发送路径调用，窗口已即时显示，仅静默入库供日后重放）
	void clear();		//清空所有会话（退出登录时调用，防止新账号看到旧账号的聊天记录）

private:
	TalkSessionStore();
	~TalkSessionStore();
	TalkSessionStore(const TalkSessionStore&) = delete;
	TalkSessionStore& operator=(const TalkSessionStore&) = delete;

signals:
	//信号
	void signalMessageStored(int talkId, int groupFlag, int sendId, int recvId, int msgType, const QString& msg);	//消息入仓广播：携带完整字段（窗口渲染需 sendId 定头像、msgType 做逆向转换）

private slots:
	//槽函数
	void onTcpMessage(int groupFlag, int sendId, int recvId, int msgType, const QString& msg);	// TcpClient 网络消息的唯一入口（路由判定 + 入库 + 广播）

private:
	//成员变量
	QMap<int, TalkSession> m_sessionMap;		// uid - 会话 的映射

};
