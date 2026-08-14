#pragma once

#include "titlebar.h"

#include <QDialog>



class BasicWindow  : public QDialog
{
	Q_OBJECT

public:
	BasicWindow(QWidget *parent = nullptr);
	virtual ~BasicWindow();

public:
	void loadStyleSheet(const QString& sheetName);		//加载样式表
	QPixmap getRoundImage(const QPixmap& src, QPixmap& mask, QSize maskSize = QSize(0, 0));		//获取圆形头像

private:
	void initBackGroundColor();			//初始化背景

protected:
	//初始化标题栏
	void initTitleBar(ButtonType buttontype = ButtonType::MIN_BUTTON);
	void setTitleBarTitle(const QString& title, const QString& icon = "");

protected:
	//事件重写
	void paintEvent(QPaintEvent* event);			//绘图事件
	void mousePressEvent(QMouseEvent* event);		//鼠标按下事件
	void mouseMoveEvent(QMouseEvent* event);		//鼠标移动事件
	void mouseReleaseEvent(QMouseEvent*);			//鼠标释放事件

public slots:
	//主窗口槽函数
	void onShowClose(bool);
	void onShowMin(bool);
	void onShowHide(bool);
	void onShowNormal(bool);
	void onShowQuit(bool);
	void onSignalSkinChanged(const QColor& color);	//更改背景色

	void onButtonMinClicked();
	void onButtonRestoreClicked();
	void onButtonMaxClicked();
	void onButtonCloseClicked();

protected:
	//成员变量
	TitleBar* _titleBar;		//标题栏
	QColor m_colorBackground;	//背景颜色
	QString m_styleName;		//样式文件名称

	//鼠标信息
	bool m_mousePressed;		//鼠标是否按下
	QPoint m_mousePoint;		//鼠标与窗口的偏移量

};
