#pragma once

#include "ui_TalkWindow.h"
#include "TalkWindowShell.h"
#include "TalkSessionStore.h"		

#include <QWidget>


class TalkWindow : public QWidget
{
	Q_OBJECT

public:
	TalkWindow(QWidget *parent, const int& uid);
	~TalkWindow();

public:
	void addEmotionImage(int emotionNum);
	void setWindowName(const QString& name);

private:
	void initControl();
	void initGroupStatus();
	int getCompDepID();
	void initGroupTalk();		//初始化群聊
	void initPTOPTalk();		//初始化单聊
	void addPeopleItem(QTreeWidgetItem* pRootGroupItem, int empID);		//联系人树 添加联系人子项

	void renderRecord(const MsgRecord& record);		//按记录渲染气泡（增量显示与历史重放共用管线）
	void replayHistory();		//重放会话仓库的全部历史（页面加载完成后调用）

private slots:
	//槽函数
	void onMsgSend(const QString& msg, int msgType, const QString file);
	void onStoredMessage(int talkId, int groupFlag, int sendId, int recvId, int msgType, const QString& msg);		//仓库广播：本窗口会话的增量渲染
	void onPageLoadFinished(bool ok);		//页面加载完成：开始历史重放

private slots:
	void onSendBtnClicked();
	void onItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
	//成员变量
	int m_talkId;			//窗口uid
	bool m_isGroupTalk;		//是否为群聊

private:
	Ui::TalkWindow ui;
};

