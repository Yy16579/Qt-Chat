#pragma once

#include "TalkWindowShell.h"

#include <QObject>


class WindowManager  : public QObject
{
	Q_OBJECT

public:
	static WindowManager& getInstance();

	//操作 m_windowMap 映射表
	QWidget* findWindowName(const int& qsWindowName);
	void deleteWindowName(const int& qsWindowName);
	void addWindowName(const int& qsWindowName, QWidget* qWidget);

	void addNewTalkWindow(const int& uid);
	TalkWindowShell* getTalkWindowShell() const;

private:
	WindowManager();
	~WindowManager();
	WindowManager(const WindowManager&) = delete;
	WindowManager& operator=(const WindowManager&) = delete;

private slots:
	//槽函数
	void onStoredMessage(int uid, int groupFlag, int sendId, int recvId, int msgType, const QString& msg);	//仓库广播入口

private:
	//成员变量
	QMap<int, QWidget*> m_windowMap;		// uid - TalkWindow 的映射
	TalkWindowShell* m_talkwindowshell;

public:
	int m_empID;
};

