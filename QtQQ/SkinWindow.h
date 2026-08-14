#pragma once

#include "ui_SkinWindow.h"
#include "basicwindow.h"


class SkinWindow : public BasicWindow
{
	Q_OBJECT

public:
	SkinWindow(QWidget *parent = nullptr);
	~SkinWindow();

public:
	void initControl();

public slots:
	void onShowClose();
	void onShowMin();

private:
	Ui::SkinWindow ui;
};

