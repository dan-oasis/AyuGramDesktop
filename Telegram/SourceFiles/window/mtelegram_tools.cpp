#include "window/mtelegram_tools.h"

#include "apiwrap.h"
#include "base/options.h"
#include "boxes/connection_box.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/abstract_box.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/sender.h"
#include "mainwidget.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QMimeData>
#include <QtCore/QSet>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

#include <limits>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <limits>

namespace MTelegram {
namespace {

using BoxPointer = not_null<Ui::GenericBox*>;
using Controller = not_null<Window::SessionController*>;

constexpr auto kMaxEntries = 40;
constexpr auto kMaxText = 12000;
constexpr auto kProxyListLimit = 256 * 1024;

base::options::toggle FastAnswers({
	.id = kFastAnswers,
	.name = "Fast Answers",
	.description = "Find and insert local reply templates from the chat menu.",
});
base::options::toggle FavoriteChats({
	.id = kFavoriteChats,
	.name = "Favorite chats",
	.description = "Pin, open and preview favorite chats from a chat menu.",
});
base::options::toggle Subfolders({
	.id = kSubfolders,
	.name = "Chat subfolders",
	.description = "Group folders with names such as Work/Project into a nested menu.",
});
base::options::toggle ProxySubscriptions({
	.id = kProxySubscriptions,
	.name = "Proxy subscriptions",
	.description = "Import proxy links from a saved HTTPS source.",
});
base::options::toggle FormattedCopy({
	.id = kFormattedCopy,
	.name = "Formatted copy",
	.description = "Include authors, dates, media labels and reply quotes when copying selected messages.",
});
base::options::toggle AdvancedSearch({
	.id = kAdvancedSearch,
	.name = "Advanced search",
	.description = "Filter search results by date range, sender and media type.",
});
base::options::toggle JumpCommands({
	.id = kJumpCommands,
	.name = "Quick shortcuts",
	.description = "Open Telegram links or settings from a list of named shortcuts.",
});
base::options::toggle ModerationNotes({
	.id = kModerationNotes,
	.name = "Moderation notes",
	.description = "Keep local tags and notes for a chat and access its management controls.",
});
base::options::toggle QuickTranslate({
	.id = kQuickTranslate,
	.name = "Quick translation",
	.description = "Expose built-in Telegram translation in the message menu.",
});

std::string PrefKey(Controller controller, const QString &name) {
	return u"mtelegram/%1/%2"_q
		.arg(controller->session().uniqueId())
		.arg(name)
		.toStdString();
}

QString ReadText(Controller controller, const QString &name) {
	return QString::fromUtf8(
		Core::App().settings().readPref<QByteArray>(PrefKey(controller, name)));
}

void SaveText(Controller controller, const QString &name, const QString &value) {
	Core::App().settings().writePref<QByteArray>(
		PrefKey(controller, name),
		value.toUtf8());
	Core::App().saveSettingsDelayed();
}

not_null<Ui::InputField*> AddInput(
		BoxPointer box,
		QString label,
		QString value = {},
		Ui::InputField::Mode mode = Ui::InputField::Mode::SingleLine) {
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		mode,
		rpl::single(std::move(label)),
		std::move(value)),
		st::boxRowPadding);
	field->setMaxLength(kMaxText);
	return field;
}

void AddChoice(
		not_null<Ui::VerticalLayout*> layout,
		QString label,
		Fn<void()> callback) {
	const auto button = layout->add(object_ptr<Ui::RoundButton>(
		layout,
		rpl::single(std::move(label)),
		st::defaultBoxButton),
		st::boxRowPadding,
		style::al_left);
	button->setClickedCallback(std::move(callback));
}

