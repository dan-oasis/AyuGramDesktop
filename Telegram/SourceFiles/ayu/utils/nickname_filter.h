#pragma once

#include <QtCore/QString>
#include <cstdint>

namespace Ayu {

[[nodiscard]] bool IsValidNicknamePattern(const QString &pattern);
[[nodiscard]] QString NicknameForDisplay(
	const QString &name,
	std::uint64_t userId = 0);

} // namespace Ayu
