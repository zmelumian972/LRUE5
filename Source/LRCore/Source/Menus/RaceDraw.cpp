// The race's 2D, drawn by the menus' rules: the loading screen, the HUDs, the standings, the
// opening and closing fades and the race's dialog.

#include "LRCore/Menus/Menus.h"
#include "LRCore/Race/World.h"

#include "Menus/Pen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lr {

namespace {

// A race time as m:ss:cc (mm:ss:cc from ten minutes, wrapping at an hour).
std::u16string raceTime(int32_t ms)
{
	const int32_t t = ms % 3600000;
	char text[16];
	std::snprintf(text, sizeof(text), "%d:%02d:%02d", t / 60000, t / 1000 % 60, t / 10 % 100);
	return std::u16string(text, text + std::char_traits<char>::length(text));
}

std::u16string number(int32_t n)
{
	const std::string s = std::to_string(n);
	return std::u16string(s.begin(), s.end());
}

} // namespace

void Menus::drawLoading(std::string_view track)
{
	const MenuData::Loading* l = data->loading(track);
	if (!l) {
		return;
	}
	// The screen as the race redraws it while it loads: the name centred, then as many dots as it has got through.
	static constexpr float Progress[27] = {0, 0.05f, 0.1f, 0.24f, 0.26f, 0.34f, 0.36f, 0.38f, 0.4f, 0.42f, 0.45f, 0.48f, 0.51f, 0.54f,
		0.56f, 0.6f, 0.7f, 0.75f, 0.77f, 0.8f, 0.84f, 0.86f, 0.88f, 0.9f, 0.93f, 0.97f, 1.0f};
	const MenuData::Language& lang = data->languages[language];
	const int16_t font = int16_t(lang.raceFonts);
	const std::u16string_view name = lang.game.at(size_t(l->screen.name));
	const MenuData::ImageInfo& tick = data->images[l->tick];
	for (float p : Progress) {
		pen().drawLine(font, name, (ScreenWidth - pen().lineWidth(font, name)) >> 1, 25, 0xffffffff);
		const int32_t dots = int32_t(float(l->screen.dots.size()) * p);
		for (int32_t i = 0; i < dots; i++) {
			const std::array<float, 2>& at = l->screen.dots[i];
			const int32_t cx = int32_t(at[0] * ScreenWidth), cy = int32_t(at[1] * ScreenHeight);
			pen().drawImage(l->tick, {cx - 8, cy - 8, cx + 8, cy + 8}, {0, 0, int32_t(tick.width), int32_t(tick.height)}, nullptr,
				0xffffffff);
		}
	}
}

