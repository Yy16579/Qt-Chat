#include "TalkWindow.h"
#include "RootContactItem.h"
#include "ContactItem.h"
#include "CommonUtils.h"
#include "WindowManager.h"
#include "TcpClient.h"

#include <QToolTip>
#include <QSqlQuery>


TalkWindow::TalkWindow(QWidget* parent, const int& uid)
	: QWidget(parent)
	, m_talkId(uid)
{
	ui.setupUi(this);

	this->setAttribute(Qt::WA_DeleteOnClose);
	this->initGroupStatus();		//根据 uid 判断是否为群聊
	this->initControl();
}

TalkWindow::~TalkWindow()
{
	//窗口析构同时，将 uid - TalkWindow 的映射从 WindowManager 中移除
	WindowManager::getInstance().deleteWindowName(this->m_talkId);		
}

void TalkWindow::addEmotionImage(int emotionNum) {
	ui.textEdit->setFocus();
	ui.textEdit->addEmotionUrl(emotionNum);
}

void TalkWindow::setWindowName(const QString& name) {
	ui.nameLabel->setText(name);
}

void TalkWindow::initControl() {
	QList<int> rightWidgetSize;
	rightWidgetSize << 600 << 138;
	ui.bodySplitter->setSizes(rightWidgetSize);

	ui.textEdit->setFontPointSize(10);
	ui.textEdit->setFocus();

	//连接信号槽
	TalkWindowShell* shell = qobject_cast<TalkWindowShell*>(this->parent());
	Q_ASSERT(shell);		//调试期断言，确保 parent 类型正确

	connect(ui.sysmin, &QPushButton::clicked, shell, &TalkWindowShell::onShowMin);
	connect(ui.sysclose, &QPushButton::clicked, shell, &TalkWindowShell::onShowClose);
	connect(ui.closeBtn, &QPushButton::clicked, shell, &TalkWindowShell::onShowClose);
	connect(ui.faceBtn, &QPushButton::clicked, shell, &TalkWindowShell::onEmotionBtnClicked);

	connect(ui.sendBtn, &QPushButton::clicked, this, &TalkWindow::onSendBtnClicked);
	connect(ui.treeWidget, &QTreeWidget::itemDoubleClicked, this, &TalkWindow::onItemDoubleClicked);

	connect(ui.msgWidget, &MsgWebView::signalSendMsg, this, &TalkWindow::onMsgSend);	//消息发送
	connect(&TcpClient::getInstance(), &TcpClient::signalMessageReceived, this, &TalkWindow::onMsgReceived);	//消息接收

	//初始化消息显示控件：注册头像模板对象并加载页面（必须在收发消息之前完成）
	ui.msgWidget->init(this->m_talkId, this->m_isGroupTalk);

	//初始化 treeWidget 控件
	if (this->m_isGroupTalk == true) {
		//为群聊
		this->initGroupTalk();
	}
	else {
		//为单聊
		this->initPTOPTalk();
	}
}

void TalkWindow::initGroupStatus() {
	//查询数据库，根据窗口 uid 判断是否为群聊 
	QSqlQuery query;
	query.prepare("SELECT `departmentID` FROM `tab_department` WHERE `departmentID` = ?");
	query.addBindValue(this->m_talkId);
	query.exec();
	if (query.next() == true) {
		//为群聊
		this->m_isGroupTalk = true;
	}
	else {
		//为单聊
		this->m_isGroupTalk = false;
	}
}

int TalkWindow::getCompDepID() {
	//查询数据库，获取 公司群 departmentID
	QSqlQuery query;
	query.prepare("SELECT `departmentID` FROM `tab_department` WHERE `department_name` = ?");
	query.addBindValue(QStringLiteral("公司群"));
	query.exec();
	query.next();
	return query.value(0).toInt();
}

