#include "UserLogin.h"
#include "CommonUtils.h"
#include "CCMainWindow.h"
#include "TcpClient.h"

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
	
	//连接登录按钮 信号槽
	connect(ui.loginBtn, &QPushButton::clicked, this, &UserLogin::onLoginBtnClicked);
}

void UserLogin::initTcpConnect() {
	//监听 TcpClient 的错误信号，弹窗提示用户
	connect(&TcpClient::getInstance(), &TcpClient::signalErrorOccurred,
		this, [this](const QString& errorMsg) {
			QMessageBox::warning(this, QStringLiteral("提示"),
				QStringLiteral("错误：") + errorMsg);
		});


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

bool UserLogin::verifyAccountCode(int& empID) {
	//查询数据库，验证账号密码是否正确，正确会修改形参为用户 employeeID

	//提取用户当前输入框的 账号密码
	QString strAccountInput = ui.editUserAccount->text();
	QString strCodeInput = ui.editPassword->text();

	//登陆方式一：按 employeeID 进行登录
	//employeeID 是 int 类型，需先判断输入是否为纯数字，避免非数字输入导致 SQL 语法错误
	bool isNumber = false;
	strAccountInput.toInt(&isNumber);

	if (isNumber) {
		//根据用户输入的 employeeID ，从数据库获取对应的密码
		//对应  C API  --------  mysql_real_query();
		//			   --------  mysql_store_result();
		QSqlQuery query;
		query.prepare("SELECT `code` FROM `tab_accounts` WHERE `employeeID` = ?");
		query.addBindValue(strAccountInput.toInt());
		bool ok = query.exec();
		if (ok == false) {
			//sql命令执行失败
			return false;
		}

		if (query.next()) {			//对应  C API  --------  mysql_fetch_row();
			//成功读取密码
			QString sqlCode = query.value(0).toString();
			if (sqlCode == strCodeInput) {
				//密码正确，记录用户 employeeID
				empID = strAccountInput.toInt();
				return true;
			}
			else {
				//密码错误
				return false;
			}
		}
	}
	//若输入非数字，或方式一未找到记录，继续尝试方式二

	//登陆方式二：按 account 进行登录
	//根据用户输入的 account ，从数据库获取对应的密码
	QSqlQuery query;
	query.prepare("SELECT `code`, `employeeID` FROM `tab_accounts` WHERE `account` = ?");
	query.addBindValue(strAccountInput);
	bool ok = query.exec();
	if (ok == false) {
		//sql命令执行失败
		return false;
	}

	if (query.next()) {
		QString sqlCode = query.value(0).toString();
		if (sqlCode == strCodeInput) {
			//密码正确，记录用户 employeeID
			empID = query.value(1).toInt();
			return true;
		}
		else {
			//密码错误
			return false;
		}
	}
	
	return false;
}


void UserLogin::onLoginBtnClicked() {
	//临时变量，用于记录用户 employeeID
	int empID = -1;

	//点击登录按钮后，先验证账号密码是否正确
	if (this->verifyAccountCode(empID) == false) {
		QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("您输入的账号密码有误，请重新输入！"));
		return;
	}

	//账号密码正确，传入用户 employeeID ，进入主窗口
	this->close();
	CCMainWindow* mainwindow = new CCMainWindow(empID);
	mainwindow->show();
}