namespace {

constexpr uint32_t HudWhite = 0xffffffff;

// The HUD's images by number (-1 past the list).
int16_t hudImage(const MenuData& data, size_t n) { return n < data.hudImages.size() ? data.hudImages[n] : int16_t(-1); }

// The gadget and banner scale: smaller in split screen.
float hudScale(const World& w) { return w.raceView.cameras.size() > 1 ? 0.8f : 1.0f; }

// The banner: the countdown's digits and GO growing, or FINISH, centred at x on the row y.
void drawHudBanner(const Pen& pen, const World& w, size_t p, int32_t cx, int32_t y)
{
	const Hud& h = w.raceView.huds[p];
	const MenuData::Language& lang = pen.data->languages[pen.language];
	const int16_t ignum = pen.data->raceFont(pen.language, "ignum");
	std::u16string text;
	float scale = hudScale(w);
	uint32_t colour = HudWhite;
	if (h.banner > 0) {
		const int32_t s = h.banner / 1000;
		colour = 0xffffff00;
		if (s < 3) {
			text = std::u16string(1, char16_t(u'3' - s));
			scale *= float(1000 * (s + 1) - h.banner) * 1.8f * 0.001f;
		}
		else if (s < 5) {
			text = lang.game.at(41);
			scale *= s == 3 ? 1.8f : float(5000 - h.banner) * 0.0018f;
		}
	}
	else {
		text = lang.game.at(40);
	}
	if (!text.empty()) {
		pen.drawScaled(ignum, text, cx - (int32_t(float(pen.lineWidth(ignum, text)) * scale) >> 1), y - (int32_t(64.0f * scale) >> 2),
			scale, scale, colour);
	}
}

// The power-up frame: a colour or a white brick held.
void drawPowerFrame(const Pen& pen, const World& w, size_t racer, MenuRect view)
{
	const Power& pw = w.racers.powers[racer];
	if (pw.held == NoColour && pw.whiteCount <= 0) {
		return;
	}
	const int32_t level = std::min<int32_t>(pw.whiteCount, 3);
	const int32_t fx = view.left + (view.width() >> 5), fy = view.bottom - 74;
	static constexpr int32_t Icons[5] = {-1, 2, 16, 20, 6};
	static constexpr uint32_t Tints[5] = {0xffffffff, 0xffff0000, 0xff5050ff, 0xff00ff00, 0xffffff00};
	const size_t colour = std::min<size_t>(pw.held, 4);
	pen.drawImage(hudImage(*pen.data, 11), {fx, fy, fx + 64, fy + 64}, {0, 0, 64, 64}, nullptr, HudWhite);
	pen.drawImage(hudImage(*pen.data, 12), {fx, fy, fx + 64, fy + 64}, {0, 0, 64, 64}, nullptr, Tints[colour]);
	if (Icons[colour] >= 0) {
		pen.drawImage(hudImage(*pen.data, size_t(Icons[colour] + level)), {fx + 15, fy + 22, fx + 47, fy + 54}, {0, 0, 32, 32}, nullptr,
			HudWhite);
	}
	if (level > 0) {
		pen.drawImage(hudImage(*pen.data, size_t(12 + level)), {fx + 24, fy, fx + 88, fy + 64}, {0, 0, 64, 64}, nullptr, HudWhite);
	}
}

// The speedometer: its dial, the needle (two triangles), the hub.
void drawSpeedometer(const Pen& pen, const World& w, size_t p, MenuRect view)
{
	const float ms = hudScale(w);
	const int32_t vr = view.right, vb = view.bottom;
	const int32_t wd = int32_t(128 * ms);
	pen.drawImage(hudImage(*pen.data, 0), {vr - wd - 2, vb - wd - 2, vr - 2, vb - 2}, {0, 0, 128, 128}, nullptr, HudWhite);
	const float hx = float(vr - ((52 * wd) >> 7) - 2), hy = float(vb - ((52 * wd) >> 7) - 2);
	const float theta = std::clamp(w.raceView.huds[p].speed * 18.0f, 0.0f, 3.4f) + 2.25f;
	const auto at = [&](float length, float angle) {
		return std::array<float, 4>{hx + length * std::cos(angle) * ms, hy + length * std::sin(angle) * ms, -1, -1};
	};
	const std::array<float, 4> tip = at(45, theta), left = at(7, theta + 2.0943952f), right = at(7, theta + 4.1887903f);
	const std::array<float, 4> hub = {hx, hy, -1, -1};
	pen.drawTriangle(-1, {right, tip, hub}, {0xff969696, 0xff969696, 0xffc8c8c8}, view);
	pen.drawTriangle(-1, {left, hub, tip}, {HudWhite, 0xffc8c8c8, HudWhite}, view);
	const int32_t size = int32_t(16 * ms);
	const int32_t dx = int32_t(hx) - (size >> 1), dy = int32_t(hy) - (size >> 1);
	pen.drawImage(hudImage(*pen.data, 1), {dx, dy, dx + size, dy + size}, {0, 0, 16, 16}, nullptr, HudWhite);
}

// A racer's dot on a map (red for player 2's AI rival and the time race's record ghost).
void drawMapDot(const Pen& pen, const World& w, int32_t x, int32_t y, bool red, MenuRect view)
{
	const int32_t row = w.raceView.cameras.size() > 1 ? 16 : 0;
	pen.drawImage(hudImage(*pen.data, 10), {x - 8, y - 8, x + 8, y + 8}, {red ? 16 : 0, row, (red ? 16 : 0) + 16, row + 16}, &view,
		HudWhite);
}

// The whole map, north up, in the viewport's corner: the others' dots, the ghosts' in a time race, this racer's arrow.
void drawFixedMap(const Pen& pen, const World& w, size_t p, const MenuData::Loading& track, MenuRect view)
{
	const size_t i = size_t(w.raceView.cameras[p].racer);
	const std::array<float, 4>& b = track.mapBounds; // min x, max y, max x, min y
	const float minX = b[0], maxY = b[1], maxX = b[2], minY = b[3];
	const bool mirrored = w.session.brief.mirrored;
	const int32_t ox = view.right - 15, oy = view.bottom - 15;
	const float g = 128.0f / std::max(maxX - minX, maxY - minY) * hudScale(w);
	const float ry = mirrored ? -maxY : minY;
	const auto toMap = [&](Vec3 at, int32_t& x, int32_t& y) {
		x = ox + int32_t((at.x - maxX) * g);
		y = oy + int32_t((ry - at.y) * g);
	};
	if (track.map >= 0) {
		const float x0 = float(ox) + (minX - maxX) * g, top = float(oy) + (minY - maxY) * g, x1 = float(ox), bottom = float(oy);
		const float vTop = mirrored ? 0.0f : 0.999f, vBottom = mirrored ? 0.999f : 0.0f;
		const std::array<uint32_t, 3> c = {0xc8ffffff, 0xc8ffffff, 0xc8ffffff};
		pen.drawTriangle(track.map, {{{x0, top, 0, vTop}, {x1, top, 0.999f, vTop}, {x1, bottom, 0.999f, vBottom}}}, c, view);
		pen.drawTriangle(track.map, {{{x0, bottom, 0, vBottom}, {x0, top, 0, vTop}, {x1, bottom, 0.999f, vBottom}}}, c, view);
	}
	for (size_t j = w.racers.race.size(); j-- > 0;) {
		if (j != i) {
			int32_t x, y;
			toMap(w.racers.visible[j].position, x, y);
			drawMapDot(pen, w, x, y, j == 1 && w.racers.race[1].ai, view);
		}
	}
	if (w.session.brief.mode == RaceBrief::Mode::TimeRace) {
		const auto ghost = [&](const GhostRun& run, bool valid, bool red) {
			Vec3 at;
			Quat q;
			if (valid && !run.finished && w.session.ghostAt(run, at, q)) {
				int32_t x, y;
				toMap(at, x, y);
				drawMapDot(pen, w, x, y, red, view);
			}
		};
		ghost(w.session.timeRace.record, w.session.timeRace.recordValid, true);
		ghost(w.session.timeRace.best, w.session.timeRace.bestValid, false);
	}
	int32_t x, y;
	toMap(w.racers.visible[i].position, x, y);
	const Vec3 f = w.racers.visible[i].rotation.f;
	const float len = std::sqrt(f.x * f.x + f.y * f.y);
	const float dx = len > 0 ? f.x / len : 1, dy = len > 0 ? -f.y / len : 0, nx = dy, ny = -dx;
	const auto arrow = [&](float tip, float back, float half, uint32_t colour) {
		const float ax = float(x), ay = float(y);
		pen.drawTriangle(-1,
			{{{ax - back * dx + half * nx, ay - back * dy + half * ny, -1, -1}, {ax + tip * dx, ay + tip * dy, -1, -1},
				{ax - back * dx - half * nx, ay - back * dy - half * ny, -1, -1}}},
			{colour, colour, colour}, view);
	};
	arrow(9.5f, 6.5f, 5.5f, 0xff000000);
	arrow(8.0f, 5.0f, 4.0f, 0xff00ff00);
}

// The rotating map: ahead is up, around this racer, inside its box.
void drawRotatingMap(const Pen& pen, const World& w, size_t p, const MenuData::Loading& track, MenuRect view)
{
	const PlayerCamera& cam = w.raceView.cameras[p];
	const size_t i = size_t(cam.racer);
	const std::array<float, 4>& b = track.mapBounds; // min x, max y, max x, min y
	const float minX = b[0], maxY = b[1], maxX = b[2], minY = b[3];
	const bool mirrored = w.session.brief.mirrored;
	const float ms = hudScale(w);
	const Vec3 me = w.racers.visible[i].position;
	const int32_t size = int32_t(128 * ms);
	const MenuRect box = {view.right - 15 - size, view.bottom - 15 - size, view.right - 15, view.bottom - 15};
	const float bx = float(box.left + box.right) * 0.5f, by = float(box.top + box.bottom) * 0.5f;
	Vec3 r_, d_, l_;
	cam.shownRows(r_, d_, l_);
	const float vlen = std::sqrt(l_.x * l_.x + l_.y * l_.y);
	const float ddx = vlen > 0 ? l_.x / vlen * 0.12f * ms : 0, ddy = vlen > 0 ? l_.y / vlen * 0.12f * ms : 0;
	const auto toBox = [&](float x, float y, float& sx, float& sy) {
		const float ex = x - me.x, ey = y - me.y;
		sx = bx + (ex * ddy - ey * ddx);
		sy = by - (ex * ddx + ey * ddy);
	};
	if (track.map >= 0) {
		const float yTop = mirrored ? -minY : maxY, yBottom = mirrored ? -maxY : minY;
		const float vTop = mirrored ? 0.0f : 0.999f, vBottom = mirrored ? 0.999f : 0.0f;
		std::array<std::array<float, 4>, 4> c;
		toBox(minX, yTop, c[0][0], c[0][1]);
		toBox(maxX, yTop, c[1][0], c[1][1]);
		toBox(maxX, yBottom, c[2][0], c[2][1]);
		toBox(minX, yBottom, c[3][0], c[3][1]);
		c[0][2] = 0, c[1][2] = 0.999f, c[2][2] = 0.999f, c[3][2] = 0;
		c[0][3] = c[1][3] = vTop, c[2][3] = c[3][3] = vBottom;
		const std::array<uint32_t, 3> colour = {0xc8ffffff, 0xc8ffffff, 0xc8ffffff};
		pen.drawTriangle(track.map, {c[0], c[1], c[3]}, colour, box);
		pen.drawTriangle(track.map, {c[3], c[1], c[2]}, colour, box);
	}
	for (size_t j = w.racers.race.size(); j-- > 0;) {
		if (j != i) {
			float sx, sy;
			toBox(w.racers.visible[j].position.x, w.racers.visible[j].position.y, sx, sy);
			drawMapDot(pen, w, int32_t(sx), int32_t(sy), j == 1 && w.racers.race[1].ai, view);
		}
	}
	const auto arrow = [&](float tip, float back, float half, uint32_t colour) {
		// (pointing up: d = (0, -1), n = (-1, 0))
		pen.drawTriangle(-1, {{{bx - half, by + back, -1, -1}, {bx, by - tip, -1, -1}, {bx + half, by + back, -1, -1}}},
			{colour, colour, colour}, box);
	};
	arrow(9.5f, 6.5f, 5.5f, 0xff000000);
	arrow(8.0f, 5.0f, 4.0f, 0xff00ff00);
	// Its 1-px white frame.
	const float l = float(box.left), t = float(box.top), rr = float(box.right), bb = float(box.bottom);
	const auto band = [&](float left, float top, float right, float bottom) {
		pen.drawTriangle(-1, {{{left, top, -1, -1}, {right, top, -1, -1}, {right, bottom, -1, -1}}}, {HudWhite, HudWhite, HudWhite},
			ScreenRect);
		pen.drawTriangle(-1, {{{left, bottom, -1, -1}, {left, top, -1, -1}, {right, bottom, -1, -1}}}, {HudWhite, HudWhite, HudWhite},
			ScreenRect);
	};
	band(l, t, rr, t + 1);
	band(l, bb - 1, rr, bb);
	band(l, t + 1, l + 1, bb - 1);
	band(rr - 1, t + 1, rr, bb - 1);
}

// The HUDs: per player, in its viewport, the banner, the race info (lap, best, total or place,
// time behind, WRONG WAY), the power-up frame and the gadget (map or speedometer).
// The HUD's text columns in a player's viewport: the fonts and their scale, the times' widths, the time column (rx),
// the place's (px), the centre (cx), the four rows and the viewport's top and bottom.
struct HudColumns {
	int16_t ths, ignum;
	float ts;
	int32_t shortWidth, longWidth, rx, px, cx, y1, y2, y3, y4, vt, vb;
};

// A time at the time column (a short one moved right by the difference): the x it was drawn at.
int32_t hudTime(const Pen& pen, const HudColumns& at, int32_t t, int32_t y)
{
	const std::u16string text = raceTime(t);
	const int32_t x = at.rx + (text.size() == 7 ? at.longWidth - at.shortWidth : 0);
	pen.drawScaled(at.ths, text, x, y, at.ts, at.ts, HudWhite);
	return x;
}

// Player p's racing rows: the lap and its time, the best lap, the total time (a time race) or the place, the time
// behind, and WRONG WAY.
void drawHudTimes(const Pen& pen, const World& w, size_t p, const HudColumns& at)
{
	const MenuData::Language& lang = pen.data->languages[pen.language];
	constexpr uint32_t White = HudWhite;
	const bool timed = w.session.brief.mode == RaceBrief::Mode::TimeRace;
	const int32_t laps = w.session.raceLaps;
	const Hud& h = w.raceView.huds[p];
	const Racer& r = w.racers.race[w.raceView.cameras[p].racer];
	// The lap row: the lap (alone), LAP, the lap's time (the last lap's while a new one is under 3 s, blinking;
	// the last once finished, blinking 3 s with FINISH).
	const int32_t n = std::min(r.laps + 1, laps);
	const std::u16string lap = number(n);
	const int32_t lapX = at.rx - (int32_t(float(pen.lineWidth(at.ths, lap)) * at.ts) + int32_t(14 * at.ts));
	pen.drawScaled(at.ths, lap, lapX, at.y1, at.ts, at.ts, White);
	pen.drawScaled(at.ths, lang.game.at(39), lapX - (int32_t(34 * at.ts) + int32_t(6 * at.ts)), at.y1, at.ts, at.ts, White);
	if (r.laps < laps) {
		const int32_t t = r.lapMs[std::clamp(r.laps, 0, 4)];
		if (r.laps > 0 && t < 3000) {
			if ((t / 250) % 2 == 0) {
				hudTime(pen, at, r.lapMs[r.laps - 1], at.y1);
			}
		}
		else {
			hudTime(pen, at, t, at.y1);
		}
	}
	else if (!(h.banner < 0 && -h.banner < 3000 && (-h.banner / 250) % 2 == 1)) {
		hudTime(pen, at, r.lapMs[std::clamp(laps - 1, 0, 4)], at.y1);
	}
	if (h.bestMs > 0) {
		pen.drawScaled(at.ths, lang.game.at(37), at.rx - (int32_t(49 * at.ts) + int32_t(14 * at.ts)), at.y2, at.ts, at.ts, White);
		hudTime(pen, at, h.bestMs, at.y2);
	}
	if (timed) {
		pen.drawScaled(at.ths, lang.game.at(38), at.rx - 74, at.y3, 1, 1, White);
		int32_t total = 0;
		for (int32_t l = 0; l < 5 && (r.laps < laps ? l <= r.laps : l < r.laps); l++) {
			total += r.lapMs[l];
		}
		hudTime(pen, at, total, at.y3);
	}
	else if (h.place >= 0 && h.place < 8) {
		const float k = h.pulseMs == 0 ? 1.0f
			: h.pulseMs <= 175		   ? 1.0f + float(h.pulseMs) / 175.0f
			: h.pulseMs <= 350		   ? 2.0f - float(h.pulseMs - 175) / 175.0f
									   : 1.0f;
		pen.drawScaled(at.ths, lang.game.at(size_t(25 + h.place)), at.px + 5, at.y1, at.ts, at.ts, White);
		const std::u16string digit(1, char16_t(u'1' + h.place));
		pen.drawScaled(at.ignum, digit, at.px - int32_t(float(pen.lineWidth(at.ignum, digit)) * at.ts), at.y1, at.ts * k, at.ts * k, White);
	}
	if (r.timeBehindMs > 0) {
		const int32_t x = hudTime(pen, at, r.timeBehind, at.y4);
		if (h.visible) {
			pen.drawScaled(at.ths, lang.game.at(33), x - 11, at.y4, 1, 1, White);
		}
	}
	if ((r.flags & RacerWrongWayShown) && h.visible) {
		const std::u16string_view text = lang.game.at(36);
		pen.drawScaled(at.ths, text, at.cx - (pen.lineWidth(at.ths, text) >> 1), at.vt + (at.vb - at.vt) / 5, 1, 1, White);
	}
}

} // namespace

