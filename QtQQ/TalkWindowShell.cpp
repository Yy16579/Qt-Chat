#include "TalkWindowShell.h"
#include "TalkWindow.h"
#include "TalkWindowItem.h"
#include "CommonUtils.h"
#include "TcpClient.h"

#include <QListWidget>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QSqlQuery>
#include <QTimer>
#include <QDebug>



TalkWindowShell::TalkWindowShell(QWidget *parent)
	: BasicWindow(parent)
{
	ui.setupUi(this);
	this->setAttribute(Qt::WA_DeleteOnClose);
	this->initControl();

	//JS文件初始化延迟到事件循环执行，避免构造阶段弹框（窗口尚未完全显示）
	QTimer::singleShot(0, this, [this]() {
		this->initJSFile();
	});
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
	this->m_emotionWindow = new EmotionWindow;
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

void TalkWindowShell::getEmployeeID(QStringList& employeesList) {
	QSqlQuery query;
	query.prepare("SELECT `employeeID` FROM `tab_employees` WHERE `status` = ?");
	query.addBindValue(1);
	query.exec();
	while (query.next() == true) {
		employeesList << query.value(0).toString();
	}
}

bool TalkWindowShell::creatJSFile(QStringList& employeeList)
{
	// 读取txt文件数据（模板从 qrc 读取，保证稳定可读）
	QString strFileTxt = ":/Resources/MainWindow/MsgHtml/msgtmpl.txt";
	QFile fileRead(strFileTxt);

	QString strFile;			// 保存读取到的数据

	// 判断以只读模式打开，能否打开成功
	if (fileRead.open(QIODevice::ReadOnly))
	{
		// 打开成功，读取全部
		strFile = fileRead.readAll();
		fileRead.close();			// 读取完了就关闭
	}
	else
	{
		qDebug() << "读取 msgtmpl.txt 失败";
		return false;
	}

	// 替换（external0，appendHtml0，用作自己发信息使用）
	// msgtmpl.js 写入到 exe 工作目录，先确保目录存在
	QString outputDir = "Resources/MainWindow/MsgHtml";
	QDir().mkpath(outputDir);
	QFile fileWrite(outputDir + "/msgtmpl.js");

	// 写入模式 ，和 覆盖模式
	if (fileWrite.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		/*
			1，初始化对象为 null
			2，通道里面设置对象
			3，在接受html那里写入数据
		*/

		// 更新空值
		// 原始的，初始化的，空值的代码
		QString strSourceInitNull = "var external = null;";

		// 更新初始化
		// 原始的，初始化的，对象的代码
		QString strSourceInit = "external = channel.objects.external;";

		// 更新newWebchannel
		// 这里把 external0 去掉，等到时候再添加上
		// 原始的都用 external
		QString strSourceNew =
			"new QWebChannel(qt.webChannelTransport,\
			function(channel) {\
			external = channel.objects.external;\
		}\
		); \
		";

		// 更新追加recvHtml
		/*
			这一段里面有双引号，会引起冲突
			所以不能像上面那样直接写在双引号里，

			因此，要通过读取字符串的方法，读到 strSourceRecvHtml 里面
			把下面这段代码，单独放到一个 txt文件里，再进行读取

			function recvHtml(msg){
					$("#placeholder").append(external.msgLHtmlTmpl.format(msg));
					window.scrollTo(0,document.body.scrollHeight); ;
			};
		*/
		QString strSourceRecvHtml;
		QFile fileRecvHtml(":/Resources/MainWindow/MsgHtml/recvHtml.txt");
		if (fileRecvHtml.open(QIODevice::ReadOnly))
		{
			// 先读取全部，再赋值，再关闭
			strSourceRecvHtml = fileRecvHtml.readAll();
			fileRecvHtml.close();
		}
		else
		{
			qDebug() << "读取 recvHtml.txt 失败";
			return false;
		}

		// 保存替换后的脚本，对应上面的4个
		QString strReplaceInitNull;
		QString strReplaceInit;
		QString strReplaceNew;
		QString strReplaceRecvHtml;

		for (int i = 0; i < employeeList.length(); i++)
		{
			// 编辑替换后的 空值
			QString strInitNull = strSourceInitNull;
			strInitNull.replace("external", QString("external_%1").arg(employeeList.at(i)));
			strReplaceInitNull += strInitNull;
			strReplaceInitNull += "\n";

			// 编辑替换后的 初始值
			QString strInit = strSourceInit;
			strInit.replace("external", QString("external_%1").arg(employeeList.at(i)));
			strReplaceInit += strInit;
			strReplaceInit += "\n";

			// 编辑替换后的 newWebchannel
			QString strNew = strSourceNew;
			strNew.replace("external", QString("external_%1").arg(employeeList.at(i)));
			strReplaceNew += strNew;
			strReplaceNew += "\n";

			// replace，替换修改的，直接就是 strRecvHtml
			// 编辑替换后的 recvHtml
			QString strRecvHtml = strSourceRecvHtml;
			strRecvHtml.replace("external", QString("external_%1").arg(employeeList.at(i)));
			strRecvHtml.replace("recvHtml", QString("recvHtml_%1").arg(employeeList.at(i)));
			strReplaceRecvHtml += strRecvHtml;
			strReplaceRecvHtml += "\n";
		}

		// 上面的for循环走完，有几个员工，就会出现几组
		// 然后 再将替换后的字符串，
		// 拿来 替换到 读取数据的【原始文件】的 【原字符串】 上
		strFile.replace(strSourceInitNull, strReplaceInitNull);
		strFile.replace(strSourceInit, strReplaceInit);
		strFile.replace(strSourceNew, strReplaceNew);
		strFile.replace(strSourceRecvHtml, strReplaceRecvHtml);

		// strFile 就是最终数据了
		// 用一个文件流，写入进去
		QTextStream stream(&fileWrite);
		stream << strFile;
		fileWrite.close();

		return true;
	}
	else
	{
		qDebug() << "写入 msgtmpl.js 失败";

		return false;
	}
}

void TalkWindowShell::initJSFile() {
	//每次启动都重新生成，保证与数据库员工数据同步
	QStringList employeeIDList;
	getEmployeeID(employeeIDList);

	if (!creatJSFile(employeeIDList)) {
		qDebug() << "JS文件写入数据失败";
	}
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


