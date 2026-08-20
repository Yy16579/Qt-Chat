#include "UserLogin.h"
#include "CommonUtils.h"
#include "CCMainWindow.h"
#include "TcpClient.h"
#include "TalkSessionStore.h"

#include <QMessageBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QSettings>


UserLogin::UserLogin(QWidget *parent)
	: BasicWindow(parent)
{
	ui.setupUi(this);
	this->setAttribute(Qt::WA_DeleteOnClose);

	//初始化标题栏
	this->initTitleBar();
	this->setTitleBarTitle("", ":/Resources/MainWindow/qqlogoclassic.png");

	this->loadStyleSheet("UserLogin");

	this->initControl();
	this->initTcpConnect();		//向服务端发起连接
}

UserLogin::~UserLogin()
{
	// UserLogin 析构时断开信号槽连接
	disconnect(&TcpClient::getInstance(), &TcpClient::signalErrorOccurred, this, nullptr);
}

void UserLogin::initControl() {
	//连接数据库
	if (this->connectMySql() == false) {
		QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("连接数据库失败！"));
		QApplication::quit();
		return;
	}

	//设置登录界面用户头像
	QLabel* headlabel = new QLabel(this);
	headlabel->setFixedSize(68, 68);
	QPixmap pix(":/Resources/MainWindow/app/logo.ico");
	QPixmap mask(":/Resources/MainWindow/head_mask.png");
	headlabel->setPixmap(this->getRoundImage(pix, mask, headlabel->size()));
	headlabel->move(this->width()/2-34, ui.titleWidget->height()-34);
	
	//连接信号槽
	connect(ui.loginBtn, &QPushButton::clicked, this, &UserLogin::onLoginBtnClicked);

	//初始化登录超时定时器（单次触发，5秒无响应视为服务端异常）
	this->m_loginTimer.setSingleShot(true);
	this->m_loginTimer.setInterval(5000);
	connect(&this->m_loginTimer, &QTimer::timeout, this, &UserLogin::onLoginTimeout);
}

void UserLogin::initTcpConnect() {
	//监听 TcpClient 的错误信号，弹窗提示用户
	connect(&TcpClient::getInstance(), &TcpClient::signalErrorOccurred,
		this, [this](const QString& errorMsg) {
			QMessageBox::warning(this, QStringLiteral("提示"),
				QStringLiteral("错误：") + errorMsg);
		});

	//监听 TcpClient 的登录响应信号
	connect(&TcpClient::getInstance(), &TcpClient::signalLoginResponse, this, &UserLogin::onLoginResponse);

	//通过 TcpClient 单例向服务端发起连接
	TcpClient::getInstance().connectToServer();
}

bool UserLogin::connectMySql() {	
	//从 config.ini 配置文件的 [Database] 节读取连接参数
	QSettings settings(CommonUtils::getConfigPath(), QSettings::IniFormat);


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
		return true;
	}
	else {
		qWarning() << "MySQL connect failed:" << db.lastError().text();
		return false;
	}
}


//槽函数
void UserLogin::onLoginBtnClicked() {
	// 提取输入框的账号密码
	QString account = ui.editUserAccount->text();
	QString password = ui.editPassword->text();

	// 输入判空：避免无效请求发往服务端
	if (account.isEmpty() || password.isEmpty()) {
		QMessageBox::information(this, QStringLiteral("提示"),
			QStringLiteral("账号或密码不能为空！"));
		return;
	}

	// 向服务端发起登录请求，并启动超时定时器
	TcpClient::getInstance().sendLoginRequest(account, password);
	this->m_loginTimer.start();
}

void UserLogin::onLoginResponse(bool result, int empID) {
	//收到响应，无论成败先停止超时定时器
	this->m_loginTimer.stop();

	//查看登录响应结果
	if (result == true) {
		//打开该账号的本地消息库（必须在 new CCMainWindow 之前：
		//同线程直连 + 串行切包保证——离线推送消息处理前，库已就位、m_empID 已设置）
		TalkSessionStore::getInstance().open(empID);

		//账号密码正确，传入用户 employeeID ，进入主窗口
		this->close();
		CCMainWindow* mainwindow = new CCMainWindow(empID);
		mainwindow->show();
	}
	else {
		//账号密码错误，弹出提示框
		QMessageBox::information(this, QStringLiteral("提示"),
			QStringLiteral("您输入的账号密码有误，请重新输入！"));
	}

	qDebug() << QStringLiteral("[LoginResponse] 登录%1，uid=%2")
		.arg(result ? QStringLiteral("成功") : QStringLiteral("失败"))
		.arg(result ? empID : -1);
}

void UserLogin::onLoginTimeout() {
	//服务端5秒内未响应（未启动/网络中断/处理异常），提示用户
	QMessageBox::warning(this, QStringLiteral("提示"),
		QStringLiteral("登录超时，请检查服务端是否已启动！"));
}

