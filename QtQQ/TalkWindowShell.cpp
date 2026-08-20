#include "TalkWindowShell.h"
#include "TalkWindow.h"
#include "TalkWindowItem.h"
#include "CommonUtils.h"	
#include "TcpClient.h"

#include <QListWidget>
#include <QMessageBox>



TalkWindowShell::TalkWindowShell(QWidget* parent)
	: BasicWindow(parent)
{
	ui.setupUi(this);
	this->setAttribute(Qt::WA_DeleteOnClose);
	this->initControl();
}

TalkWindowShell::~TalkWindowShell()
{
	//断开 TcpClient 信号槽，避免析构后 lambda 仍访问 this
	disconnect(&TcpClient::getInstance(), &TcpClient::signalErrorOccurred, this, nullptr);

	delete this->m_emotionWindow;
	this->m_emotionWindow = nullptr;
}

void TalkWindowShell::addTalkWindow(TalkWindow* talkWindow, TalkWindowItem* talkWindowItem) {
	// 1. 将创建好的 TalkWindow 添加至 TalkWindowShell 中
	ui.rightStackedWidget->addWidget(talkWindow);
	
	// 2. 将创建好的 TalkWindowItem 添加至 TalkWindowShell 中
	QListWidgetItem* aItem = new QListWidgetItem(ui.listWidget);
	ui.listWidget->addItem(aItem);

	ui.listWidget->setItemWidget(aItem, talkWindowItem);

	connect(talkWindowItem, &TalkWindowItem::signalCloseClicked, this, [talkWindowItem, talkWindow, aItem, this]() {
		//当点击关闭时，将对应的窗口从 TalkWindowShell 中移除
		//将映射从 map 中移除
		this->m_talkWindowItemMap.remove(aItem);
		
		//将 ListWidgetItem 和 TalkWindowItem 移除
		ui.listWidget->takeItem(ui.listWidget->row(aItem));
		delete talkWindowItem;

		//将 TalkWindow 移除
		talkWindow->close();
		ui.rightStackedWidget->removeWidget(talkWindow);
		
		//若 TalkWindowShell 中窗口数量 < 1，则将 TalkWindowShell 也移除
		if (ui.rightStackedWidget->count() < 1) {
			this->close();
		}
		});

	// 3. 将添加的窗口设为选中
	aItem->setSelected(true);
	this->setCurrentWidget(talkWindow);

	// 4. 将 LisWidgetItem - TalkWindow 添加到映射
	this->m_talkWindowItemMap.insert(aItem, talkWindow);
}

void TalkWindowShell::setCurrentWidget(QWidget* widget) {
	ui.rightStackedWidget->setCurrentWidget(widget);
}

QListWidgetItem* TalkWindowShell::getTalkWindowListWidgetItem(TalkWindow* talkWindow) const {
	return this->m_talkWindowItemMap.key(talkWindow);
}

void TalkWindowShell::initControl() {
	this->loadStyleSheet("TalkWindow");
	this->setWindowTitle(QStringLiteral("testWindow"));

	//界面布局设置
	QList<int> leftWidgetSize;
	leftWidgetSize << 154 << (this->width() - 154);
	ui.splitter->setSizes(leftWidgetSize);

	ui.listWidget->setStyle(new CustomProxyStyle(this));

	//创建表情窗口，将窗口隐藏
	this->m_emotionWindow = new EmotionWindow(this);
	this->m_emotionWindow->hide();

	//连接信号槽
	connect(ui.listWidget, &QListWidget::itemClicked, this, &TalkWindowShell::onTalkWindowItemClicked);
	connect(this->m_emotionWindow, &EmotionWindow::signalEmotionItemClicked, this, &TalkWindowShell::onEmotionItemClicked);
	connect(&TcpClient::getInstance(), &TcpClient::signalErrorOccurred,
		this, [this](const QString& errorMsg) {
			//监听 TcpClient 错误信号，弹窗提示用户
			QMessageBox::warning(this, QStringLiteral("提示"),
				QStringLiteral("错误：") + errorMsg);
		});
}

//槽函数
void TalkWindowShell::onEmotionBtnClicked(bool) {
	//切换表情窗口的显示 / 隐藏状态
	this->m_emotionWindow->setVisible(!this->m_emotionWindow->isVisible());		

	//获取聊天窗口左上角的绝对坐标
	QPoint emotionPoint = this->mapToGlobal(QPoint(0, 0));

	//根据聊天窗口绝对坐标，获取表情窗口的绝对坐标
	emotionPoint.setX(emotionPoint.x() + 170);
	emotionPoint.setY(emotionPoint.y() + 220);

	//设置表情窗口坐标
	this->m_emotionWindow->move(emotionPoint);
}

void TalkWindowShell::onTalkWindowItemClicked(QListWidgetItem* item) {
	//点击左边的 TalkWindowItem时，右边切换为对应的TalkWindow
	QWidget* talkwindowWidget = this->m_talkWindowItemMap[item];
	ui.rightStackedWidget->setCurrentWidget(talkwindowWidget);
}

void TalkWindowShell::onEmotionItemClicked(int emotionNum) {
	//获取当前 TalkWindow
	TalkWindow* curTalkWindow = dynamic_cast<TalkWindow*>(ui.rightStackedWidget->currentWidget());

	//将表情添加至当前对话窗口
	if (curTalkWindow) {
		curTalkWindow->addEmotionImage(emotionNum);
	}
}


