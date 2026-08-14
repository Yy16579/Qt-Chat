#include "EmotionWindow.h"
#include "CommonUtils.h"
#include "EmotionLabelItem.h"

#include <QStyleOption>
#include <QPainter>

const int EMOTIONCOLUMN = 14;
const int EMOTIONROW = 12;



EmotionWindow::EmotionWindow(QWidget *parent)
	: QWidget(parent)
{
	ui.setupUi(this);
	this->setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);
	this->setAttribute(Qt::WA_TranslucentBackground);
	this->setAttribute(Qt::WA_DeleteOnClose);

	this->initControl();
}

EmotionWindow::~EmotionWindow()
{}

void EmotionWindow::initControl() {
	CommonUtils::loadStyleSheet(this, "EmotionWindow");
	
	//创建表情标签
	for (int row = 0; row < EMOTIONROW; row++) {
		for (int column = 0; column < EMOTIONCOLUMN; column++) {
			EmotionLabelItem* label = new EmotionLabelItem(this);
			label->setEmotionName(row * EMOTIONCOLUMN + column);
			//添加到布局管理器中
			ui.gridLayout->addWidget(label, row, column);
			
			connect(label, &EmotionLabelItem::emotionClicked, this, &EmotionWindow::addEmotion);
		}
	}
}

//事件重写
void EmotionWindow::paintEvent(QPaintEvent* event) {
	//创建一个"样式选项"对象，用来保存窗口的所有状态信息
	QStyleOption opt;

	//把当前表情窗口的状态（大小、位置、是否启用、当前主题等）复制到opt里
	opt.initFrom(this);

	//创建一个"画家"，绑定到当前表情窗口，准备画画
	QPainter painter(this);

	//最关键的一行：调用Qt的全局样式系统，绘制一个普通窗口的背景和边框
	style()->drawPrimitive(QStyle::PE_Widget, &opt, &painter, this);

	//调用父类的paintEvent，让父类完成剩下的绘制工作
	QWidget::paintEvent(event);
}


//槽函数
void EmotionWindow::addEmotion(int emotionNum) {
	this->hide();		//点击表情之后，窗口进行隐藏
	emit this->signalEmotionItemClicked(emotionNum);		//发射点击表情信号
}

