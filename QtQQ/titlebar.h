#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>



enum class ButtonType {
	MIN_BUTTON = 0,			//最小化及关闭按钮
	MIN_MAX_BUTTON,			//最小化，最大化及关闭按钮
	ONLY_CLOSE_BUTTON,		//只有关闭按钮
};


//自定义标题栏
class TitleBar  : public QWidget
{
	Q_OBJECT

public:
	TitleBar(QWidget *parent);
	~TitleBar();
	
	void setTitleIcon(const QString& filePath);					//设置标题栏图标
	void setTitleContent(const QString& titleContent);			//设置标题栏内容
	void setTitleWidth(int width);								//设置标题栏长度
	void setButtonType(ButtonType buttonType);					//设置标题栏按钮类型
 
	//保存，获取窗口最大化前，窗口的位置大小
	void saveRestoreInfo(const QPoint& point, const QSize& size);
	void getRestoreInfo(QPoint& point, QSize& size);

private:
	//事件重写
	void paintEvent(QPaintEvent* event);			//绘图事件
	void mouseDoubleClickEvent(QMouseEvent* event); //鼠标双击事件
	void mousePressEvent(QMouseEvent* event);		//鼠标按下事件
	void mouseMoveEvent(QMouseEvent* event);		//鼠标移动事件
	void mouseReleaseEvent(QMouseEvent* event);		//鼠标释放事件

	//初始化函数
	void initControl();									//初始化控件
	void initConnections();								//初始化信号槽的连接
	void loadStyleSheet(const QString& sheetName);		//加载样式表

signals:
	//标题栏信号
	void signalButtonMinClicked();
	void signalButtonRestoreClicked();
	void signalButtonMaxClicked();
	void signalButtonCloseClicked();

private slots:
	//标题栏槽函数
	void onButtonMinClicked();
	void onButtonRestoreClicked();
	void onButtonMaxClicked();
	void onButtonCloseClicked();

private:
	//成员变量
	QLabel* m_pIcon;				//标题栏图标
	QLabel* m_pTitleContent;		//标题栏内容
	QPushButton* m_pButtonMin;		//最小化按钮
	QPushButton* m_pButtonRestore;	//最大化还原按钮
	QPushButton* m_pButtonMax;		//最大化按钮
	QPushButton* m_pButtonClose;	//关闭按钮

	QString m_titleContent;				//标题栏内容
	ButtonType m_buttonType;			//标题栏按钮类型

	//最大化窗口还原变量（原窗口的位置大小）
	QPoint m_restorePos;
	QSize m_restoreSize;

	//移动窗口的变量
	bool m_isPressed;
	QPoint m_startMovePos;

};
