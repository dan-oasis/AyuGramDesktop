#include "ayu/utils/nickname_filter.h"

#include "ayu/ayu_settings.h"
#include "lang_auto.h"
#include "unicode/regex.h"

#include <memory>

namespace Ayu {
namespace {

const auto kStandardPattern = QString::fromUtf8(
	R"((?:(?![\u200C\u200D])[\p{Cc}\p{Cf}]|[\u034F\u115F\u1160\u2800\u3164\uFFA0]))"
	R"(|[\p{L}\p{N}][\u200C\u200D]|[\u200C\u200D][\p{L}\p{N}])"
	R"(|^[\u200C\u200D]|[\u200C\u200D]$)"
	R"(|^[\p{Z}\p{M}\p{Cf}\p{P}]*\p{L}?[\p{Z}\p{M}\p{Cf}\p{P}]*$)");

std::unique_ptr<icu::RegexPattern> CompilePattern(const QString &pattern) {
	if (pattern.isEmpty() || pattern.size() > 256) {
		return nullptr;
	}
	auto status = U_ZERO_ERROR;
	return std::unique_ptr<icu::RegexPattern>(icu::RegexPattern::compile(
		icu::UnicodeString(
			reinterpret_cast<const UChar*>(pattern.constData()),
			pattern.size()),
		0,
		status));
}

bool MatchesPattern(const QString &name, const QString &expression) {
	thread_local auto cachedExpression = QString();
	thread_local auto pattern = std::unique_ptr<icu::RegexPattern>();
	if (cachedExpression != expression) {
		cachedExpression = expression;
		pattern = CompilePattern(expression);
	}
	if (!pattern) {
		return false;
	}
	const auto text = icu::UnicodeString(
		reinterpret_cast<const UChar*>(name.constData()),
		name.size());
	auto status = U_ZERO_ERROR;
	const auto matcher = std::unique_ptr<icu::RegexMatcher>(
		pattern->matcher(text, status));
	if (!matcher || U_FAILURE(status)) {
		return false;
	}
	matcher->setTimeLimit(5, status);
	matcher->setStackLimit(64 * 1024, status);
	const auto matched = matcher->find(status);
	return U_SUCCESS(status) && matched;
}

} // namespace

bool IsValidNicknamePattern(const QString &pattern) {
	return CompilePattern(pattern) != nullptr;
}

QString NicknameForDisplay(const QString &name, std::uint64_t userId) {
	const auto &settings = AyuSettings::getInstance();
	if (!settings.nicknameReplacement()) {
		return name;
	}
	const auto &pattern = settings.nicknameCustomFilter()
		? settings.nicknameFilterPattern()
		: kStandardPattern;
	if (!MatchesPattern(name, pattern)) {
		return name;
	}
	return userId
		? u"👤 %1"_q.arg(userId)
		: tr::ayu_NicknameHiddenId(tr::now);
}

} // namespace Ayu
