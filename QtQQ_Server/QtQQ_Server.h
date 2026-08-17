#pragma once

#include "ui_QtQQ_Server.h"
#include "TcpServer.h"

#include <QDialog>


class QtQQ_Server : public QDialog
{
	Q_OBJECT

public:
	QtQQ_Server(QWidget *parent = nullptr);
	~QtQQ_Server();

private:
	void initDatabase();			//初始化数据库连接
	void initTcpServer();			//初始化 Tcp 监听 socket

private:
	//成员变量
	TcpServer* m_tcpServer;

private:
	Ui::QtQQ_ServerClass ui;
};

