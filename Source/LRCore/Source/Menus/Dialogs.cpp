// The dialogs over a screen: opened with a message, handed their buttons, shown, opening and closing,
// and the questions and notices the screens ask with them.

#include "LRCore/Menus/Menus.h"

#include "Menus/MenuNames.h"

#include <algorithm>

namespace lr {

void Ui::openDialog(MenuDialog::Kind kind, int32_t message)
{
	if (dialogCount >= dialogs.size()) {
		return;
	}
	MenuDialog& d = dialogs[dialogCount++];
	d = {};
	d.kind = kind;
	d.message = message;
	makeRoot(d.tree);
	d.frame = make(d.tree, m.data->dialog, "dialfrme", "brickbox", 0);
	d.label = make(d.tree, m.data->dialog, "dialtext", "menufont", d.frame);
	d.tree[d.label].wrapping = true;
	setString(d.tree, d.label, message);
}

void Ui::giveButton(uint32_t binding, uint32_t style, int32_t textIndex)
{
	MenuDialog& d = dialogs[dialogCount - 1];
	WidgetTree& t = d.tree;
	const int16_t button = makeNamed(t, binding, style, d.frame, textIndex);
	if (button < 0) {
		return;
	}
	// Under the frame, in the dialog's icon tree (a binding that doesn't attach itself is attached here).
	if (t[button].parent < 0) {
		t.adopt(button, d.frame);
	}
	if (t[button].iconParent < 0) {
		t.attachIcon(button, 0);
	}
	d.buttons[d.buttons[0] < 0 ? 0 : 1] = button;
}

void Ui::showDialog(int32_t defaultButton)
{
	MenuDialog& d = dialogs[dialogCount - 1];
	WidgetTree& t = d.tree;
	Widget& label = t[d.label];
	const int32_t mw = label.rect.right - label.rect.left, mh = label.rect.bottom - label.rect.top;
	d.halfWidth = float(mw + 40) * 0.5f;
	d.halfHeight = float(mh + 40) * 0.5f;
	label.rect = {20, 20, mw + 20, mh + 20};
	int32_t top = label.rect.bottom + 20;
	for (int16_t b : d.buttons) {
		if (b < 0) {
			continue;
		}
		Widget& w = t[b];
		const int32_t bottom = w.rect.bottom, right = w.rect.right;
		d.halfHeight += float(bottom + 20) * 0.5f;
		w.rect = {25, top, right + 25, top + bottom};
		top = w.rect.bottom + 20;
		show(t, b, false);
	}
	show(t, d.label, false);
	if (defaultButton >= 0 && d.buttons[defaultButton] >= 0) {
		select(t, d.buttons[defaultButton], IconQuiet | IconSilent);
	}
}

void Ui::closeDialog()
{
	MenuDialog& d = dialogs[dialogCount - 1];
	if (d.phase != MenuDialog::Open) {
		return;
	}
	d.phase = MenuDialog::Closing;
	d.animMs = 0;
	show(d.tree, d.label, false);
	// Its buttons leave the trees.
	WidgetTree& t = d.tree;
	for (int16_t& b : d.buttons) {
		if (b < 0) {
			continue;
		}
		for (int16_t* link = &t[d.frame].first; *link >= 0; link = &t[*link].next) {
			if (*link == b) {
				*link = t[b].next;
				break;
			}
		}
		t[d.frame].last = -1;
		for (int16_t c = t[d.frame].first; c >= 0; c = t[c].next) {
			t[d.frame].last = c;
		}
		t[b].parent = -1;
		b = -1;
	}
	t[0].iconFirst = t[0].iconLast = t[0].selected = -1;
	if (t[0].focus >= 0) {
		t[0].focus = -1;
	}
}

void Ui::updateDialog(int32_t dt)
{
	MenuDialog& d = dialogs[dialogCount - 1];
	d.ageMs += dt;
	if (d.kind == MenuDialog::TimedNotice && d.ageMs > 5000 && d.phase == MenuDialog::Open) {
		d.result = MenuDialog::TimedOut;
		closeDialog();
	}
	float scale = 1;
	if (d.phase == MenuDialog::Opening || d.phase == MenuDialog::Closing) {
		d.animMs += dt;
		scale = float(d.animMs) * 0.0033333334f;
		const bool closing = d.phase == MenuDialog::Closing;
		if (d.animMs > 300) {
			if (closing) {
				d.phase = MenuDialog::Closed;
			}
			else {
				d.phase = MenuDialog::Open;
				show(d.tree, d.label, true);
				for (int16_t b : d.buttons) {
					show(d.tree, b, true);
				}
			}
			d.animMs = 0;
		}
		if (closing) {
			scale = 1 - scale;
		}
		scale = std::clamp(scale, 0.0f, 1.0f);
	}
	if (d.phase == MenuDialog::Closed) {
		dialogCount--;
		return;
	}
	const int32_t hw = int32_t(d.halfWidth * scale), hh = int32_t(d.halfHeight * scale);
	d.tree[d.frame].rect = {320 - hw, 240 - hh, 320 + hw, 240 + hh};
	layoutFrame(d.tree, d.frame);
}

void Ui::ask(int32_t message, int32_t yes, int32_t no)
{
	openDialog(MenuDialog::YesNo, message);
	giveButton(NameDialog, StyleCheck, yes);
	giveButton(NameDialog, StyleCancel, no);
	showDialog(1);
}

void Ui::tell(int32_t message)
{
	openDialog(MenuDialog::OneButton, message);
	giveButton(NameDialog, StyleCheck, TextOk);
	showDialog(0);
}

} // namespace lr
