#pragma once


#include <QTextEdit>
#include <QMovie>	// QMovie 是 Qt 用来播放 GIF/APNG 动画的类，为什么需要 QMovie？因为 QTextEdit 默认只能显示 静态图，要让它显示 动图，就得手动把每一帧的图片"喂"给它



class QMsgTextEdit  : public QTextEdit
{
	Q_OBJECT

public:
	QMsgTextEdit(QWidget *parent = nullptr);
	~QMsgTextEdit();

public:
	void addEmotionUrl(int emotionNum);		// 消息编辑框 插入一个表情
	void deleteAllEmotionImage();			// 清空所有表情资源

	bool hasEmotionImage() const;			// 编辑框内是否插有表情（供发送判空：纯表情消息无纯文本）

private slots:
	//槽函数
	void onEmotionImageFrameChange(int frame);		// 表情动画每播一帧时触发

private:
	//成员变量
	QList<QString> m_listEmotionUrl;		// 记录消息编辑框已插入的 所有的 表情
	QMap<QMovie*, QString> m_emotionMap;	// 表情QMovie - 资源URL 的映射


};

