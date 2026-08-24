#include "basicwindow.h"
#include "CommonUtils.h"
#include "NotifyManager.h"

#include <QFile>
#include <QApplication>
#include <QPainter>
#include <QStyleOption>
#include <QMouseEvent>
#include <QScreen>


BasicWindow::BasicWindow(QWidget *parent)
	: QDialog(parent)
{
	this->m_colorBackground = CommonUtils::getDefaultSkinColor();
	setWindowFlags(Qt::FramelessWindowHint);
	setAttribute(Qt::WA_TranslucentBackground, true);
	connect(&NotifyManager::getInstance(), &NotifyManager::signalSkinChanged, this, &BasicWindow::onSignalSkinChanged);
}

BasicWindow::~BasicWindow()
{}

//初始化标题栏
void BasicWindow::initTitleBar(ButtonType buttontype) {
	this->_titleBar = new TitleBar(this);
	this->_titleBar->setButtonType(buttontype);
	this->_titleBar->move(0, 0);

	connect(this->_titleBar, &TitleBar::signalButtonMinClicked, this, &BasicWindow::onButtonMinClicked);
	connect(this->_titleBar, &TitleBar::signalButtonRestoreClicked, this, &BasicWindow::onButtonRestoreClicked);
	connect(this->_titleBar, &TitleBar::signalButtonMaxClicked, this, &BasicWindow::onButtonMaxClicked);
	connect(this->_titleBar, &TitleBar::signalButtonCloseClicked, this, &BasicWindow::onButtonCloseClicked);
}

//设置标题栏标题
void BasicWindow::setTitleBarTitle(const QString& title, const QString& icon) {
	this->_titleBar->setTitleContent(title);
	this->_titleBar->setTitleIcon(icon);
}

//加载样式表
void BasicWindow::loadStyleSheet(const QString& sheetName) {
	this->m_styleName = sheetName;
	QFile file(":/Resources/QSS/" + sheetName + ".css");
	file.open(QFile::ReadOnly);
	if (file.isOpen()) {
		this->setStyleSheet("");
		QString qsstyleSheet = QLatin1String(file.readAll());

		// 防御性处理：去掉文件开头可能存在的 UTF-8 BOM 字符（\uFEFF）
		// BOM 会让 QSS 解析器无法识别首行选择器，导致整张样式表失效
		// 表现为窗口和子控件全部透明（WA_TranslucentBackground 生效但无背景色填充）
		if (qsstyleSheet.startsWith(QChar(0xFEFF))) {
			qsstyleSheet.remove(0, 1);
		}

		//获取当前背景颜色的 RGB 值
		QString r = QString::number(this->m_colorBackground.red());
		QString g = QString::number(this->m_colorBackground.green());
		QString b = QString::number(this->m_colorBackground.blue());

		qsstyleSheet += QString("QWidget[titleskin=true]\n"
			"{background-color:rgb(%1,%2,%3);\n"
			"border-top-left-radius:4px;}\n"
			"QWidget[bottomskin=true]\n"
			"{border-top:1px solid rgba(%1,%2,%3,100);\n"
			"background-color:rgba(%1,%2,%3,50);\n"
			"border-bottom-left-radius:4px;\n"
			"border-bottom-right-radius:4px;}")
			.arg(r).arg(g).arg(b);

		this->setStyleSheet(qsstyleSheet);
	}
	file.close();
}

//获取圆形头像
QPixmap BasicWindow::getRoundImage(const QPixmap& src, QPixmap& mask, QSize maskSize) {
	//将一张源图片（src），按照遮罩图片（mask）的形状裁剪成圆角形状的头像

	//确定目标尺寸
	if (maskSize == QSize(0, 0))
	{
		maskSize = mask.size();
	}
	else
	{
		mask = mask.scaled(maskSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}
	//创建透明背景的结果图像
	QImage resultImage(maskSize, QImage::Format_ARGB32_Premultiplied);
	QPainter painter(&resultImage);
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.fillRect(resultImage.rect(), Qt::transparent);
	//先绘制遮罩到目标图像
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.drawPixmap(0, 0, mask);
	//再将源图像以“SourceIn”模式绘制，只保留遮罩区域内的部分
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.drawPixmap(0, 0, src.scaled(maskSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	painter.end();

	return QPixmap::fromImage(resultImage);
}

//初始化背景
void BasicWindow::initBackGroundColor() {
	QStyleOption opt;
	opt.initFrom(this);

	QPainter p(this);
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}


//绘图事件
void BasicWindow::paintEvent(QPaintEvent* event) {
	this->initBackGroundColor();
	QDialog::paintEvent(event);
}

//鼠标按下事件
void BasicWindow::mousePressEvent(QMouseEvent* event) {
	if (event->button() == Qt::LeftButton) {		//鼠标左键按下
		this->m_mousePressed = true;
		this->m_mousePoint = event->globalPos() - this->pos();		//获取鼠标与窗口的偏移量
		event->accept();
	}
}

//鼠标移动事件
void BasicWindow::mouseMoveEvent(QMouseEvent* event) {
	if (this->m_mousePressed == true && (event->buttons() && Qt::LeftButton)) {		//鼠标左键按下
		this->move(event->globalPos() - this->m_mousePoint);
		event->accept();
	}
}

//鼠标释放事件
void BasicWindow::mouseReleaseEvent(QMouseEvent*) {
	this->m_mousePressed = false;
}


//槽函数
void BasicWindow::onShowClose(bool) {
	this->close();
}

void BasicWindow::onShowMin(bool) {
	this->showMinimized();
}

void BasicWindow::onShowHide(bool) {
	this->hide();
}

void BasicWindow::onShowNormal(bool) {
	this->show();
	this->activateWindow();
}

void BasicWindow::onShowQuit(bool) {
	QApplication::quit();
}	  

void BasicWindow::onSignalSkinChanged(const QColor& color) {	
	this->m_colorBackground = color;
	this->loadStyleSheet(this->m_styleName);
}

void BasicWindow::onButtonMinClicked() {
	if (Qt::Tool == (windowFlags() & Qt::Tool))		//判断窗口是否是工具窗口
	{
		this->hide();          //是工具窗口 → 隐藏
	}
	else
	{
		this->showMinimized(); //普通窗口 → 最小化到任务栏
	}
}

void BasicWindow::onButtonRestoreClicked() {
	QPoint windowPos;
	QSize windowSize;
	this->_titleBar->getRestoreInfo(windowPos, windowSize);
	this->setGeometry(QRect(windowPos, windowSize));
}

void BasicWindow::onButtonMaxClicked() {
	this->_titleBar->saveRestoreInfo(this->pos(), QSize(this->width(), this->height()));

	//Qt6 获取当前屏幕的可用区域
	QScreen* screen = QGuiApplication::primaryScreen();		//获取主屏幕
	QRect desktopRect = screen->availableGeometry();

	//扩大矩形，让窗口稍微超出屏幕边界，隐藏阴影/圆角
	QRect factRect = QRect(desktopRect.x() - 3, desktopRect.y() - 3,
						   desktopRect.width() + 6, desktopRect.height() + 6);

	this->setGeometry(factRect);
}

void BasicWindow::onButtonCloseClicked() {
	this->close();
}

