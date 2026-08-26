#include "TalkWindow.h"
#include "RootContactItem.h"
#include "ContactItem.h"
#include "CommonUtils.h"
#include "WindowManager.h"
#include "TcpClient.h"
#include "ContactBook.h"

#include <QToolTip>


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

	//消息发送与接收
	connect(ui.msgWidget, &MsgWebView::signalSendMsg, this, &TalkWindow::onMsgSend);	
	connect(&TalkSessionStore::getInstance(), &TalkSessionStore::signalMessageStored, this, &TalkWindow::onStoredMessage);	

	//页面加载完成才开始历史重放（QWebEngine 异步加载，load 前调 runJavaScript 会静默丢失）
	connect(ui.msgWidget, &QWebEngineView::loadFinished, this, &TalkWindow::onPageLoadFinished);


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
	//查 ContactBook 缓存，根据窗口 uid 判断是否为群聊 
	bool isGroup = ContactBook::getInstance().isGroup(this->m_talkId);
	if (isGroup == true) {
		//为群聊
		this->m_isGroupTalk = true;
	}
	else {
		//为单聊
		this->m_isGroupTalk = false;
	}
}

void TalkWindow::initGroupTalk() {
	//为群聊，将所有该群的成员添加至右侧 treeWidget 联系人列表

	ui.treeWidget->setFixedHeight(646);
	
	//创建根项 
	QTreeWidgetItem* pRootItem = new QTreeWidgetItem();
	pRootItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
	pRootItem->setData(0, Qt::UserRole, 0);        //根项 key = <0, Qt::UserRole>,  value = 0

	//查 ContactBook 缓存，根据 uid 获取 department_name
	DepartmentInfo depInfo = ContactBook::getInstance().departmentInfo(this->m_talkId);
	QString groupName = depInfo.name;

	//查 ContactBook 缓存，根据 departmentID 获取所有该群成员 employeeID
	QList<int> employeeIDs = ContactBook::getInstance().groupMembers(this->m_talkId);
	int nEmployeeNum = employeeIDs.size();
	
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


	//查 ContactBook 缓存，根据 employeeID 获取对应的 picture , name , sign
	EmployeeInfo empInfo = ContactBook::getInstance().employeeInfo(empID);
	QPixmap peoplePix;
	peoplePix.load(empInfo.picture);
	QString peopleName = empInfo.name;
	QString peopleSign = empInfo.sign;

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

void TalkWindow::renderRecord(const MsgRecord& record) {
	//按一条记录，增量渲染气泡
	//wire → html 逆向转换（发送侧 appendMsg 拼包的镜像操作）

	QString html;
	if (record.msgType == 1) {
		//文本：wire = 5位长度前缀 + 文本内容，去掉前缀后直接包 html 骨架
		if (record.msg.size() <= 5) {
			return;
		}
		html = "<html><body>" + record.msg.mid(5) + "</body></html>";
	}
	else if (record.msgType == 0) {
		//表情：wire = 表情个数 + "images" + 每个3位编号，还原为 img 标签
		int idx = record.msg.indexOf("images");
		if (idx < 0) {
			return;
		}
		int count = record.msg.left(idx).toInt();
		QString nums = record.msg.mid(idx + QString("images").size());
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
		//文件消息（msgType==2）：暂不支持显示（TODO，对齐原项目）
		return;
	}

	//渲染：mine=右侧气泡（"-1"纯显示模式，不 emit 发送信号）；对方=左侧气泡（按 sendId 定头像）
	if (record.mine) {
		ui.msgWidget->appendMsg(html, "-1");
	}
	else {
		ui.msgWidget->appendMsg(html, QString::number(record.sendId));
	}
}

void TalkWindow::replayHistory() {
	//加载本地仓库中本会话的全部历史记录（窗口刚创建/重开时）
	QList<MsgRecord> history = TalkSessionStore::getInstance().records(this->m_talkId);
	for (int i = 0; i < history.size(); i++) {
		this->renderRecord(history.at(i));
	}
}


//槽函数
void TalkWindow::onMsgSend(const QString& msg, int msgType, const QString file) {
	//向服务端发送消息数据
	int sendID = WindowManager::getInstance().m_empID;

	//发送失败（未连接/消息过长）直接返回，不入本地仓库：
	//否则断线期间的消息会被存库，重启后重放成"已发送"，实际对方从未收到（假发送）
	//正常流程 onSendBtnClicked 已做连接预检，此处是兜底（预检后瞬间断线、消息超长等场景）
	if (!TcpClient::getInstance().sendMessage(this->m_isGroupTalk, sendID, this->m_talkId, msgType, msg, file)) {
		return;
	}

	//自己发的消息同步入会话仓库（仅发送成功才入库）
	MsgRecord record;
	record.groupFlag = this->m_isGroupTalk ? 1 : 0;
	record.sendId = sendID;
	record.recvId = this->m_talkId;
	record.msgType = msgType;

	//入库格式必须与网络 wire 格式完全一致（renderRecord 按 mid(5) 解前缀）
	//文本：signalSendMsg 传出的 msg 尚无 5 位长度前缀（前缀由 sendMessage 补给网络包），
	//此处用相同算法补齐（UTF-8 字节数、右对齐补零），否则重放时前 5 字符被当前缀吃掉
	//表情：appendMsg 已拼好 "Nimages..." 头，无需处理
	if (msgType == 1) {
		record.msg = QString::number(msg.toUtf8().size()).rightJustified(5, '0') + msg;
	}
	else {
		record.msg = msg;
	}
	record.mine = true;
	TalkSessionStore::getInstance().appendSelfRecord(this->m_talkId, record);
}

void TalkWindow::onStoredMessage(int uid, int groupFlag, int sendId, int recvId, int msgType, const QString& msg) {
	//仓库广播入口
	if (uid != this->m_talkId) {
		return;
	}

	//按记录渲染左侧气泡
	MsgRecord record;
	record.groupFlag = groupFlag;
	record.sendId = sendId;
	record.recvId = recvId;
	record.msgType = msgType;
	record.msg = msg;
	record.mine = false;
	this->renderRecord(record);
}

void TalkWindow::onPageLoadFinished(bool ok) {
	//页面就绪后，加载全部历史消息记录（含自己发的与对方发的）
	if (ok) {
		this->replayHistory();
	}
}

void TalkWindow::onSendBtnClicked() {
	// 若消息编辑窗口既无文本也无表情，直接返回
	//注意：表情以 <img> 插入，toPlainText() 不包含图片，纯表情消息需靠 hasEmotionImage() 放行
	if (ui.textEdit->toPlainText().isEmpty() && !ui.textEdit->hasEmotionImage()) {
		QToolTip::showText(this->mapToGlobal(QPoint(630, 660)), QStringLiteral("发送的信息不能为空！"), this, QRect(0, 0, 120, 100), 2000);
		return;
	}

	//连接预检：断线期间不渲染气泡、不清空输入（防止"假发送"：气泡显示了但消息没出网）
	//此处拦截走静默气泡提示；sendMessage 内部还有二次兜底（返回 false 则不入库）
	if (!TcpClient::getInstance().isConnected()) {
		QToolTip::showText(this->mapToGlobal(QPoint(630, 660)), QStringLiteral("未连接到服务器，消息未发送！"), this, QRect(0, 0, 120, 100), 2000);
		return;
	}

	// 消息编辑窗口不为空，将对话框消息转换为 html 格式
	QString html = ui.textEdit->document()->toHtml();

	// 清除消息编辑窗口的所有消息（前置校验全部通过，此时清空才不会丢内容）
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