void RaceHud::draw(const Pen& pen, const World& w)
{
	layers = {};
	const Session::Phase phase = w.session.phase;
	if (phase != Session::Countdown && phase != Session::Racing && phase != Session::Finished) {
		return;
	}
	const int16_t ths = pen.data->raceFont(pen.language, "font_ths"), ignum = pen.data->raceFont(pen.language, "ignum");
	const MenuData::Loading* track = pen.data->loading(w.session.brief.track);
	const bool testDrive = w.session.brief.mode == RaceBrief::Mode::TestDrive;
	const bool split = w.raceView.cameras.size() > 1;
	const float ts = split ? 0.9f : 1.0f;
	const int32_t shortWidth = int32_t(float(pen.lineWidth(ths, u"0:00:00")) * ts),
				  longWidth = int32_t(float(pen.lineWidth(ths, u"59:59:88")) * ts);
	const int32_t th = int32_t(float(pen.measure(ths, u"", 0).height()) * ts); // a line's height
	for (size_t p = 0; p < w.raceView.huds.size() && p < w.raceView.cameras.size() && p < layers.size(); p++) {
		const Hud& h = w.raceView.huds[p];
		const PlayerCamera& cam = w.raceView.cameras[p];
		const size_t i = size_t(cam.racer);
		const Racer& r = w.racers.race[i];
		layers[p].first = uint32_t(pen.elements.size());
		const int32_t vl = cam.viewport[0], vt = cam.viewport[1], vr = cam.viewport[2], vb = cam.viewport[3], width = vr - vl;
		const MenuRect view = {vl, vt, vr, vb};
		const int32_t rx = vr - ((11 * longWidth) >> 3), y1 = vt + 7, y2 = y1 + ((7 * th) >> 3), y3 = y1 + ((14 * th) >> 3),
					  y4 = y1 + ((21 * th) >> 3);
		const int32_t px = vl + width / 10, cx = (vl + vr) >> 1;
		if (h.banner != 0 && h.visible) {
			drawHudBanner(pen, w, p, cx, y4);
		}
		if (r.flags & RacerGhost) {
			layers[p].second = uint32_t(pen.elements.size());
			continue; // (in the warp tube: only the banner)
		}
		if (!testDrive) {
			drawHudTimes(pen, w, p, {ths, ignum, ts, shortWidth, longWidth, rx, px, cx, y1, y2, y3, y4, vt, vb});
		}
		drawPowerFrame(pen, w, i, view);
		// The gadget.
		const uint8_t mode = p < w.raceView.mapModes.size() ? w.raceView.mapModes[p] : uint8_t(GadgetFixedMap);
		if (mode == GadgetSpeedometer) {
			drawSpeedometer(pen, w, p, view);
		}
		else if (mode == GadgetFixedMap && track) {
			drawFixedMap(pen, w, p, *track, view);
		}
		else if (mode == GadgetRotatingMap && track) {
			drawRotatingMap(pen, w, p, *track, view);
		}
		layers[p].second = uint32_t(pen.elements.size());
	}
}

