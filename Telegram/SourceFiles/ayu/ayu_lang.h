// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtCore/QString>

class QJsonDocument;

class AyuLanguage {
public:
	static void init();

private:
	static void applyLanguageJson(QJsonDocument doc);
	static QString getCacheDir();
	static QString getCachePath(const QString &langId);
};
