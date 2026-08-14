#pragma once

#include <QWidget>
#include "ui_EmotionWindow.h"

class EmotionWindow : public QWidget
{
	Q_OBJECT

public:
	EmotionWindow(QWidget *parent = nullptr);
	~EmotionWindow();

private:
	void initControl();

private:
	//事件重写
	void paintEvent(QPaintEvent* event) override;

signals:
	//信号
	void signalEmotionItemClicked(int emotionNum);

private slots:
	//槽函数
	void addEmotion(int emotionNum);

private:
	Ui::EmotionWindow ui;
};

