// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/components/avatar_corners_preview.h"

#include "lang/lang_keys.h"
#include "styles/style_dialogs.h"
#include "styles/style_settings.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"

AvatarCornersPreview::AvatarCornersPreview(QWidget *parent)
: RpWidget(parent)
, _emptyUserpic(Ui::EmptyUserpic::UserpicColor(0), u"mTelegram"_q) {
	const auto &row = st::defaultDialogRow;
	setFixedHeight(row.height);
}

void AvatarCornersPreview::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto &row = st::defaultDialogRow;
	const auto photoSize = row.photoSize;
	const auto xShift = st::settingsButtonNoIcon.padding.left()
		- row.padding.left();
	const auto userpicX = row.padding.left() + xShift;
	const auto userpicY = (height() - photoSize) / 2;

	p.fillRect(rect(), st::windowBg);

	_emptyUserpic.paintCircle(p, userpicX, userpicY, width(), photoSize);

	const auto nameText = u"mTelegram"_q;
	p.setPen(st::dialogsNameFg);
	p.setFont(st::semiboldFont);
	p.drawText(row.nameLeft + xShift, row.nameTop + st::semiboldFont->ascent, nameText);

	p.setPen(st::dialogsTextFg);
	p.setFont(st::dialogsTextFont);
	p.drawText(row.textLeft + xShift, row.textTop + st::dialogsTextFont->ascent, tr::ayu_AvatarPreviewText(tr::now));
}
