#include "TalkWindowItem.h"
#include "CommonUtils.h"



TalkWindowItem::TalkWindowItem(QWidget *parent)
	: QWidget(parent)
{
	ui.setupUi(this);
	this->initControl();
}

TalkWindowItem::~TalkWindowItem()
{}

void TalkWindowItem::setHeadPixmap(const QPixmap& pixmap) {
	QPixmap mask = QPixmap(":/Resources/MainWindow/head_mask.png");
	const QPixmap& headpixmap = CommonUtils::getRoundImage(pixmap, mask, ui.headlabel->size());
	ui.headlabel->setPixmap(headpixmap);
}

void TalkWindowItem::setMsgLabelContent(const QString& msg) {
	ui.msgLabel->setText(msg);
}

QString TalkWindowItem::getMsgLabelText() {
	return ui.msgLabel->text();
}

void TalkWindowItem::initControl() {
	ui.tclosebtn->setVisible(false);
	connect(ui.tclosebtn, &QPushButton::clicked, this, &TalkWindowItem::signalCloseClicked);
}


//事件重写
void TalkWindowItem::enterEvent(QEnterEvent* event) {
	ui.tclosebtn->setVisible(true);
	QWidget::enterEvent(event);
}

void TalkWindowItem::leaveEvent(QEvent* event) {
	ui.tclosebtn->setVisible(false);
	QWidget::leaveEvent(event);
}

void TalkWindowItem::resizeEvent(QResizeEvent* event) {
	QWidget::resizeEvent(event);
}


