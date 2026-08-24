#include "MsgWebView.h"
#include "WindowManager.h"
#include "ContactBook.h"

#include <QFile>
#include <QMessageBox>
#include <QJsonObject>
#include <QJsonDocument>
#include <QWebChannel>
#include <QPixmap>
#include <QBuffer>



//=============================================== MsgHtmlObj =====================================================
MsgHtmlObj::MsgHtmlObj(QObject* parent, QString msgLPicPath)
	: QObject(parent)
	, m_msgLPicPath(msgLPicPath)
{
	this->initHtmlTmpl();
}

void MsgHtmlObj::initHtmlTmpl()
{
	// 将磁盘图片路径转换为 base64 data URI
	// 原因：Chromium 从 qrc:// 页面加载 file:/// 资源时受同源策略限制，图片加载失败
	// 改用 data URI 把图片字节直接内联进 HTML，完全绕过文件系统和跨域检查
	auto toDataUri = [](const QString& path) -> QString {
		if (path.isEmpty()) {
			return QString();
		}
		QPixmap pix(path);
		if (pix.isNull()) {
			return QString();
		}
		QByteArray byteArray;
		QBuffer buffer(&byteArray);
		buffer.open(QIODevice::WriteOnly);
		pix.save(&buffer, "PNG");
		return QString("data:image/png;base64,%1").arg(QString::fromLatin1(byteArray.toBase64()));
	};

	// 初始化 左侧
	// 将msgleftTmpl中的【%1】，替换成 对方头像的 base64 data URI
	m_msgLHtmlTmpl = getMsgtmplHtml("msgleftTmpl");
	m_msgLHtmlTmpl.replace("%1", toDataUri(m_msgLPicPath));

	// 初始化 右侧
	// 通过 WindowManager 获取当前登录用户 empID，查 ContactBook 缓存拿头像路径
	m_msgRHtmlTmpl = getMsgtmplHtml("msgrightTmpl");
	int empID = WindowManager::getInstance().m_empID;
	QString msgRPicPath = ContactBook::getInstance().employeeInfo(empID).picture;
	// 同左侧，转成 base64 data URI
	m_msgRHtmlTmpl.replace("%1", toDataUri(msgRPicPath));

}

QString MsgHtmlObj::getMsgtmplHtml(const QString& code)
{
	// 将数据全部读取出来，然后再 返回
	QFile file(":/Resources/MainWindow/MsgHtml/" + code + ".html");

	//先检查 open 返回值，失败时弹窗提示并返回空串
	if (!file.open(QFile::ReadOnly)) {
		QMessageBox::information(nullptr, "Tips", "Failed to init html!");
		return QString("");
	}

	QString strData = QLatin1String(file.readAll());
	file.close();

	return strData;
}



//=========================================== MsgWebPage ==========================================================
MsgWebPage::MsgWebPage(QObject* parent)
	: QWebEnginePage(parent)
{}

bool MsgWebPage::acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) {
	// 只允许加载 qrc:/ 开头的资源（即 Qt 内嵌资源）
	// 拒绝任何外部网页跳转，防止用户被钓鱼或弹窗骚扰
	if (url.scheme() == QString("qrc")) {
		return true;
	}
	else {
		return false;
	}
}



//=========================================== MsgWebView ==========================================================
MsgWebView::MsgWebView(QWidget* parent)
	: QWebEngineView(parent)
	, m_channel(new QWebChannel(this))
{
	// 创建自定义页面，并设为当前 view 的页面
	MsgWebPage* page = new MsgWebPage(this);
	this->setPage(page);

	// 通道
	this->m_msgHtmlObj = new MsgHtmlObj(this);

	// 注册自己的消息模板对象（右侧气泡，显示"我"发出的消息）
	this->m_channel->registerObject("external0", m_msgHtmlObj);

	// 设置当前网页，网络通道
	this->page()->setWebChannel(m_channel);

	//注意：此处不 load 页面，延迟到 init() 中
	//必须先注册完所有消息发送者的模板对象（external_<uid>），页面加载时
	//QWebChannel 才能把它们全部暴露给 JS，否则 JS 侧取到 undefined
}

MsgWebView::~MsgWebView()
{}

