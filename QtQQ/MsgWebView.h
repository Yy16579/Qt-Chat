#pragma once

#include <QWebEngineView>
#include <QDomNode>
#include <QDomText>
#include <QURL>


//============================================= MsgHtmlObj（数据管家）=================================================
// 存储两个 HTML 模板（左气泡/右气泡）
// 通过 QWebChannel 暴露给 JS 访问
class MsgHtmlObj : public QObject
{
	Q_OBJECT
	// Q_PROPERTY 是 Qt 的元对象系统宏，把 C++ 成员变量暴露给 JS
	// MEMBER 表示这是成员变量，JS 可以直接读写
	// NOTIFY 表示变量变化时发这个信号
	Q_PROPERTY(QString msgLHtmlTmpl MEMBER m_msgLHtmlTmpl NOTIFY signalMsgHtml)		
	Q_PROPERTY(QString msgRHtmlTmpl MEMBER m_msgRHtmlTmpl NOTIFY signalMsgHtml)		
		//核心概念：Q_PROPERTY 这是 Qt 让 C++ 对象的属性能被 JavaScript 访问的"魔法"。打个比方：
		//	- C++ 是后台仓库， m_msgRHtmlTmpl 是仓库里的一箱货
		//	- Q_PROPERTY 相当于在仓库门口贴了标签："这里有一箱货叫 msgRHtmlTmpl，可以拿"
		//	- JS 是前台，通过 external0.msgRHtmlTmpl 就能"取货"
public:
	MsgHtmlObj(QObject* parent, QString msgLPicPath = "");

private:
	void initHtmlTmpl();	// 初始化聊天网页，初始化时加载两个 HTML 模板文件
	QString getMsgtmplHtml(const QString& code);

signals:
	//信号
	void signalMsgHtml(const QString& html);

private:
	//成员变量
	QString m_msgLHtmlTmpl;		//别人发来的信息
	QString m_msgRHtmlTmpl;		//我发的信息
	QString m_msgLPicPath;		//发信息来的人的头像路径

};



//=========================================== MsgWebPage（页面守卫）====================================================
// 拦截导航请求，只允许加载 qrc 资源
// 继承 QWebEnginePage，重写导航方法
class MsgWebPage :public QWebEnginePage
{
	Q_OBJECT

public:
	MsgWebPage(QObject* parent = nullptr);

protected:
	bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame);

};



//=========================================== MsgWebView（主控件）=====================================================
// MsgWebView 是一个"内嵌的浏览器" ，继承自 QWebEngineView （基于 Chromium）
// 它能加载 HTML 网页，执行 JavaScript，显示漂亮的聊天气泡
class MsgWebView  : public QWebEngineView			
{
	Q_OBJECT

public:
	MsgWebView(QWidget *parent = nullptr);
	~MsgWebView();

public:
	void appendMsg(const QString& html, const QString strObj = "0");	//发送消息(strObj="0")或接收消息(strObj=QQ号)

private:
	QList<QStringList> parseHtml(const QString& html);			// 解析 HTML
	QList<QStringList> parseDocNode(const QDomNode& node);		// 递归解析 DOM 节点

signals:
	void signalSendMsg(const QString& msg, int msgType, const QString file = "");

private:
	//成员变量
	MsgHtmlObj* m_msgHtmlObj;		// 数据管家对象
	QWebChannel* m_channel;			// 网络通道

};

