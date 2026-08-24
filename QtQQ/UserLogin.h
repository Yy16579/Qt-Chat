#pragma once

#include "ui_UserLogin.h"
#include "basicwindow.h"

#include <QTimer>


class UserLogin : public BasicWindow
{
	Q_OBJECT

public:
	UserLogin(QWidget *parent = nullptr);
	~UserLogin();

private:
	void initControl();
	void initTcpConnect();		//初始化网络连接

private slots:
	//槽函数
	void onLoginBtnClicked();
	void onLoginResponse(bool result, int empID);	//登录响应处理
	void onLoginTimeout();		//登录请求超时处理

private:
	//成员变量
	QTimer m_loginTimer;	//登录超时定时器（发出请求后启动，收到响应或超时后停止）

private:
	Ui::UserLogin ui;
};

