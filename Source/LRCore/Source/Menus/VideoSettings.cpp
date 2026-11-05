#include "LRCore/Menus/VideoSettings.h"

namespace lr {

namespace {

// The menus' sounds: a row selected (hilight1), a value stepped (slider2), the screen left
// (backup, as the options' BACK).
constexpr int32_t SoundSelect = 14, SoundStep = 24, SoundClose = 2;

constexpr std::u16string_view Names[VideoSettings::Rows] = {u"RESOLUTION", u"WINDOW", u"VSYNC", u"FRAME CAP", u"QUALITY",
	u"RENDER SCALE", u"FRAME SMOOTHING"};
constexpr std::u16string_view ResolutionValues[] = {u"1280X960", u"1600X1200", u"1920X1080", u"2048X1536", u"2560X1440", u"3840X2160"};
constexpr std::u16string_view WindowValues[] = {u"WINDOWED", u"BORDERLESS", u"FULLSCREEN"};
constexpr std::u16string_view OnOff[] = {u"ON", u"OFF"};
constexpr std::u16string_view CapValues[] = {u"OFF", u"60 FPS", u"120 FPS", u"144 FPS", u"240 FPS"};
constexpr std::u16string_view QualityValues[] = {u"LOW", u"MEDIUM", u"HIGH", u"EPIC", u"CINEMATIC"};
constexpr std::u16string_view ScaleValues[] = {u"50 PERCENT", u"67 PERCENT", u"75 PERCENT", u"100 PERCENT"};

constexpr uint32_t White = 0xffffffff, Selected = 0xfff6e606, Unselected = 0xff766b0f; // (246, 230, 6), (118, 107, 15)

} // namespace

std::u16string_view VideoSettings::name(size_t row) { return row < Rows ? Names[row] : std::u16string_view(); }

std::span<const std::u16string_view> VideoSettings::values(size_t row)
{
	switch (row) {
	case Resolution: return ResolutionValues;
	case Window: return WindowValues;
	case FrameCap: return CapValues;
	case Quality: return QualityValues;
	case RenderScale: return ScaleValues;
	case VSync:
	case Smoothing: return OnOff;
	default: return {};
	}
}

std::u16string_view VideoSettings::shown(size_t r) const
{
	const std::span<const std::u16string_view> list = values(r);
	const int32_t v = value[r];
	return v >= 0 && size_t(v) < list.size() ? list[size_t(v)] : std::u16string_view(other[r]);
}

void VideoSettings::step(size_t r, bool forward)
{
	const int32_t n = int32_t(values(r).size());
	const int32_t v = value[r];
	if (v < 0 || v >= n) {
		value[r] = int8_t(forward ? 0 : n - 1);
	}
	else {
		value[r] = int8_t((v + (forward ? 1 : n - 1)) % n);
	}
	sounds.push_back(SoundStep);
}

int32_t VideoSettings::key(Key k, bool fromPad)
{
	pad = fromPad;
	switch (k) {
	case Up:
	case Down:
		row = uint8_t((row + (k == Down ? 1 : Rows - 1)) % Rows);
		sounds.push_back(SoundSelect);
		return -1;
	case Back:
	case Forward: step(row, k == Forward); return row;
	case Close:
		closed = true;
		sounds.push_back(SoundClose);
		return -1;
	}
	return -1;
}

int32_t VideoSettings::mouse(int32_t x, int32_t y, uint32_t pressed)
{
	const bool moved = x != mouseX || y != mouseY;
	mouseX = x, mouseY = y;
	const int32_t over = x >= Left && x < Right && y >= Top ? (y - Top) / Pitch : -1;
	if (over < 0 || over >= int32_t(Rows)) {
		return -1;
	}
	if (moved || pressed) {
		pad = false;
		if (over != row) {
			row = uint8_t(over);
			sounds.push_back(SoundSelect);
		}
	}
	if (pressed & 3) {
		step(row, (pressed & 1) != 0);
		return row;
	}
	return -1;
}

void VideoSettings::draw(const MenuData& data, size_t language, std::vector<MenuElement>& elements, std::vector<MenuGlyph>& glyphs) const
{
	// (The menus' own drawing, into a scratch front end's lists.)
	Menus m;
	m.data = &data;
	m.language = uint8_t(language);
	const int16_t backdrop = data.image("backdrp", ScreenOptions);
	if (backdrop >= 0) {
		const MenuData::ImageInfo& b = data.images[size_t(backdrop)];
		m.drawImage(backdrop, {0, 0, 640, 480}, {0, 0, int32_t(b.width), int32_t(b.height)}, nullptr, White);
	}
	const int16_t title = data.font(language, "fontmenu");
	const int16_t font = data.font(language, "font_ths");
	m.drawLine(title, u"VIDEO SETTINGS", 620 - m.lineWidth(title, u"VIDEO SETTINGS"), 14, White);
	for (size_t r = 0; r < Rows; r++) {
		const uint32_t colour = r == row ? Selected : Unselected;
		const int32_t y = Top + int32_t(r) * Pitch;
		m.drawLine(font, name(r), Left, y, colour);
		m.drawLine(font, shown(r), Right - m.lineWidth(font, shown(r)), y, colour);
	}
	const std::u16string_view change = pad ? u"D-PAD: CHANGE" : u"ARROWS: CHANGE", back = pad ? u"B: BACK" : u"ESC: BACK";
	m.drawLine(font, change, Left, 438, White);
	m.drawLine(font, back, Right - m.lineWidth(font, back), 438, White);
	elements = std::move(m.elements);
	glyphs = std::move(m.glyphs);
}

} // namespace lr
