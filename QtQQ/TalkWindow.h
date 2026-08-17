#pragma once

#include "ui_TalkWindow.h"
#include "TalkWindowShell.h"

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

private slots:
	//槽函数
	void onMsgSend(const QString& msg, int msgType, const QString file);
	void onMsgReceived(int groupFlag, int sendId, int recvId, int msgType, const QString& msg);

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

