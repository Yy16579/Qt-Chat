#pragma once

#include "ui_TalkWindowShell.h"
#include "basicwindow.h"
#include "EmotionWindow.h"

#include <QMap>



//前向声明  减少依赖、加快编译、隐藏实现细节
class TalkWindow;
class TalkWindowItem;


class TalkWindowShell : public BasicWindow
{
	Q_OBJECT

public:
	TalkWindowShell(QWidget *parent = nullptr);
	~TalkWindowShell();

public:
	void addTalkWindow(TalkWindow* talkWindow, TalkWindowItem* talkWindowItem);
	void setCurrentWidget(QWidget* widget);
	QListWidgetItem* getTalkWindowListWidgetItem(TalkWindow* talkWindow) const;

private:
	void initControl();

	//--------------------------------------------------------------------------
	void getEmployeeID(QStringList& employeesList);			//获取所有员工QQ号
	bool creatJSFile(QStringList& employeeList);			//创建JS文件
	void initJSFile();										//延迟初始化JS文件（生成 msgtmpl.js）
	//--------------------------------------------------------------------------

public slots:
	//槽函数
	void onEmotionBtnClicked(bool);

private slots:
	void onTalkWindowItemClicked(QListWidgetItem* item);
	void onEmotionItemClicked(int emotionNum);

private:
	//成员变量
	QMap<QListWidgetItem*, QWidget*> m_talkWindowItemMap;	// ListWidgetItem - TalkWindow 的映射
	EmotionWindow* m_emotionWindow;

private:
	Ui::TalkWindowClass ui;
};

