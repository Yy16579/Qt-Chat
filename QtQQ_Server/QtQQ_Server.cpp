#include "QtQQ_Server.h"

#include <QSettings>
#include <QCoreApplication>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>


QtQQ_Server::QtQQ_Server(QWidget *parent)
	: QDialog(parent)
	, m_tcpServer(nullptr)
{
	ui.setupUi(this);

	this->initTcpServer();		//初始化监听 socket 
	this->initDatabase();		//连接 Mysql 数据库
}

QtQQ_Server::~QtQQ_Server()
{}

void QtQQ_Server::initTcpServer() {
	//从配置文件 config.ini 读取端口号（默认 6666）
	QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
	QSettings settings(configPath, QSettings::IniFormat);
	int port = settings.value("Tcp/port", 6666).toInt();

	//创建监听 socket 并开始监听
	//对应 socket API  --------  socket();	  
	//对应 socket API  --------  bind();	  
	this->m_tcpServer = new TcpServer(port);
	//对应 socket API  --------  listen();	
	if (this->m_tcpServer->startListen() == false) {
		qDebug() << QStringLiteral("服务端启动失败，请检查端口是否被占用") << '\n';
	}
}

void QtQQ_Server::initDatabase() {
	//从 config.ini 配置文件的 [Database] 节读取连接参数
	QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
	QSettings settings(configPath, QSettings::IniFormat);

	//向数据库发起连接
	//对应  C API  --------  mysql_init();
	//			   --------  mysql_real_connect();
	QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");
	db.setHostName(settings.value("Database/host").toString());
	db.setPort(settings.value("Database/port").toInt());
	db.setUserName(settings.value("Database/userName").toString());
	db.setPassword(settings.value("Database/password").toString());
	db.setDatabaseName(settings.value("Database/databaseName").toString());

	if (db.open()) {
		qDebug() << QStringLiteral("数据库连接成功：%1:%2/%3")
					.arg(db.hostName()).arg(db.port()).arg(db.databaseName());
	}
	else {
		qDebug() << QStringLiteral("数据库连接失败：%1").arg(db.lastError().text());
	}
}
