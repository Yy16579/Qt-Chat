#include "titlebar.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFile>



#define BUTTON_HEIGHT 27		//按钮高度
#define BUTTON_WIDTH 27			//按钮宽度
#define TITLE_HEIGHT 27			//标题栏高度



TitleBar::TitleBar(QWidget *parent)
	: QWidget(parent)
	,m_buttonType(ButtonType::MIN_MAX_BUTTON)
	,m_isPressed(false)
{
	this->initControl();
	this->initConnections();
	this->loadStyleSheet("Title");
}

TitleBar::~TitleBar()
{}

//初始化控件
void TitleBar::initControl() {
	//创建控件对象
	this->m_pIcon = new QLabel(this);
	this->m_pTitleContent = new QLabel(this);

	this->m_pButtonMin = new QPushButton(this);
	this->m_pButtonRestore = new QPushButton(this);
	this->m_pButtonMax = new QPushButton(this);
	this->m_pButtonClose = new QPushButton(this);

	//设置对象名
	this->m_pTitleContent->setObjectName("TitleContent");
	this->m_pButtonMin->setObjectName("ButtonMin");
	this->m_pButtonRestore->setObjectName("ButtonRestore");
	this->m_pButtonMax->setObjectName("ButtonMax");
	this->m_pButtonClose->setObjectName("ButtonClose");

	//设置按钮大小
	this->m_pButtonMin->setFixedSize(QSize(BUTTON_HEIGHT, BUTTON_WIDTH));
	this->m_pButtonRestore->setFixedSize(QSize(BUTTON_HEIGHT, BUTTON_WIDTH));
	this->m_pButtonMax->setFixedSize(QSize(BUTTON_HEIGHT, BUTTON_WIDTH));
	this->m_pButtonClose->setFixedSize(QSize(BUTTON_HEIGHT, BUTTON_WIDTH));

	//设置布局
	QHBoxLayout* mylayout = new QHBoxLayout(this);			//水平布局管理器 QHBoxLayout
	mylayout->addWidget(m_pIcon);							//将控件依次加入布局管理器中
	mylayout->addWidget(m_pTitleContent);

	mylayout->addWidget(m_pButtonMin);
	mylayout->addWidget(m_pButtonRestore);
	mylayout->addWidget(m_pButtonMax);
	mylayout->addWidget(m_pButtonClose);

	mylayout->setContentsMargins(5, 0, 0, 0);				//设置布局外侧的留白
	mylayout->setSpacing(0);								//设置布局内各个控件之间的间隙

	this->m_pTitleContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setFixedHeight(TITLE_HEIGHT);						//固定高度
	setWindowFlags(Qt::FramelessWindowHint);			//无边框
}

//初始化信号槽的连接
void TitleBar::initConnections() {
	connect(this->m_pButtonMin, &QPushButton::clicked, this, &TitleBar::onButtonMinClicked);
	connect(this->m_pButtonRestore, &QPushButton::clicked, this, &TitleBar::onButtonRestoreClicked);
	connect(this->m_pButtonMax, &QPushButton::clicked, this, &TitleBar::onButtonMaxClicked);
	connect(this->m_pButtonClose, &QPushButton::clicked, this, &TitleBar::onButtonCloseClicked);
}

//加载样式表
void TitleBar::loadStyleSheet(const QString& sheetName) {
	QFile file(":/Resources/QSS/" + sheetName + ".css");
	file.open(QFile::ReadOnly);
	if (file.isOpen()) {
		QString styleSheet = this->styleSheet();
		styleSheet += QLatin1String(file.readAll());
		this->setStyleSheet(styleSheet);
	}
	file.close();
}

//设置标题栏图标
void TitleBar::setTitleIcon(const QString& filePath) {
	QPixmap pix;
	pix.load(filePath);
	this->m_pIcon->setPixmap(pix);
	this->m_pIcon->setFixedSize(pix.size());
}

//设置标题栏内容
void TitleBar::setTitleContent(const QString& titleContent) {
	this->m_titleContent = titleContent;
	this->m_pTitleContent->setText(this->m_titleContent);	
}

//设置标题栏长度
void TitleBar::setTitleWidth(int width) {
	this->m_pTitleContent->setFixedWidth(width);
}