void EditText(
		Controller controller,
		QString title,
		QString placeholder,
		QString value,
		Fn<void(QString)> save) {
	controller->show(Box([=](BoxPointer box) {
		box->setTitle(rpl::single(title));
		const auto field = AddInput(
			box, placeholder, value, Ui::InputField::Mode::MultiLine);
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addButton(tr::lng_settings_save(), [=] {
			save(field->getLastText().trimmed());
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowEntries(
		Controller controller,
		QString title,
		QString key,
		QString placeholder,
		Fn<void(QString)> open) {
	controller->show(Box([=](BoxPointer box) {
		box->setTitle(rpl::single(title));
		const auto search = AddInput(box, tr::ayu_FilterEntries(tr::now));
		const auto list = box->addRow(object_ptr<Ui::VerticalLayout>(box));
		const auto text = ReadText(controller, key);
		const auto render = [=] {
			while (list->count()) {
				delete list->widgetAt(0);
			}
			const auto query = search->getLastText().trimmed();
			auto count = 0;
			for (const auto &line : text.split('\n', Qt::SkipEmptyParts)) {
				const auto separator = line.indexOf('=');
				if (separator <= 0
					|| (!query.isEmpty()
						&& !line.contains(query, Qt::CaseInsensitive))) {
					continue;
				}
				const auto name = line.left(separator).trimmed();
				const auto value = line.mid(separator + 1).trimmed();
				if (name.isEmpty() || value.isEmpty()) {
					continue;
				}
				AddChoice(list, name, [=] {
					box->closeBox();
					open(value);
				});
				if (++count == kMaxEntries) {
					break;
				}
			}
			list->resizeToWidth(list->width());
		};
		search->changes() | rpl::on_next(render, search->lifetime());
		render();
		box->setFocusCallback([=] { search->setFocusFast(); });
		box->addButton(tr::ayu_EditEntries(), [=] {
			box->closeBox();
			EditText(controller, title, placeholder, text, [=](QString updated) {
				SaveText(controller, key, updated);
			});
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowFavoriteChats(Controller controller) {
	controller->show(Box([=](BoxPointer box) {
		box->setTitle(tr::ayu_FavoriteChats());
		auto count = 0;
		for (const auto &id : ReadText(controller, u"favorite-chats"_q).split(
				',', Qt::SkipEmptyParts)) {
			const auto peer = controller->session().data().peerLoaded(
				PeerId(id.toULongLong()));
			if (!peer) {
				continue;
			}
			++count;
			AddChoice(box->verticalLayout(), peer->name(), [=] {
				box->closeBox();
				controller->showPeerHistory(peer);
			});
			AddChoice(box->verticalLayout(), tr::ayu_PreviewChat(
				tr::now, lt_item, peer->name()), [=] {
				box->closeBox();
				controller->showChatPreview({
					peer->owner().history(peer),
					FullMsgId(),
				});
			});
		}
		if (!count) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				tr::ayu_FavoriteChatsEmpty(),
				st::boxLabel),
				st::boxRowPadding);
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowFolderGroups(Controller controller, QString group = {}) {
	controller->show(Box([=](BoxPointer box) {
		box->setTitle(rpl::single(group.isEmpty()
			? tr::ayu_ChatFolders(tr::now)
			: group));
		auto groups = QSet<QString>();
		for (const auto &filter : controller->session().data().chatsFilters().list()) {
			const auto title = filter.titleText().text;
			if (!filter.id() || title.isEmpty()) {
				continue;
			}
			const auto prefix = group.isEmpty() ? QString() : group + '/';
			if (!title.startsWith(prefix) || title.size() <= prefix.size()) {
				continue;
			}
			const auto remaining = title.mid(prefix.size());
			const auto separator = remaining.indexOf('/');
			if (separator > 0) {
				const auto child = remaining.left(separator);
				if (!groups.contains(child)) {
					groups.insert(child);
					AddChoice(box->verticalLayout(), child + '/', [=] {
						box->closeBox();
						ShowFolderGroups(controller, prefix + child);
					});
				}
			} else {
				const auto id = filter.id();
				AddChoice(box->verticalLayout(), remaining, [=] {
					box->closeBox();
					controller->setActiveChatsFilter(id);
				});
			}
		}
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowProxySubscription(Controller controller) {
	const auto key = u"proxy-subscription"_q;
	controller->show(Box([=](BoxPointer box) {
		box->setTitle(tr::ayu_ProxySubscription());
		const auto field = AddInput(
			box, tr::ayu_ProxySourcePlaceholder(tr::now), ReadText(controller, key));
		const auto protocol = AddInput(box, tr::ayu_ProxyProtocolPlaceholder(tr::now));
		const auto wantedType = [=]() -> std::optional<MTP::ProxyData::Type> {
			using Type = MTP::ProxyData::Type;
			const auto text = protocol->getLastText().trimmed().toLower();
			if (text.isEmpty()) {
				return Type::None;
			} else if (text == u"mtproto"_q) {
				return Type::Mtproto;
			} else if (text == u"webproxy"_q) {
				return Type::Web;
			} else if (text == u"socks"_q) {
				return Type::Socks5;
			}
			protocol->showError();
			return std::nullopt;
		};
		const auto manager = new QNetworkAccessManager(box.get());
		const auto feedback = box->addRow(object_ptr<Ui::FlatLabel>(
			box, rpl::single(QString()), st::boxLabel), st::boxRowPadding);
		box->addButton(tr::ayu_FiltersImportAction(), [=] {
			const auto url = QUrl(field->getLastText().trimmed(), QUrl::StrictMode);
			const auto type = wantedType();
			if (!type) {
				return;
			}
			if (url.scheme() != u"https"_q
				|| url.host().isEmpty()
				|| !url.userInfo().isEmpty()) {
				field->showError();
				return;
			}
			if (!manager->findChildren<QNetworkReply*>().isEmpty()) {
				return;
			}
			auto request = QNetworkRequest(url);
			request.setAttribute(
				QNetworkRequest::RedirectPolicyAttribute,
				QNetworkRequest::NoLessSafeRedirectPolicy);
			request.setTransferTimeout(15000);
			const auto reply = manager->get(request);
			reply->setReadBufferSize(kProxyListLimit + 1);
			const auto data = std::make_shared<QByteArray>();
			QObject::connect(reply, &QNetworkReply::readyRead, box.get(), [=] {
				data->append(reply->read(kProxyListLimit + 1 - data->size()));
				if (data->size() > kProxyListLimit) {
					reply->abort();
				}
			});
			QObject::connect(reply, &QNetworkReply::finished, box.get(), [=] {
				data->append(reply->read(kProxyListLimit + 1 - data->size()));
				reply->deleteLater();
				if (reply->error() != QNetworkReply::NoError
					|| data->size() > kProxyListLimit) {
					feedback->setText(tr::ayu_ProxyLoadFailed(tr::now));
					return;
				}
				static const auto links = QRegularExpression(
					u"(?:https://t\\.me/|tg://)(?:proxy|webproxy|socks)\\?[^\\s<>\"']+"_q,
					QRegularExpression::CaseInsensitiveOption);
				auto matches = links.globalMatch(QString::fromUtf8(*data));
				auto &settings = Core::App().settings().proxy();
				auto added = 0;
				for (auto i = 0; i != 200 && matches.hasNext(); ++i) {
					auto proxy = ProxiesBoxController::ProxyFromLink(
						matches.next().captured());
					if (proxy
						&& (*type == MTP::ProxyData::Type::None || proxy.type == *type)
						&& settings.indexInList(proxy) < 0) {
						settings.addToList(std::move(proxy));
						++added;
					}
				}
				SaveText(controller, key, url.toString());
				feedback->setText(tr::ayu_ProxiesAdded(
					tr::now, lt_item, QString::number(added)));
			});
		});
		box->addButton(tr::ayu_CopyProxyList(), [=] {
			const auto type = wantedType();
			if (!type) {
				return;
			}
			auto links = QStringList();
			for (const auto &proxy : Core::App().settings().proxy().list()) {
				if (*type == MTP::ProxyData::Type::None || proxy.type == *type) {
					const auto link = ProxiesBoxController::ProxyLink(
						&controller->session().account(), proxy);
					if (!link.isEmpty()) {
						links.push_back(link);
					}
				}
			}
			if (!links.isEmpty()) {
				QGuiApplication::clipboard()->setText(links.join('\n'));
			}
			feedback->setText(tr::ayu_ProxyLinksCopied(
				tr::now, lt_item, QString::number(links.size())));
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

void ShowAdvancedSearch(Controller controller, PeerData *peer) {
	controller->show(Box([=](BoxPointer box) {
		box->setTitle(tr::ayu_AdvancedSearch());
		const auto query = AddInput(box, tr::ayu_SearchTextPlaceholder(tr::now));
		const auto first = AddInput(box, tr::ayu_SearchFromDatePlaceholder(tr::now));
		const auto last = AddInput(box, tr::ayu_SearchToDatePlaceholder(tr::now));
		const auto media = AddInput(box, tr::ayu_SearchMediaPlaceholder(tr::now));
		const auto sender = peer
			? AddInput(box, tr::ayu_SearchSenderPlaceholder(tr::now)).get()
			: nullptr;
		const auto list = box->addRow(object_ptr<Ui::VerticalLayout>(box));
		const auto state = box->lifetime().make_state<MTP::Sender>(
			&controller->session().mtp());
		const auto active = box->lifetime().make_state<mtpRequestId>(0);
		box->setFocusCallback([=] { query->setFocusFast(); });
		box->addButton(tr::lng_dlg_filter(), [=] {
			if (*active) {
				return;
			}
			const auto date = [](Ui::InputField *field, bool end) -> std::optional<int> {
				const auto text = field->getLastText().trimmed();
				if (text.isEmpty()) {
					return 0;
				}
				const auto value = QDate::fromString(text, Qt::ISODate);
				const auto result = (end ? value.addDays(1) : value)
					.startOfDay().toSecsSinceEpoch();
				if (!value.isValid() || result <= 0 || result > std::numeric_limits<int>::max()) {
					field->showError();
					return std::nullopt;
				}
				return int(result);
			};
			const auto from = date(first, false);
			const auto to = date(last, true);
			if (!from || !to) {
				return;
			}
			if (*from && *to && *from >= *to) {
				last->showError();
				return;
			}
			const auto text = query->getLastText().trimmed();
			const auto kind = media->getLastText().trimmed().toLower();
			auto filter = MTP_inputMessagesFilterEmpty();
			if (kind == u"photo"_q || kind == u"фото"_q) {
				filter = MTP_inputMessagesFilterPhotos();
			} else if (kind == u"video"_q || kind == u"видео"_q) {
				filter = MTP_inputMessagesFilterVideo();
			} else if (kind == u"file"_q || kind == u"файл"_q) {
				filter = MTP_inputMessagesFilterDocument();
			} else if (kind == u"link"_q || kind == u"ссылка"_q) {
				filter = MTP_inputMessagesFilterUrl();
			} else if (!kind.isEmpty()) {
				media->showError();
				return;
			}
			auto fromPeer = (PeerData*)nullptr;
			if (sender) {
				auto name = sender->getLastText().trimmed();
				if (name.startsWith('@')) {
					name = name.mid(1);
				}
				if (!name.isEmpty()) {
					fromPeer = controller->session().data().peerByUsername(name);
					if (!fromPeer) {
						sender->showError();
						return;
					}
				}
			}
			if (text.isEmpty() && kind.isEmpty() && !fromPeer && !*from && !*to) {
				query->showError();
				return;
			}
			while (list->count()) {
				delete list->widgetAt(0);
			}
			const auto done = [=](const MTPmessages_Messages &result) {
				*active = 0;
				auto &data = controller->session().data();
				result.match([&](const MTPDmessages_messagesNotModified &) {
				}, [&](const auto &parsed) {
					data.processUsers(parsed.vusers());
					data.processChats(parsed.vchats());
					for (const auto &message : parsed.vmessages().v) {
						if (!DateFromMessage(message)
							|| !data.peerLoaded(PeerFromMessage(message))) {
							continue;
						}
						const auto item = data.addNewMessage(
							message, MessageFlags(), NewMessageType::Existing);
						const auto id = item->fullId();
						const auto label = item->originalText().text.simplified().left(90);
						AddChoice(list, label.isEmpty() ? item->history()->peer->name() : label, [=] {
							box->closeBox();
							controller->showPeerHistory(id.peer, Window::SectionShow::Way::Forward, id.msg);
						});
					}
				});
				if (!list->count()) {
					AddChoice(list, tr::lng_search_messages_none(tr::now), [] {});
				}
				list->resizeToWidth(list->width());
			};
			const auto fail = [=] {
				*active = 0;
				AddChoice(list, tr::ayu_SearchFailed(tr::now), [] {});
				list->resizeToWidth(list->width());
			};
			if (peer) {
				using Flag = MTPmessages_Search::Flag;
				*active = state->request(MTPmessages_Search(
					MTP_flags(fromPeer ? Flag::f_from_id : Flag()),
					peer->input(),
					MTP_string(text),
					(fromPeer ? fromPeer->input() : MTP_inputPeerEmpty()),
					MTP_inputPeerEmpty(),
					MTP_vector<MTPReaction>(),
					MTP_int(0),
					filter,
					MTP_int(*from),
					MTP_int(*to),
					MTP_int(0),
					MTP_int(0),
					MTP_int(kMaxEntries),
					MTP_int(0),
					MTP_int(0),
					MTP_long(0))).done(done).fail(fail).send();
			} else {
				*active = state->request(MTPmessages_SearchGlobal(
					MTP_flags(MTPmessages_SearchGlobal::Flag()),
					MTP_int(0),
					MTPInputChannel(),
					MTP_string(text),
					filter,
					MTP_int(*from),
					MTP_int(*to),
					MTP_int(0),
					MTP_inputPeerEmpty(),
					MTP_int(0),
					MTP_int(kMaxEntries))).done(done).fail(fail).send();
			}
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace

bool Enabled(const char *id) {
	return base::options::lookup<bool>(id).value();
}

namespace {

void FillChatTools(
		Controller controller,
		not_null<PeerData*> peer,
		Data::Thread *thread,
		const Ui::Menu::MenuCallback &addAction) {
	const auto append = [&](const QString &text, Fn<void()> handler) {
		addAction(text, std::move(handler), nullptr);
	};
	if (thread && Enabled(kFastAnswers)) {
		append(tr::ayu_FastAnswers(tr::now), [=, weak = base::make_weak(thread)] {
			ShowEntries(controller, tr::ayu_FastAnswers(tr::now), u"fast-answers"_q,
				tr::ayu_FastAnswersPlaceholder(tr::now), [=](QString text) {
					if (const auto target = weak.get()) {
						auto data = QMimeData();
						data.setText(std::move(text));
						controller->content()->textDrop(not_null{ target }, &data);
					}
				});
		});
	}
	if (Enabled(kFavoriteChats)) {
		const auto key = u"favorite-chats"_q;
		const auto id = QString::number(peer->id.value);
		const auto exists = ReadText(controller, key).split(',').contains(id);
		append(exists
			? tr::ayu_RemoveFavoriteChat(tr::now)
			: tr::ayu_AddFavoriteChat(tr::now), [=] {
			auto ids = ReadText(controller, key).split(',', Qt::SkipEmptyParts);
			if (!ids.removeAll(id) && ids.size() < kMaxEntries) {
				ids.push_front(id);
			}
			SaveText(controller, key, ids.join(','));
		});
		append(tr::ayu_FavoriteChats(tr::now), [=] { ShowFavoriteChats(controller); });
	}
	if (Enabled(kSubfolders)) {
		append(tr::ayu_ChatSubfolders(tr::now), [=] { ShowFolderGroups(controller); });
	}
	if (Enabled(kProxySubscriptions)) {
		append(tr::ayu_ProxySubscription(tr::now), [=] { ShowProxySubscription(controller); });
	}
	if (Enabled(kAdvancedSearch)) {
		append(tr::ayu_SearchThisChat(tr::now), [=] {
			ShowAdvancedSearch(controller, peer);
		});
		append(tr::ayu_SearchAllChats(tr::now), [=] {
			ShowAdvancedSearch(controller, nullptr);
		});
	}
	if (Enabled(kJumpCommands)) {
		append(tr::ayu_JumpTo(tr::now), [=] {
			ShowEntries(controller, tr::ayu_QuickShortcuts(tr::now), u"jump-commands"_q,
				tr::ayu_ShortcutsPlaceholder(tr::now), [=](QString text) {
					const auto url = QUrl(text, QUrl::StrictMode);
					if ((url.scheme() == u"https"_q && url.host() == u"t.me"_q)
						|| (url.scheme() == u"tg"_q && url.host() == u"settings"_q)) {
						UrlClickHandler::Open(text);
					} else {
						controller->showToast(tr::ayu_ShortcutInvalid(tr::now));
					}
				});
		});
	}
	if (Enabled(kModerationNotes) && (peer->isChat() || peer->isMegagroup())) {
		if (EditPeerInfoBox::Available(peer)) {
			append(tr::ayu_ManageChat(tr::now), [=] { controller->showEditPeerBox(peer); });
		}
		append(tr::ayu_ModerationNotes(tr::now), [=] {
			const auto key = u"moderation/%1"_q.arg(peer->id.value);
			EditText(controller, peer->name(), tr::ayu_ModerationNotesPlaceholder(tr::now),
				ReadText(controller, key), [=](QString text) {
					SaveText(controller, key, text);
				});
		});
	}
}

} // namespace

void AddChatTools(
		Controller controller,
		not_null<PeerData*> peer,
		Data::Thread *thread,
		const Ui::Menu::MenuCallback &addAction) {
	if ((!thread || !Enabled(kFastAnswers))
		&& !Enabled(kFavoriteChats)
		&& !Enabled(kSubfolders)
		&& !Enabled(kProxySubscriptions)
		&& !Enabled(kAdvancedSearch)
		&& !Enabled(kJumpCommands)
		&& (!Enabled(kModerationNotes) || (!peer->isChat() && !peer->isMegagroup()))) {
		return;
	}
	addAction(Ui::Menu::MenuCallback::Args{
		.text = tr::ayu_Tools(tr::now),
		.handler = nullptr,
		.icon = &st::menuIconManage,
		.fillSubmenu = [=](not_null<Ui::PopupMenu*> menu) {
			FillChatTools(
				controller, peer, thread, Ui::Menu::CreateAddActionCallback(menu));
		},
	});
}

} // namespace MTelegram
