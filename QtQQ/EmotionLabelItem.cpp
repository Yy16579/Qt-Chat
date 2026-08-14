#include "EmotionLabelItem.h"



EmotionLabelItem::EmotionLabelItem(QWidget *parent)
	: QClickLabel(parent)
{
	this->initControl();
	connect(this, &QClickLabel::clicked, this, [this]() {
			emit this->emotionClicked(this->m_emotionName);
		});
}

EmotionLabelItem::~EmotionLabelItem()
{}

void EmotionLabelItem::setEmotionName(int emotionName) {
	this->m_emotionName = emotionName;
	QString imageName = QString(":/Resources/MainWindow/emotion/%1.png").arg(emotionName);

	//设置 QMovie 动图
	//QMovie（图片路径，格式，父类）
	this->m_apngMovie = new QMovie(imageName, "apng", this);
	this->m_apngMovie->start();
	this->m_apngMovie->stop();
	this->setMovie(this->m_apngMovie);
}

void EmotionLabelItem::initControl() {
	this->setAlignment(Qt::AlignCenter);
	this->setObjectName("emotionLabelItem");
	this->setFixedSize(32, 32);
}