//设置标题栏按钮类型
void TitleBar::setButtonType(ButtonType buttonType) {
	this->m_buttonType = buttonType;

	switch (m_buttonType)
	{
	case ButtonType::MIN_BUTTON:
		this->m_pButtonRestore->setVisible(false);
		this->m_pButtonMax->setVisible(false);
		break;
	case ButtonType::MIN_MAX_BUTTON:
		this->m_pButtonRestore->setVisible(false);
		break;
	case ButtonType::ONLY_CLOSE_BUTTON:
		this->m_pButtonMin->setVisible(false);
		this->m_pButtonRestore->setVisible(false);
		this->m_pButtonMax->setVisible(false);
		break;
	default:
		break;
	}
}

//保存窗口最大化前，窗口的位置大小
void TitleBar::saveRestoreInfo(const QPoint& point, const QSize& size) {
	this->m_restorePos = point;
	this->m_restoreSize = size;
}

//获取窗口最大化前，窗口的位置大小
void TitleBar::getRestoreInfo(QPoint& point, QSize& size) {
	point = this->m_restorePos;
	size = this->m_restoreSize;
}

//绘图事件（绘制标题栏背景）
void TitleBar::paintEvent(QPaintEvent* event) {
	QPainter painter(this);
	QPainterPath pathBack;
	pathBack.setFillRule(Qt::WindingFill);	//设置填充规则
	pathBack.addRoundedRect(QRect(0, 0, width(), height()), 3, 3);	//添加圆角矩形到绘图路径
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

	//宽度同步，标题栏宽度自动跟随父窗口变化
	if (width() != parentWidget()->width())
	{
		setFixedWidth(parentWidget()->width());
	}

	QWidget::paintEvent(event);
}

//鼠标双击事件（双击实现标题栏最大化最小化操作）
void TitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
	//只有存在最大化和最小化按钮才有效
	if (this->m_buttonType == ButtonType::MIN_MAX_BUTTON) {
		if (this->m_pButtonMax->isVisible()) {
			this->onButtonMaxClicked();
		}
		else if (this->m_pButtonRestore->isVisible()) {
			this->onButtonRestoreClicked();
		}
	}

	return QWidget::mouseDoubleClickEvent(event);
}

//鼠标按下事件（获取鼠标的坐标）
void TitleBar::mousePressEvent(QMouseEvent* event) {
	if (this->m_buttonType == ButtonType::MIN_MAX_BUTTON) {
		//窗口最大化时禁止拖动窗口
		if (this->m_pButtonMax->isVisible()) {
			this->m_isPressed = true;
			this->m_startMovePos = event->globalPos();		
		}
	}
	else {
		this->m_isPressed = true;
		this->m_startMovePos = event->globalPos();
	}

	return QWidget::mousePressEvent(event);
}

//鼠标移动事件（根据鼠标偏移量移动窗口）
void TitleBar::mouseMoveEvent(QMouseEvent* event) {
	if (this->m_isPressed == true) {
		QPoint movePoint = event->globalPos() - this->m_startMovePos;		//获取鼠标移动偏移量
		QPoint widgetPos = parentWidget()->pos();		//获取父控件坐标
		parentWidget()->move(widgetPos.x() + movePoint.x(), widgetPos.y() + movePoint.y());			//移动父控件
		this->m_startMovePos = event->globalPos();			//更新鼠标坐标
	}

	return QWidget::mouseMoveEvent(event);
}

//鼠标释放事件
void TitleBar::mouseReleaseEvent(QMouseEvent* event) {
	this->m_isPressed = false;

	return QWidget::mouseReleaseEvent(event);
}


//标题栏槽函数
void TitleBar::onButtonMinClicked() {
	emit this->signalButtonMinClicked();
}

void TitleBar::onButtonRestoreClicked() {
	this->m_pButtonRestore->setVisible(false);
	this->m_pButtonMax->setVisible(true);
	emit this->signalButtonRestoreClicked();
}

void TitleBar::onButtonMaxClicked() {
	this->m_pButtonMax->setVisible(false);
	this->m_pButtonRestore->setVisible(true);
	emit this->signalButtonMaxClicked();
}

void TitleBar::onButtonCloseClicked() {
	emit this->signalButtonCloseClicked();
}
