#include "TcpServer.h"

#include <QCoreApplication>
#include <QSettings>
#include <QDebug>


int main(int argc, char* argv[])
{
	//纯控制台服务端：无 GUI，qDebug 直接打印到终端，无需重定向
	QCoreApplication app(argc, argv);

	//从配置文件 config.ini 读取端口号（默认 6666）
	QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
	QSettings settings(configPath, QSettings::IniFormat);
	int port = settings.value("Tcp/port", 6666).toInt();

	//创建监听 socket 并开始监听
	//对应 socket API  --------  socket();
	//对应 socket API  --------  bind();
	TcpServer server(port);
	//对应 socket API  --------  listen();
	if (server.startListen() == false) {
		qDebug() << QStringLiteral("服务端启动失败，请检查端口是否被占用");
		return -1;
	}

	return app.exec();
}
