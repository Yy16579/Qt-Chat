#pragma once

#include <QWidget>
#include <QEnterEvent>
#include "ui_TalkWindowItem.h"



class TalkWindowItem : public QWidget
{
	Q_OBJECT

public:
	TalkWindowItem(QWidget *parent = nullptr);
	~TalkWindowItem();

public:
	void setHeadPixmap(const QPixmap& pixmap);
	void setMsgLabelContent(const QString& msg);
	QString getMsgLabelText();

private:
	void initControl();

private:
	//事件重写
	void enterEvent(QEnterEvent* event);
	void leaveEvent(QEvent* event);
	void resizeEvent(QResizeEvent* event);

signals:
	//信号
	void signalCloseClicked();

private:
	Ui::TalkWindowItemClass ui;
};

