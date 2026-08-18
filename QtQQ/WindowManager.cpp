#include "WindowManager.h"
#include "TalkWindow.h"
#include "TalkWindowItem.h"
#include "TalkSessionStore.h"

#include <QSqlQuery>


WindowManager::WindowManager()
	: QObject(nullptr)
	, m_talkwindowshell(nullptr)
	, m_empID(-1)
{
	//监听会话仓库广播：网络消息入仓后驱动开窗策略
	connect(&TalkSessionStore::getInstance(), &TalkSessionStore::signalMessageStored, this, &WindowManager::onStoredMessage);
}

WindowManager::~WindowManager()
{}

WindowManager& WindowManager::getInstance(){
	static WindowManager instance;
	return instance;
}

QWidget* WindowManager::findWindowName(const int& qsWindowName) {
	if (this->m_windowMap.contains(qsWindowName)) {
		return this->m_windowMap[qsWindowName];
	}

	return nullptr;
}

void WindowManager::deleteWindowName(const int& qsWindowName) {
	this->m_windowMap.remove(qsWindowName);
}

void WindowManager::addWindowName(const int& qsWindowName, QWidget* qWidget) {
	if (!this->m_windowMap.contains(qsWindowName)) {
		this->m_windowMap.insert(qsWindowName, qWidget);
	}
}

void WindowManager::addNewTalkWindow(const int& uid) {
	//若 TalkWindowShell 为空，则创建 TalkWindowShell
	if (this->m_talkwindowshell == nullptr) {
		this->m_talkwindowshell = new TalkWindowShell();

		//TalkWindowShell 销毁时，将指针置空
		connect(this->m_talkwindowshell, &TalkWindowShell::destroyed, this, [this]() {
			this->m_talkwindowshell = nullptr;
			});
	}
	
	//根据唯一标识 uid 查询对应的 TalkWindow 是否已创建
	QWidget* widget = this->findWindowName(uid);

	if (widget == nullptr) {
		// 1. 若未创建，则创建 TalkWindow 和 TalkWindowItem
		TalkWindow* talkwindow = new TalkWindow(this->m_talkwindowshell, uid);
		TalkWindowItem* talkwindowItem = new TalkWindowItem(talkwindow);
		
		// 2. 设置窗口的 name, sign, picture 信息
		//	  查询数据库，根据 uid 判断窗口是 单聊 还是 群聊，向对应的表提取 name, sign, picture 信息
		QString windowName;
		QString windowSign;
		QPixmap windowPix;

		QSqlQuery query;
		query.prepare("SELECT `department_name`, `sign`, `picture` FROM `tab_department` WHERE `departmentID` = ?;");
		query.addBindValue(uid);
		query.exec();
		if (query.next() == false) {
			//若窗口为单聊 ，则该表查询不到 department_name 和 sign 信息，换表查询
			QSqlQuery query1;
			query1.prepare("SELECT `employee_name`, `employee_sign`, `picture` FROM `tab_employees` WHERE `employeeID` = ?;");
			query1.addBindValue(uid);
			query1.exec();
			query1.next();
			windowName = query1.value(0).toString();
			windowSign = query1.value(1).toString();
			windowPix.load(query1.value(2).toString());
		}
		else {
			//若窗口为群聊，则能够查询到 department_name 和 sign 信息
			windowName = query.value(0).toString();
			windowSign = query.value(1).toString();
			windowPix.load(query.value(2).toString());
		}

		talkwindow->setWindowName(windowSign);
		talkwindowItem->setMsgLabelContent(windowName);
		talkwindowItem->setHeadPixmap(windowPix);

		// 3. 将新建的 TalkWindow 和 TalkWindowItem 加入到 TalkWindowShell 中
		this->m_talkwindowshell->addTalkWindow(talkwindow, talkwindowItem);

		// 4. 添加 uid - TalkWindow 的映射
		this->addWindowName(uid, talkwindow);
	}
	else {
		//若已创建，将该 ListWidgetItem 和 TalkWindow 设为选中
		QListWidgetItem* item = this->m_talkwindowshell->getTalkWindowListWidgetItem((TalkWindow*)widget);
		item->setSelected(true);
		this->m_talkwindowshell->setCurrentWidget(widget);
	}

	this->m_talkwindowshell->show();
	this->m_talkwindowshell->activateWindow();
}

TalkWindowShell* WindowManager::getTalkWindowShell() const {
	return m_talkwindowshell; 
};


//槽函数
void WindowManager::onStoredMessage(int talkId, int groupFlag, int sendId, int recvId, int msgType, const QString& msg) {
	//消息接收开窗策略：
	//聊天壳已打开（用户处于聊天场景）→ 自动打开/选中该会话窗口，消息即时可见
	//聊天壳未打开（非聊天场景）→ 不打扰：消息只躺在仓库里
	//Q_UNUSED：本槽只关心开窗，消息内容已由 TalkWindow 渲染
	Q_UNUSED(groupFlag);
	Q_UNUSED(sendId);
	Q_UNUSED(recvId);
	Q_UNUSED(msgType);
	Q_UNUSED(msg);

	if (this->m_talkwindowshell == nullptr) {
		return;		//壳未创建：不自动拉起聊天壳
	}

	//壳已存在：该会话窗口若已开则 addNewTalkWindow 内部自动选中，未开则创建并重放历史
	this->addNewTalkWindow(talkId);
}