void TalkWindow::initGroupTalk() {
	//为群聊，将所有该群的成员添加至右侧 treeWidget 联系人列表

	ui.treeWidget->setFixedHeight(646);
	
	//创建根项 
	QTreeWidgetItem* pRootItem = new QTreeWidgetItem();
	pRootItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
	pRootItem->setData(0, Qt::UserRole, 0);        //根项 key = <0, Qt::UserRole>,  value = 0

	//查询数据库，根据 uid 获取 department_name
	QSqlQuery query;
	query.prepare("SELECT `department_name` FROM `tab_department` WHERE `departmentID` = ?");
	query.addBindValue(this->m_talkId);
	query.exec();
	query.next();
	QString groupName = query.value(0).toString();

	//查询数据库，根据 departmentID 获取所有该群成员 employeeID
	QList<int> employeeIDs;
	int nEmployeeNum = 0;
	if (this->m_talkId == this->getCompDepID()) {
		//若为 公司群 ，则查询所有群的成员 employeeID
		query.prepare("SELECT `employeeID` FROM `tab_employees` WHERE `status` = ?");
		query.addBindValue(1);
		query.exec();
	}
	else {
		//不为 公司群，则查询对应群的成员 employeeID
		query.prepare("SELECT `employeeID` FROM `tab_employees` WHERE `status` = ? AND `departmentID` = ?");
		query.addBindValue(1);
		query.addBindValue(this->m_talkId);
		query.exec();
	}
	//将结果集存储起来，避免后续被破坏
	while (query.next() == true) {
		employeeIDs << query.value(0).toInt();
	}
	nEmployeeNum = query.size();

	//创建根项贴纸，设置贴纸信息
	RootContactItem* pItemName = new RootContactItem(false, ui.treeWidget);
	QString text = QStringLiteral("%1 %2/%3").arg(groupName).arg(0).arg(nEmployeeNum);
	pItemName->setText(text);

	//将根项添加至 treeWidget 控件
	ui.treeWidget->addTopLevelItem(pRootItem);
	ui.treeWidget->setItemWidget(pRootItem, 0, pItemName);

	pRootItem->setExpanded(true);		//默认展开根项，显示群成员
										//setExpanded 函数的生效前提是，该 item 已经被添加到 QTreeWidget 中

	//循环遍历结果集，将所有群聊成员添加至 treeWidget 联系人树
	for (int i = 0; i < employeeIDs.size(); i++) {
		this->addPeopleItem(pRootItem, employeeIDs.at(i));
	}
}

void TalkWindow::initPTOPTalk() {
	//为单聊，将右侧 treeWidget 控件初始化为一张图片
	QPixmap skinPix;
	skinPix.load(":/Resources/MainWindow/skin.png");
	
	QLabel* skinLabel = new QLabel(ui.widget);
	skinLabel->setPixmap(skinPix);
	skinLabel->setFixedSize(skinPix.size());

	ui.widget->setFixedSize(skinPix.size());
}

void TalkWindow::addPeopleItem(QTreeWidgetItem* pRootGroupItem, int empID) {
	//创建子项
	QTreeWidgetItem* pChild = new QTreeWidgetItem();

	//为子项绑定一份 “隐形的自定义业务数据”
	pChild->setData(0, Qt::UserRole, 1);        //子项 key <0, Qt::UserRole>,  value = 1
	pChild->setData(0, Qt::UserRole + 1, empID);     //单聊 key <0, Qt::UserRole + 1>, value = empID


	//查询数据库，根据 employeeID 获取对应的 picture , name , sign
	QSqlQuery query;
	query.prepare("SELECT `picture`, `employee_name`, `employee_sign` FROM `tab_employees` WHERE `employeeID` = ?");
	query.addBindValue(empID);
	query.exec();
	query.next();
	QPixmap peoplePix;
	peoplePix.load(query.value(0).toString());
	QString peopleName = query.value(1).toString();
	QString peopleSign = query.value(2).toString();

	//创建子项贴纸，设置贴纸信息
	ContactItem* pContactItem = new ContactItem(ui.treeWidget);
	QPixmap pix;
	pix.load(":/Resources/MainWindow/head_mask.png");
	pContactItem->setHeadPixmap(CommonUtils::getRoundImage(peoplePix, pix, pContactItem->getHeadLabelSize()));
	pContactItem->setUserName(peopleName);
	pContactItem->setSignName(peopleSign);

	//将子项添加到根项上
	pRootGroupItem->addChild(pChild);
	ui.treeWidget->setItemWidget(pChild, 0, pContactItem);
}	


