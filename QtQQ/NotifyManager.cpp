#include "NotifyManager.h"
#include "CommonUtils.h"


NotifyManager::NotifyManager()
	:QObject(nullptr)
{}

NotifyManager::~NotifyManager()
{}

NotifyManager& NotifyManager::getInstance() {
	static NotifyManager instance;
	return instance;
}

void NotifyManager::notifyOtherWindowChangeSkin(const QColor& color) {
	emit this->signalSkinChanged(color);
	CommonUtils::setDefaultSkinColor(color);
}

