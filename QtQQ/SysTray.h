#pragma once

#include <QSystemTrayIcon>
#include <QWidget>


class CCMainWindow;		//前置声明：头文件仅使用指针，无需完整类型定义

class SysTray  : public QSystemTrayIcon
{
	Q_OBJECT

public:
	SysTray(CCMainWindow* parent);
	~SysTray();

private:
	void initSystemTray();
	void addSysTrayMenu();

public slots:
	void onIconActivated(QSystemTrayIcon::ActivationReason reason);

private:
	CCMainWindow* m_parent;		//托盘唯一使用者是主窗口，直接持有具体类型，避免向下转型
};