//槽函数
void TalkWindow::onMsgSend(const QString& msg, int msgType, const QString file) {
	//通过 TcpClient 单例向服务端发送消息数据
	int sendID = WindowManager::getInstance().m_empID;
	TcpClient::getInstance().sendMessage(this->m_isGroupTalk, sendID, this->m_talkId, msgType, msg, file);
}

void TalkWindow::onMsgReceived(int groupFlag, int sendId, int recvId, int msgType, const QString& msg) {
	//消息接收：路由匹配 + wire→html 逆向转换 + 显示

	// 1.路由匹配：每个 TalkWindow 只处理属于自己的消息，其余忽略
	int myEmpID = WindowManager::getInstance().m_empID;
	if (groupFlag == 0) {
		//私聊：sendId 为对方、recvId 必须是我（过滤"对方发给别人"的消息）
		if (sendId != this->m_talkId || recvId != myEmpID) {
			return;
		}
	}
	else {
		//群聊：recvId 为群ID，排除自己发的消息
		//（服务端群聊转发暂未实现，此处为预留的完整判断逻辑）
		if (recvId != this->m_talkId || sendId == myEmpID) {
			return;
		}
	}

	// 2.wire → html 逆向转换（发送侧 appendMsg 拼包的镜像操作）
	QString html;
	if (msgType == 1) {
		//文本：wire = 5位长度前缀 + 文本内容，去掉前缀后直接包 html 骨架
		//（文本样式由气泡页 ui.css 决定，无需套字体模板）
		if (msg.size() <= 5) {
			return;
		}
		html = "<html><body>" + msg.mid(5) + "</body></html>";
	}
	else if (msgType == 0) {
		//表情：wire = 表情个数 + "images" + 每个3位编号，还原为 img 标签
		int idx = msg.indexOf("images");
		if (idx < 0) {
			return;
		}
		int count = msg.left(idx).toInt();
		QString nums = msg.mid(idx + QString("images").size());
		html = "<html><body>";
		for (int i = 0; i < count; i++) {
			int eNum = nums.mid(i * 3, 3).toInt();		//按3位宽度切出表情编号
			QPixmap pix(QString(":/Resources/MainWindow/emotion/%1.png").arg(eNum));
			html += QString("<img src=\"qrc:/Resources/MainWindow/emotion/%1.png\" width=\"%2\" height=\"%3\"/>")
				.arg(eNum).arg(pix.width()).arg(pix.height());
		}
		html += "</body></html>";
	}
	else {
		//文件消息（msgType==2）：暂不支持接收显示（TODO，对齐原项目）
		return;
	}

	// 3.显示：按发送者ID渲染左侧气泡（头像取 external_<sendId> 模板）
	ui.msgWidget->appendMsg(html, QString::number(sendId));
}

void TalkWindow::onSendBtnClicked() {
	// 若消息编辑窗口既无文本也无表情，直接返回
	//注意：表情以 <img> 插入，toPlainText() 不包含图片，纯表情消息需靠 hasEmotionImage() 放行
	if (ui.textEdit->toPlainText().isEmpty() && !ui.textEdit->hasEmotionImage()) {
		QToolTip::showText(this->mapToGlobal(QPoint(630, 660)), QStringLiteral("发送的信息不能为空！"), this, QRect(0, 0, 120, 100), 2000);
		return;
	}

	// 消息编辑窗口不为空，将对话框消息转换为 html 格式
	QString html = ui.textEdit->document()->toHtml();

	// 清除消息编辑窗口的所有消息
	ui.textEdit->clear();
	ui.textEdit->deleteAllEmotionImage();

	// 将 html 消息添加至 MsgWebView 网页
	ui.msgWidget->appendMsg(html);
}

void TalkWindow::onItemDoubleClicked(QTreeWidgetItem* item, int column) {
	bool bIsChild = item->data(0, Qt::UserRole).toBool();
	//如果为子项，则添加聊天窗口
	if (bIsChild) {
		//提取子项唯一标识 uid (单聊为 employeeID )
		int uid = item->data(0, Qt::UserRole + 1).toInt();
		WindowManager::getInstance().addNewTalkWindow(uid);
	}
}


