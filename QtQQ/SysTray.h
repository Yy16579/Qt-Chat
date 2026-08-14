#pragma once

#include "basicwindow.h"

#include <QSystemTrayIcon>
#include <QWidget>


class SysTray  : public QSystemTrayIcon
{
	Q_OBJECT

public:
	SysTray(BasicWindow* parent);
	~SysTray();

private:
	void initSystemTray();
	void addSysTrayMenu();

public slots:
	void onIconActivated(QSystemTrayIcon::ActivationReason reason);

private:
	BasicWindow* m_parent;
};

