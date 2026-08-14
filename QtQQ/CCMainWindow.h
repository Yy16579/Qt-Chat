#pragma once

#include "ui_CCMainWindow.h"
#include "basicwindow.h"



class CCMainWindow : public BasicWindow
{
	Q_OBJECT

public:
	CCMainWindow(int empID, QWidget *parent = nullptr);
	~CCMainWindow();

public:
	void setUserName(const QString& username);			//设置用户名
	void setLevelPixmap(int level);						//设置等级图像
	void setHeadPixmap(const QString& headPath);		//设置用户头像
	void setStatusMenuIcon(const QString& statusPath);	//设置状态

	QWidget* addOtherAppExtension(const QString& appPath, const QString& appName);	//添加应用控件
	void initContactTree();			//初始化联系人树

private:
	void initControl();		//初始化控件
	void initTimer();		//初始化计时器	
	void initUserInfo();	//初始化用户信息

	void updateSearchStyle();		//更新/还原搜索栏样式
	void addGroupItem(QTreeWidgetItem* pRootGroupItem, int depID);		//联系人树 添加群聊子项

private:
	//事件重写
	bool eventFilter(QObject* obj, QEvent* event);		//事件过滤器

	void resizeEvent(QResizeEvent* event);
	void mousePressEvent(QMouseEvent* event);

private slots:
	//槽函数
	void onBottonAppClicked();		//应用控件槽函数

	void onItemClicked(QTreeWidgetItem* item, int column);
	void onItemExpanded(QTreeWidgetItem* item);
	void onItemCollapsed(QTreeWidgetItem* item);
	void onItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
	//成员变量
	int m_empID;		//当前登录用户的 employeeID

private:
	Ui::CCMainWindowClass ui;
};

