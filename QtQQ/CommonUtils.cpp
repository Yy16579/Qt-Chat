#include "CommonUtils.h"

#include <QPainter>
#include <QFile>
#include <QWidget>
#include <QApplication>
#include <QSettings>


QPixmap CommonUtils::getRoundImage(const QPixmap& src, QPixmap& mask, QSize maskSize) {
	if (maskSize == QSize(0, 0)) {
		maskSize = mask.size();
	}
	else {
		mask = mask.scaled(maskSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}
	
	QImage resultImage(maskSize, QImage::Format_ARGB32_Premultiplied);
	QPainter painter(&resultImage);
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.fillRect(resultImage.rect(), Qt::transparent);
	painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
	painter.drawPixmap(0, 0, mask);
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.drawPixmap(0, 0, src.scaled(maskSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	painter.end();

	return QPixmap::fromImage(resultImage);
}

void CommonUtils::loadStyleSheet(QWidget* widget, const QString& sheetName) {
	QFile file(":/Resources/QSS/" + sheetName + ".css");
	file.open(QFile::ReadOnly);
	if (file.isOpen()) {
		widget->setStyleSheet("");
		QString qsstyleSheet = QLatin1String(file.readAll());
		widget->setStyleSheet(qsstyleSheet);
	}
	file.close();
}

void CommonUtils::setDefaultSkinColor(const QColor& color) {
	//构造配置文件路径
	const QString path = QApplication::applicationDirPath() + "/tradeprintinfo.ini";
	QSettings settings(path, QSettings::IniFormat);
	//根据传入color，设置配置文件颜色值
	settings.setValue("DefaultSkin/red", color.red());
	settings.setValue("DefaultSkin/green", color.green());
	settings.setValue("DefaultSkin/blue", color.blue());
}

QColor CommonUtils::getDefaultSkinColor() {
	//构造配置文件路径
	const QString path = QApplication::applicationDirPath() + "/tradeprintinfo.ini";
	//如果文件不存在，写入默认颜色
	if (!QFile::exists(path)) {
		setDefaultSkinColor(QColor(22, 154, 218));
	}
	//从配置文件中读取颜色值
	QSettings settings(path, QSettings::IniFormat);
	QColor color;
	color.setRed(settings.value("DefaultSkin/red").toInt());
	color.setGreen(settings.value("DefaultSkin/green").toInt());
	color.setBlue(settings.value("DefaultSkin/blue").toInt());

	return color;
}

QString CommonUtils::getConfigPath() {
	return QApplication::applicationDirPath() + "/config.ini";
}
