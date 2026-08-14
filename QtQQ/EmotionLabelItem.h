#pragma once

#include "QClickLabel.h"

#include <QMovie>



//自定义表情标签类
class EmotionLabelItem  : public QClickLabel
{
	Q_OBJECT

public:
	EmotionLabelItem(QWidget *parent);
	~EmotionLabelItem();

public:
	void setEmotionName(int emotionName);

private:
	void initControl();

signals:
	//信号
	void emotionClicked(int emotionNum);

private:
	//成员变量
	int m_emotionName;
	QMovie* m_apngMovie;
};

