#pragma once

#include <QWidget>
#include "ui_ContactItem.h"


//QTreeWidgetItem子项贴纸类
class ContactItem : public QWidget
{
	Q_OBJECT

public:
	ContactItem(QWidget *parent = nullptr);
	~ContactItem();

public:
	void setUserName(const QString& userName);
	void setSignName(const QString& signName);
	void setHeadPixmap(const QPixmap& headPix);
	QString getUserName() const;
	QSize getHeadLabelSize() const;

private:
	void initControl();

private:
	Ui::ContactItemClass ui;
};

