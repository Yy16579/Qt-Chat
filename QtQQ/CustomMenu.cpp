#include "CustomMenu.h"
#include "CommonUtils.h"


CustomMenu::CustomMenu(QWidget *parent)
	: QMenu(parent)
{
	this->setAttribute(Qt::WA_TranslucentBackground);
	CommonUtils::loadStyleSheet(this, "Menu");
}

CustomMenu::~CustomMenu()
{}

void CustomMenu::addCustomMenu(const QString& text, const QString& icon, const QString& name) {
	QAction* pAction = this->addAction(QIcon(icon), name);
	this->m_menuActionMap.insert(text, pAction);
}

QAction* CustomMenu::getAction(const QString& text) {
	return this->m_menuActionMap[text];
}

