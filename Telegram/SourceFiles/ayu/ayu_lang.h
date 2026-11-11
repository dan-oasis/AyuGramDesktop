// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtCore/QString>

class QJsonDocument;

namespace Lang {
class Instance;
} // namespace Lang

class AyuLanguage {
public:
	static void init();
	static void applyBuiltIn(Lang::Instance &language);

private:
	static void applyLanguageJson(QJsonDocument doc);
	static QString getCacheDir();
	static QString getCachePath(const QString &langId);

};
