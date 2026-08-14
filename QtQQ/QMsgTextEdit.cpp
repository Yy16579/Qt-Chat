#include "QMsgTextEdit.h"



QMsgTextEdit::QMsgTextEdit(QWidget *parent)
	: QTextEdit(parent)
{}

QMsgTextEdit::~QMsgTextEdit()
{
	this->deleteAllEmotionImage();
}

void QMsgTextEdit::addEmotionUrl(int emotionNum) {
	// 1 :  拼出表情图片的资源路径  ( emotionNum 是表情编号（比如 23），路径变成 qrc:/Resources/MainWindow/emotion/23.png )
	const QString& imageName = QString("qrc:/Resources/MainWindow/emotion/%1.png").arg(emotionNum);
	const QString& flagName = QString("%1").arg(imageName);
	
	// 2 :  把 <img> 标签插入到消息编辑框中（光标当前位置），这样消息编辑框里就会出现表情图片
	this->insertHtml(QString("<img src = '%1' />").arg(flagName));
	
	// 3 :  将表情插入到 QList 中
	if (this->m_listEmotionUrl.contains(imageName)) {
		return;			// 如果之前插入过这个表情，直接返回，不再创建新的 QMovie
	}
	else {
		this->m_listEmotionUrl.append(imageName);
	}

	// 4 :  创建一个 QMovie 对象来播放 APNG 表情动画
	QMovie* apngMovie = new QMovie(imageName, "apng", this);

	// 5 :  添加 QMovie - 资源URL 的映射，后面更新帧时要用
	this->m_emotionMap.insert(apngMovie, flagName);
	
	// 6 :  连接信号槽，动画每播一帧，就执行 onEmotionImageFrameChange 槽函数
	connect(apngMovie, &QMovie::frameChanged, this, &QMsgTextEdit::onEmotionImageFrameChange);
	
	// 7 :  启动动画
	apngMovie->start();
	this->updateGeometry();
}

void QMsgTextEdit::deleteAllEmotionImage() {
	//迭代器遍历 emotionMap ，清除所有 QMovie 对象
	for (QMap<QMovie*, QString>::const_iterator it = m_emotionMap.constBegin(); it != m_emotionMap.constEnd(); it++) {
		it.key()->stop();		//先停止动画，防止析构时触发信号
		it.key()->deleteLater();
	}

	//清除所有映射
	this->m_emotionMap.clear();
}


//槽函数
void QMsgTextEdit::onEmotionImageFrameChange(int frame) {
	// sender() 返回发送信号的对象（就是某个 QMovie）
	QMovie* movie = qobject_cast<QMovie*>(sender());

	// 把"当前帧的图片"更新到文档的资源缓存里
	// 参数1：资源类型（图片）
	// 参数2：图片的 URL（就是之前存在 m_emotionMap 里的路径）
	// 参数3：当前帧的 QPixmap（一帧静态图）
	document()->addResource(QTextDocument::ImageResource, QUrl(this->m_emotionMap[movie]), movie->currentPixmap());
	
	// 类比 ：就像翻页动画书——QMovie 每翻一页（一帧），就告诉 QTextEdit"这张图片变了，你重新显示一下吧
}


