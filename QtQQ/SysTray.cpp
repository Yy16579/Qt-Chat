#include "SysTray.h"
#include "CustomMenu.h"
#include "CCMainWindow.h"


SysTray::SysTray(CCMainWindow* parent)
	: m_parent(parent)
	, QSystemTrayIcon(parent)
{
	this->initSystemTray();
	this->show();
}

SysTray::~SysTray()
{}

void SysTray::initSystemTray() {
	this->setToolTip(QString("QQproject"));		//设置托盘样式
	this->setIcon(QIcon(":/Resources/MainWindow/app/logo.ico"));
	connect(this, &QSystemTrayIcon::activated, this, &SysTray::onIconActivated);	//连接托盘激活信号槽
}

void SysTray::addSysTrayMenu() {	//临时菜单模式：每次右键都重新创建菜单，用完即毁，内存占用极低，且不会有常驻对象的状态残留问题
	//创建右键菜单栏
	CustomMenu* customMenu = new CustomMenu(this->m_parent);

	//添加菜单项
	customMenu->addCustomMenu("onShow",
							  ":/Resources/MainWindow/app/logo.ico",
							  QStringLiteral("显示"));
	customMenu->addCustomMenu("onLogout",
							  ":/Resources/MainWindow/qqlogoclassic.png",
							  QStringLiteral("退出登录"));
	customMenu->addCustomMenu("onQuit",
							  ":/Resources/MainWindow/app/page_close_btn_hover.png",
							  QStringLiteral("退出"));

	//连接菜单项信号槽
	connect(customMenu->getAction("onShow"), &QAction::triggered, this->m_parent, &CCMainWindow::onShowNormal);
	connect(customMenu->getAction("onQuit"), &QAction::triggered, this->m_parent, &CCMainWindow::onShowQuit);
	connect(customMenu->getAction("onLogout"), &QAction::triggered, this->m_parent, &CCMainWindow::onLogoutTriggered);

	//exec 模态阻塞调用，会启动一个局部事件循环，直到用户关闭菜单
	//QCursor::pos()：获取鼠标当前的全局屏幕坐标，让菜单正好在用户右键点击的位置弹出
	customMenu->exec(QCursor::pos());	
	
	//手动释放菜单内存
	delete customMenu;
	customMenu = nullptr;
}


//槽函数
void SysTray::onIconActivated(QSystemTrayIcon::ActivationReason reason) {	
	//根据 QSystemTrayIcon::ActivationReason（激活原因）分支处理
	if (reason == QSystemTrayIcon::Trigger) {		//Trigger（左键点击）
		this->m_parent->show();
		this->m_parent->activateWindow();
	}
	else if (reason == QSystemTrayIcon::Context) {	//Context（右键点击）
		this->addSysTrayMenu();
	}
}

