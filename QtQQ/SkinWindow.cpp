#include "SkinWindow.h"
#include "QClickLabel.h"
#include "NotifyManager.h"



SkinWindow::SkinWindow(QWidget *parent)
	: BasicWindow(parent)
{
	ui.setupUi(this);
	this->setAttribute(Qt::WA_DeleteOnClose);
	this->loadStyleSheet("SkinWindow");
	this->initControl();
}

SkinWindow::~SkinWindow()
{}

void SkinWindow::initControl() {
	//创建调色板
	QList<QColor> colorList = {
		QColor(22, 154, 218), QColor(40, 138, 221), QColor(49, 166, 107), QColor(218, 67, 68),
		QColor(177, 99, 158), QColor(107, 81, 92), QColor(89, 92, 160), QColor(21, 156, 199),
		QColor(79, 169, 173), QColor(155, 183, 154), QColor(128, 77, 77), QColor(240, 188, 189)
	};
	for (int row = 0; row < 3; row++) {
		for (int clown = 0; clown < 4; clown++) {
			QClickLabel* label = new QClickLabel(this);
			connect(label, &QClickLabel::clicked, this, [row, clown, colorList]() {
				NotifyManager::getInstance().notifyOtherWindowChangeSkin(colorList.at(row * 4 + clown));
			});

			label->setCursor(Qt::PointingHandCursor);
			label->setFixedSize(84, 84);

			QPalette palette;		//设置调色板
			palette.setColor(QPalette::Window, colorList.at(row * 4 + clown));
			label->setAutoFillBackground(true);
			label->setPalette(palette);

			ui.gridLayout->addWidget(label, row, clown);
		}
	}

	//连接信号槽
	connect(ui.sysmin, &QPushButton::clicked, this, &SkinWindow::onShowMin);
	connect(ui.sysclose, &QPushButton::clicked, this, &SkinWindow::onShowClose);
	
}

void SkinWindow::onShowClose() {
	this->close();
}

void SkinWindow::onShowMin() {
	this->showMinimized();
}