namespace {

// A slot's name on the standings, and whether its row shows (players' rows blink).
std::u16string_view slotName(const World& w, int32_t slot) { return std::u16string_view(w.session.brief.slots[slot].name); }
bool rowShown(const World& w, int32_t slot) { return !w.racers.race[slot].player || w.session.blinkMs >= 150; }

// A racer's race time: its laps' times summed.
int32_t raceMsOf(const World& w, int32_t s)
{
	int32_t sum = 0;
	for (int32_t lap = 0; lap < w.session.raceLaps; lap++) {
		sum += w.racers.race[s].lapMs[lap];
	}
	return sum;
}

// A standings table's rows (from y 132, 32 apart, above 324): each the slot left with the most points (ties to the last
// slot when tiesLast, else the first; `none` when no slot left beats 0), its place, name and points drawn when its row
// shows, then row(slot, y).
template <typename Row>
void standingsRows(const Pen& pen, const World& w, const Array<int32_t, 6>& points, bool tiesLast, int32_t none, Row row)
{
	const int16_t font = int16_t(pen.data->languages[pen.language].raceFonts);
	const int32_t n = int32_t(w.racers.race.size());
	Array<bool, 6> taken = {};
	for (int32_t place = 0, y = 132; place < n && y < 324; place++, y += 32) {
		int32_t pick = none;
		for (int32_t s = 0, most = 0; s < n; s++) {
			if (!taken[s] && (tiesLast ? points[s] >= most : points[s] > most)) {
				most = points[s];
				pick = s;
			}
		}
		taken[pick] = true;
		if (!rowShown(w, pick)) {
			continue;
		}
		pen.drawLine(font, number(place + 1), 20, y, HudWhite);
		pen.drawLine(font, slotName(w, pick), 50, y, HudWhite);
		pen.drawLine(font, number(points[pick]), 450, y, HudWhite);
		row(pick, y);
	}
}

// The race's standings: by this race's points (ties to the last slot), with the gaps to the leader.
void drawRaceStandings(const Pen& pen, const World& w)
{
	const MenuData::Language& lang = pen.data->languages[pen.language];
	const int16_t font = int16_t(lang.raceFonts);
	const int32_t n = int32_t(w.racers.race.size());
	int32_t leader = n - 1, best = 0;
	for (int32_t s = 0; s < n; s++) {
		if (w.session.roundPoints[s] >= best) {
			best = w.session.roundPoints[s];
			leader = s;
		}
	}
	char16_t sign = u'+';
	standingsRows(pen, w, w.session.roundPoints, true, leader, [&](int32_t pick, int32_t y) {
		if (pick == leader) {
			pen.drawLine(font, lang.game.at(42), 500, y, HudWhite);
		}
		else if (w.racers.race[pick].flags & RacerFinished) {
			const int32_t gap = raceMsOf(w, pick) - raceMsOf(w, leader);
			sign = gap > 0 ? u'+' : gap < 0 ? u'-' : sign;
			pen.drawLine(font, raceTime(std::abs(gap)), 520, y, HudWhite);
			pen.drawLine(font, std::u16string(1, sign), 500, y, HudWhite);
		}
		else {
			pen.drawLine(font, lang.game.at(43), 500, y, HudWhite);
		}
	});
}

// The circuit's standings: by its points (ties to the first slot).
void drawCircuitStandings(const Pen& pen, const World& w)
{
	const MenuData::Language& lang = pen.data->languages[pen.language];
	const int16_t font = int16_t(lang.raceFonts);
	const std::u16string_view title = lang.game.at(45);
	pen.drawLine(font, title, 320 - (pen.lineWidth(font, title) >> 1), 60, HudWhite);
	standingsRows(pen, w, w.session.circuitPoints, false, 0, [](int32_t, int32_t) {});
}

// The race's dialog over it.
void drawRaceDialog(const Pen& pen, const World& w)
{
	const MenuData::Language& lang = pen.data->languages[pen.language];
	const int16_t font = int16_t(lang.raceFonts);
	const RaceDialog& d = w.session.dialog;
	if (d.kind == RaceDialog::None) {
		return;
	}
	// The dialog: a dimmed screen, the prompt (and its second line) and the options centred, one line height apart.
	int32_t h = 0;
	for (int32_t i = 0; i < d.count; i++) {
		h = std::max(h, pen.measure(font, lang.game.at(size_t(d.options[i])), 0).height());
	}
	h += 2;
	const int32_t y0 = 240 - (d.count * h) / 2;
	pen.drawQuad(ScreenRect, 0x40000000, 64);
	const auto centred = [&](int32_t text, int32_t y, uint32_t colour) {
		const std::u16string_view line = lang.game.at(size_t(text));
		pen.drawLine(font, line, 320 - (pen.lineWidth(font, line) >> 1), y, colour);
	};
	int32_t y = y0 - h - (d.second >= 0 ? h >> 1 : 0);
	centred(d.prompt, y, HudWhite);
	if (d.second >= 0) {
		y += h;
		centred(d.second, y, HudWhite);
	}
	for (int32_t i = 0; i < d.count; i++) {
		y += h;
		if (i == d.selection && !d.chosen) {
			// Blinking, its alpha pulsing.
			if (d.blinkMs < 250) {
				const int32_t angle = int32_t(float(d.blinkMs) * 0.002f * Tau * -162.97466f);
				const int32_t alpha = 191 + int32_t(cosEntry((-256 - angle) & 1023) * -64.0f);
				centred(d.options[i], y, uint32_t(alpha) << 24 | 0xffff24);
			}
			continue;
		}
		centred(d.options[i], y, 0xff7f7f12);
	}
}

} // namespace

