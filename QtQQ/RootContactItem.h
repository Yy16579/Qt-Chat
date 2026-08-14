#pragma once

#include <QLabel>
#include <QPropertyAnimation>


/**
 * RootContactItem 类
 * 自定义联系人列表根分组项控件，继承自QLabel
 *          实现"带可动画旋转箭头的分类标题"功能
 *          用于QQ联系人列表的一级分组（如"我的好友"、"群聊"等）
 *          支持展开/收起状态切换，箭头平滑旋转动画
 */

//QTreeWidgetItem根项贴纸类
class RootContactItem  : public QLabel
{
	Q_OBJECT

	Q_PROPERTY(int rotation READ rotation WRITE setRotation);
public:
	RootContactItem(bool hasArrow = true, QWidget* parent = nullptr);
	~RootContactItem();

public:
	void setText(const QString& title);		//设置分组标题文本
	void setExpanded(bool expand);			//设置分组展开/收起状态

private:
	int rotation();							//获取当前箭头旋转角度
	void setRotation(int rotation);			//设置箭头旋转角度

protected:
	void paintEvent(QPaintEvent* event);	//绘图事件

private:
	QPropertyAnimation* m_animation;	//箭头旋转属性动画对象
	QString m_titleText;				//分组标题文本
	int m_rotation;						//箭头当前旋转角度
	bool m_hasArrow;					//是否显示箭头图标

};

