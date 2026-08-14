#include "RootContactItem.h"

#include <QPainter>


RootContactItem::RootContactItem(bool hasArrow, QWidget* parent)
	: QLabel(parent)
	, m_rotation(0)
	, m_hasArrow(hasArrow)
{
	//设置标签属性
	this->setFixedHeight(32);
	this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	
	//设置动画属性
	this->m_animation = new QPropertyAnimation(this, "rotation");	//初始化属性动画，绑定到当前对象的"rotation"属性	
	this->m_animation->setDuration(50);			//设置动画总时长为50ms
	this->m_animation->setEasingCurve(QEasingCurve::InQuad);	// 设置缓动曲线：InQuad（先慢后快的加速曲线），让箭头旋转更自然，符合物理直觉
}

RootContactItem::~RootContactItem()
{}

void RootContactItem::setText(const QString& title) {
	this->m_titleText = title;
	this->update();
}

void RootContactItem::setExpanded(bool expand) {
	if (expand == true) {
		this->m_animation->setEndValue(90);
	}
	else if (expand == false) {
		this->m_animation->setEndValue(0);
	}

	this->m_animation->start();
}

int RootContactItem::rotation() {
	return this->m_rotation;
}

void RootContactItem::setRotation(int rotation) {
	this->m_rotation = rotation;
	this->update();
}

void RootContactItem::paintEvent(QPaintEvent* event) {
	QPainter painter(this);

	//开启文本抗锯齿，让文字边缘更平滑
	painter.setRenderHint(QPainter::TextAntialiasing, true);	

	//设置字体
	QFont font;		
	font.setPointSize(10);
	painter.setFont(font);

	//绘制标题文本
	painter.drawText(24, 0, width() - 24, height(), Qt::AlignLeft | Qt::AlignVCenter, m_titleText);
	
	//开启图片平滑变换，让箭头旋转后不模糊
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

	painter.save();			

	if (this->m_hasArrow) {
		QPixmap pixmap;
		pixmap.load(":/Resources/MainWindow/arrow.png");
		//创建与箭头图片同大小的临时透明画布
		//用于绘制旋转后的箭头，避免直接旋转原图片导致的变形
		QPixmap tmpPixmap(pixmap.size());
		tmpPixmap.fill(Qt::transparent);				
		QPainter p(&tmpPixmap);
		p.setRenderHint(QPainter::SmoothPixmapTransform, true);
		//将坐标原点平移到图片中心
		//这样旋转时箭头会围绕中心旋转，而不是左上角
		p.translate(pixmap.width()/2, pixmap.height()/2);
		//旋转指定角度
		p.rotate(m_rotation);
		//将箭头绘制回临时画布
		//因为坐标原点已经移到中心，所以需要向左上偏移半个图片大小
		p.drawPixmap(0 - pixmap.width()/2, 0-pixmap.height()/2, pixmap);
		//将旋转后的箭头绘制到主控件上
		//位置：x=6px（左对齐），y=(控件高度-箭头高度)/2（垂直居中）
		painter.drawPixmap(6, (height() - pixmap.height())/2, tmpPixmap);
		//恢复画家之前保存的状态
		painter.restore();
	}

	QLabel::paintEvent(event);
}

