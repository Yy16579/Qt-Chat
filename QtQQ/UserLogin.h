#pragma once

#include "basicwindow.h"
#include "ui_UserLogin.h"



class UserLogin : public BasicWindow
{
	Q_OBJECT

public:
	UserLogin(QWidget *parent = nullptr);
	~UserLogin();

private:
	void initControl();
	void initTcpConnect();		//初始化网络连接

	bool connectMySql();		//初始化数据库连接
	bool verifyAccountCode(int& empID);		//验证账号密码是否正确

private slots:
	//槽函数
	void onLoginBtnClicked();

private:
	Ui::UserLogin ui;
};

