#pragma once

#include <QObject>


//Meyers单例
class NotifyManager  : public QObject
{
	Q_OBJECT

public:
	static NotifyManager& getInstance();

	void notifyOtherWindowChangeSkin(const QColor& color);

private:
	NotifyManager();
	~NotifyManager();
	NotifyManager(const NotifyManager&) = delete;
	NotifyManager& operator=(const NotifyManager&) = delete;


signals:
	void signalSkinChanged(const QColor& color);

};

