#pragma once

#include <QLabel>

class QClickLabel  : public QLabel
{
	Q_OBJECT

public:
	QClickLabel(QWidget *parent);
	~QClickLabel();

protected:
	//事件重写
	void mousePressEvent(QMouseEvent* event);

signals:
	//信号
	void clicked();

};