void Menus::drawRace(const World& w)
{
	elements.clear();
	glyphs.clear();
	scenes.clear();
	hud.draw(pen(), w);
	const MenuData::Language& lang = data->languages[language];
	// The standings table showing: the race's or the circuit's.
	const int32_t table = w.session.standingsTable();
	if (table == 1) {
		drawRaceStandings(pen(), w);
	}
	else if (table == 2) {
		drawCircuitStandings(pen(), w);
	}
	// The attract demo's DEMO, blinking.
	if (w.session.brief.mode == RaceBrief::Mode::Demo && w.raceView.demoMs > 250) {
		const int16_t ths = data->raceFont(language, "font_ths");
		const std::u16string_view demo = lang.game.at(46);
		pen().drawLine(ths, demo, 320 - (pen().lineWidth(ths, demo) >> 1), 443, HudWhite);
	}
	// The opening fade from black.
	if (w.session.openingMs > 0) {
		const int32_t alpha = 255 * w.session.openingMs / 500;
		pen().drawQuad(ScreenRect, uint32_t(alpha) << 24, alpha);
	}
	// The fade to black, under the dialog.
	if (w.session.fade.active && w.session.fade.ms > 0) {
		const int32_t alpha = int32_t(255.0f * (1.0f - float(w.session.fade.left) / float(w.session.fade.ms)));
		pen().drawQuad(ScreenRect, uint32_t(alpha) << 24, alpha);
	}
	drawRaceDialog(pen(), w);
}

} // namespace lr