void MsgWebView::init(int talkUid, bool isGroupTalk) {
	//注册消息发送者对应的头像模板对象
	//JS 侧通过 external_<uid>.msgLHtmlTmpl 取"该发送者头像"的左侧气泡模板

	if (isGroupTalk == false) {
		//单聊：查 ContactBook 缓存拿对方头像路径，注册对方的头像模板
		QString picPath = ContactBook::getInstance().employeeInfo(talkUid).picture;
		this->m_channel->registerObject(QString("external_%1").arg(talkUid), new MsgHtmlObj(this, picPath));
	}
	else {
		//群聊：注册全部群成员的头像模板（收到谁的消息就用谁的模板显示）
		//公司群/部门群的成员过滤由 ContactBook::groupMembers 内部消化
		QList<int> members = ContactBook::getInstance().groupMembers(talkUid);
		for (int memberId : members) {
			QString picPath = ContactBook::getInstance().employeeInfo(memberId).picture;
			this->m_channel->registerObject(
				QString("external_%1").arg(memberId),
				new MsgHtmlObj(this, picPath));
		}
	}

	//所有对象注册完成，最后才加载页面
	this->load(QUrl("qrc:/Resources/MainWindow/MsgHtml/msgTmpl.html"));
}

void MsgWebView::appendMsg(const QString& html, const QString strObj) {
	// 1.入参 :  html (QString) = "<span>你好</span><img src=\"qrc:/Resources/MainWindow/emotion/23.png\"/>"

	int msgType = 1;			// 消息类型（0是表情，1是文本，2是文件）
	bool isImageMsg = false;	// 是否为图片
	int imageNum = 0;			// 表情数量
	QString msg;				// 消息内容

	// 2.拆解 :  msgList (QList<QStringList>) = [ ["text", "你好"], ["img", "qrc:/Resources/MainWindow/emotion/23.png"] ]
	const QList<QStringList> msgList = this->parseHtml(html);
			// 把传入的 html 字符串进行拆解, 拆解为“类型 + 内容”的结构化数据
			// -------------------------------------------------------------------------------
			// 例:  html = <span>你好</span><img src="qrc:/Resources/MainWindow/emotion/1.png"/>
			//		转化为 ------->
			//		msgList = [
			//						["text", "你好"],
			//						["img", "qrc:/Resources/MainWindow/emotion/1.png"]
			//			       ]
			// -------------------------------------------------------------------------------
			// 将 html ( QString 类型）  -------->   msgList ( QList<QStringList> 类型)

	// 2.5混合消息检测：wire 协议仅支持 纯文本/纯表情/文件 三种（对齐原项目）
	//     文字+表情混输时无法编码成合法数据包，直接拒绝（不显示、不发送）
	bool hasText = false;
	bool hasImg = false;
	for (int i = 0; i < msgList.size(); i++) {
		if (msgList.at(i).at(0) == "text") {
			hasText = true;
		}
		else if (msgList.at(i).at(0) == "img") {
			hasImg = true;
		}
	}
	if (hasText && hasImg) {
		QMessageBox::warning(this, QStringLiteral("提示"),
			QStringLiteral("仅支持发送纯文本或纯表情消息！"));
		return;
	}

	//空消息守卫：解析结果为空（无文本无表情）时不显示空气泡、不发送
	if (msgList.isEmpty()) {
		return;
	}

	// 3.拼接 :  qsMsg (QString) = "你好<img src=\\"...\\" width=\\"24\\" height=\\"24\\"/>"
	QString qsMsg;		//最终渲染用的完整 HTML
	bool firstText = true;		//是否为第一段文本（用于多段文本间补 <br> 换行）
	for (int i = 0; i < msgList.size(); i++)		//遍历链表，取出消息内容
	{
		//消息类型为表情 img
		if (msgList.at(i).at(0) == "img")
		{
			msgType = 0;
			isImageMsg = true;
			imageNum++;

			//提取表情路径
			QString imagePath = msgList.at(i).at(1);

			// 提取表情编号（去掉路径前缀和.png后缀），补零到3位
			QString strEmotionName = imagePath.mid(QString("qrc:/Resources/MainWindow/emotion/").size());
			strEmotionName.replace(".png", "");
			msg += strEmotionName.rightJustified(3, '0');

			// 加载表情图片获取尺寸，qrc路径需去掉"qrc"前缀
			QPixmap pixmap;
			pixmap.load(imagePath.startsWith("qrc") ? imagePath.mid(3) : imagePath);

			// 拼接显示用的 img 标签
			qsMsg += QString("<img src=\"%1\" width=\"%2\" height=\"%3\"/>")
				.arg(imagePath).arg(pixmap.width()).arg(pixmap.height());
		}
		//消息类型为文本 text
		else if (msgList.at(i).at(0) == "text")
		{
			//HTML 转义：用户输入的 < > & " 等转为实体，防止气泡页把它们当标签解析（错乱/注入）
			//wire 传输的就是转义后文本，接收端直接嵌入模板即安全
			QString text = msgList.at(i).at(1).toHtmlEscaped();

			//多段文本（多行输入被解析为多段）之间补 <br>，保留换行显示
			if (firstText) {
				firstText = false;
			}
			else {
				text.prepend("<br>");
			}

			qsMsg += text;
			msg += text;
		}
	}

	// 4.封装 :  msgObj (QJsonObject) = {"MSG":"你好<img src=.../>"}
	QJsonObject msgObj;
	msgObj.insert("MSG", qsMsg);

	// 将消息重新拼装成网页能识别的格式
	// JS 函数操作网页 DOM，把消息显示到页面上
	const QString& Msg = QJsonDocument(msgObj).toJson(QJsonDocument::Compact);
	

	if (strObj == "0")
	{
		// strObj == "0"，为发送消息

		// 属于 Java脚本语言
		this->page()->runJavaScript(QString("appendHtml0(%1)").arg(Msg));

		// 如果发送的是表情，那就对 发送的数据，进行处理
		if (isImageMsg)
		{
			// 当前strData保存的只是表情的名称，占位是3个宽度
			// 这里加上表情的数量
			msg = QString::number(imageNum) + "images" + msg;
		}

		// 发送信号，发送信息
		emit this->signalSendMsg(msg, msgType);
	}
	else if (strObj == "-1")
	{
		// strObj == "-1"，为纯显示模式（重放自己发过的历史消息）
		//只显示右侧气泡，不 emit 发送信号（否则历史消息会被重新发到网络）
		this->page()->runJavaScript(QString("appendHtml0(%1)").arg(Msg));
	}
	else
	{
		// strObj == QQ号，为接收消息

		// 调用通用接收函数：objName 必须带 external_ 前缀，与 init() 注册的对象名对齐
		// JS 侧从 channelObjects[objName] 取该发送者的左侧气泡模板（头像即发送者的）
		this->page()->runJavaScript(QString("recvMsgHtml('external_%1',%2)").arg(strObj).arg(Msg));
	}
}

