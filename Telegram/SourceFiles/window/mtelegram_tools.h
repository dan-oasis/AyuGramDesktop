#pragma once

#include "ui/widgets/menu/menu_add_action_callback.h"

class PeerData;

namespace Data {
class Thread;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace MTelegram {

inline constexpr auto kFastAnswers = "mtelegram-fast-answers";
inline constexpr auto kFavoriteChats = "mtelegram-favorite-chats";
inline constexpr auto kSubfolders = "mtelegram-subfolders";
inline constexpr auto kProxySubscriptions = "mtelegram-proxy-subscriptions";
inline constexpr auto kFormattedCopy = "mtelegram-formatted-copy";
inline constexpr auto kAdvancedSearch = "mtelegram-advanced-search";
inline constexpr auto kJumpCommands = "mtelegram-jump-commands";
inline constexpr auto kModerationNotes = "mtelegram-moderation-notes";
inline constexpr auto kQuickTranslate = "mtelegram-quick-translate";

[[nodiscard]] bool Enabled(const char *id);

void AddChatTools(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	Data::Thread *thread,
	const Ui::Menu::MenuCallback &addAction);

} // namespace MTelegram
