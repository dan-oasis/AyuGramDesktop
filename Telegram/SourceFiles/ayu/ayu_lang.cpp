// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_lang.h"

#include "qjsondocument.h"
#include "lang/lang_instance.h"
#include "settings.h"

#include <QFile>
#include <QRegularExpression>

// hard-coded languages
std::map<QString, QString> langMapping = {
	{"pt-br", "pt"},
	{"zh-hans-beta", "zh-hans"},
	{"zh-hant-beta", "zh-hant"},
	{"zh-hans-raw", "zh-hans"},
	{"zh-hant-raw", "zh-hant"},
};

constexpr auto postfixes = {
	"zero",
	"one",
	"two",
	"few",
	"many",
	"other"
};

QString AyuLanguage::getCacheDir() {
	return cWorkingDir() + u"tdata/ayu/languages/"_q;
}

QString AyuLanguage::getCachePath(const QString &langId) {
	return getCacheDir() + langId + u".json"_q;
}

void AyuLanguage::init() {
	const auto builtInGuard = gsl::finally([] {
		applyBuiltIn(Lang::GetInstance());
	});
	const auto langPackId = Lang::GetInstance().id();
	const auto langPackBaseId = Lang::GetInstance().baseId();
	auto finalLangPackId = langMapping.contains(langPackId) ? langMapping[langPackId] : langPackId;

	if (finalLangPackId.isEmpty()) {
		finalLangPackId = langPackBaseId;
	}
	if (finalLangPackId.isEmpty()) {
		return;
	}

	const auto cachePath = getCachePath(finalLangPackId);
	QFile file(cachePath);
	if (!file.exists()) {
		const auto basePath = getCachePath(langPackBaseId);
		if (!QFile::exists(basePath)) {
			return;
		}
		file.setFileName(basePath);
	}

	if (file.open(QIODevice::ReadOnly)) {
		const auto data = file.readAll();
		file.close();

		QJsonParseError error{};
		const auto doc = QJsonDocument::fromJson(data, &error);
		if (error.error == QJsonParseError::NoError) {
			LOG(("Loading cached mTelegram language: %1").arg(finalLangPackId));
			applyLanguageJson(doc);
		}
	}
}

void AyuLanguage::applyBuiltIn(Lang::Instance &language) {
	if (language.id() != u"ru"_q && language.baseId() != u"ru"_q) {
		return;
	}
	static const auto translations = [] {
		auto file = QFile(u":/gui/langs/mtelegram_ru.json"_q);
		return file.open(QIODevice::ReadOnly)
			? QJsonDocument::fromJson(file.readAll()).object()
			: QJsonObject();
	}();
	for (auto i = translations.begin(); i != translations.end(); ++i) {
		language.applyValue(
			(u"ayu_"_q + i.key()).toUtf8(), i.value().toString().toUtf8());
	}
}

void AyuLanguage::applyLanguageJson(QJsonDocument doc) {
	static const auto legacyBrand = QRegularExpression(
		u"(?:ayu[ -]?gram|radolyn)"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto legacyPromotion = QRegularExpression(
		u"(?:extera[ -]?gram|zasto[ -]?gram|boosty\\.to|patreon\\.com|"
		"buymeacoffee\\.com|ko-fi\\.com|donationalerts\\.com)"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto json = doc.object();
	for (const QString &brokenKey : json.keys()) {
		auto key = qsl("ayu_") + brokenKey;
		auto val = json.value(brokenKey).toString().replace(qsl("&amp;"), qsl("&"));

		if (key.endsWith("_Android")
			|| val.contains(legacyPromotion)
			|| (val.contains(legacyBrand)
				&& (val.contains(u"https://"_q)
					|| val.contains(u"tg://"_q)
					|| val.contains('@')))
			|| key == u"ayu_SettingsWatermark"_q) {
			continue;
		}

		for (const auto &postfix : postfixes) {
			if (key.endsWith(qsl("_") + postfix)) {
				key = key.replace(qsl("_") + postfix, qsl("#") + postfix);
				break;
			}
		}

		if (key.endsWith("_PC")) {
			key = key.replace("_PC", "");
		}

		if (val.contains(qsl("%1$d")) && !val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{count}"));
		} else if (val.contains(qsl("%1$d")) && val.contains(qsl("%2$d"))) {
			val = val.replace(qsl("%1$d"), qsl("{count1}")).replace(qsl("%2$d"), qsl("{count2}"));
		} else if (val.contains(qsl("%1$s")) && !val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item}"));
		} else if (val.contains(qsl("%1$s")) && val.contains(qsl("%2$s"))) {
			val = val.replace(qsl("%1$s"), qsl("{item1}")).replace(qsl("%2$s"), qsl("{item2}"));
		}

		val.replace(legacyBrand, u"mTelegram"_q);
		Lang::GetInstance().resetValue(key.toUtf8());
		Lang::GetInstance().applyValue(key.toUtf8(), val.toUtf8());
	}
	Lang::GetInstance().updatePluralRules();
}