QList<QStringList> MsgWebView::parseHtml(const QString& html) {
	// 因为，传进来的是 html 文件，将它转换成 QT节点文件
	QDomDocument doc;
	doc.setContent(html);					// 转换

	// 想要解析的节点，就是 html里面的 body
	// 想拿到body，需要先获取元素
	// 节点元素
	const QDomElement& root = doc.documentElement();
	// 获取 第一个元素，node 是节点类型，还需要对节点进行 解析
	const QDomNode& node = root.firstChildElement("body");

	return parseDocNode(node);
}

QList<QStringList> MsgWebView::parseDocNode(const QDomNode& node)
{
	// 需要 最终解析出来的，是 字符串链表
	QList<QStringList> attribute;
	// list 保存，返回所有子节点
	const QDomNodeList& list = node.childNodes();

	// list.count()，链表的长度
	for (int i = 0; i < list.count(); i++)
	{
		// 挨个获取，当前链表中的 节点
		const QDomNode& node = list.at(i);

		// 再对节点，进行解析，判断 是否为 元素
		if (node.isElement())
		{
			// toElement() 方法，直接转换成 元素
			const QDomElement& element = node.toElement();

			// 判断 元素名 是否为 图片
			if (element.tagName() == "img")
			{
				// 获取图片的值
				QStringList attributeList;
				// 拿到图片的路径，表情，人头像
				attributeList << "img" << element.attribute("src");
				attribute << attributeList;
			}
			else if (element.tagName() == "span")
			{
				//注意：span 不能整体取 text()——text() 会拼接全部后代文本且丢失 <br> 换行结构，
				//再叠加子节点递归就重复计数（多行消息显示两遍的根因）
				//正确做法：只递归子节点，文本由文本节点分支逐段提取，段间换行由 appendMsg 统一重建
				if (node.hasChildNodes())
				{
					attribute << parseDocNode(node);
				}
			}
			// 其他标签（如 p、div 等容器元素）若有子节点，也需递归解析
			else if (node.hasChildNodes())
			{
				// 再 继续解析节点，解析之后 再将结果 存到 attribute
				attribute << parseDocNode(node);
			}
		}
		else if (node.isText())		//处理文本节点（如 <p>你好<img/></p> 中的"你好"）
		{
			QString text = node.toText().data();
			//过滤纯空白文本（HTML 换行、缩进产生的空文本节点）
			if (!text.trimmed().isEmpty())
			{
				attribute << (QStringList() << "text" << text);
			}
		}
	}

	return attribute;
}


