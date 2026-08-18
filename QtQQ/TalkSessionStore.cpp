#include "TalkSessionStore.h"
#include "TcpClient.h"
#include "WindowManager.h"


TalkSessionStore::TalkSessionStore()
	: QObject(nullptr)
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

QList<MsgRecord> TalkSessionStore::records(int uid) const {
	return this->m_sessionMap.value(uid).messages;
}

void TalkSessionStore::appendSelfRecord(int uid, const MsgRecord& record) {
	//自己发的消息入库（发送路径已即时显示右侧气泡，此处静默入库，不广播）
	this->m_sessionMap[uid].messages.append(record);
}

void TalkSessionStore::clear() {
	//退出登录清空仓库，避免跨账号数据泄漏
	this->m_sessionMap.clear();
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
	MsgRecord record;
	record.groupFlag = groupFlag;
	record.sendId = sendId;
	record.recvId = recvId;
	record.msgType = msgType;
	record.msg = msg;
	record.mine = false;
	this->m_sessionMap[uid].messages.append(record);

	// 3. 广播（已开的窗口增量渲染；WindowManager 据此决定是否自动开窗）
	emit this->signalMessageStored(uid, groupFlag, sendId, recvId, msgType, msg);
}

