// The cinematic screens' player: a scene's clock and its cues begun and ended in time order,
// the links that start and stop effects as they do, the 2D overlays and fades it draws, and the flow after an award.
// What the cues show in 3D and what the sounds and particles do is the presentation's.

#include "LRCore/Menus/Menus.h"
#include "LRData/Archive.h"

#include "Menus/Pen.h"

#include <algorithm>
#include <cctype>
#include <numeric>

namespace lr {

namespace {

// The screens' tunes (LEGOMSC's): the awards, the win cars, the circuits' intros.
int32_t cinemaTune(uint16_t screen)
{
	static constexpr int32_t Tunes[16] = {2, 3, 4, 11, 13, 14, 15, -1, -1, 5, 6, 7, 8, 9, 10, 12};
	return screen >= 22 && screen <= 37 ? Tunes[screen - 22] : -1;
}

// An overlay's place (a fraction of the screen) in pixels: one beyond 4 screens is as far off it as 4 are, which keeps
// the sums of pixels in range.
int32_t pixels(float place, int32_t screen) { return truncatedInt(std::clamp(place, -4.0f, 4.0f) * float(screen)); }

} // namespace

bool Menus::cinematicScreen() const
{
	return screen == ScreenLegal || screen == ScreenCredits || (screen >= 22 && screen <= 37 && screen != 29 && screen != 30);
}

void Cinema::startCinema(Menus& m)
{
	Game& g = *m.game;
	const CinemaFile& file = m.data->screens[m.screen].cinema;
	const CinemaFile::Scene& scene = file.scenes[0];
	state.rate = scene.rate;
	state.cinemaFrames = scene.frames;
	CinemaPlayer& c = state.cinema;
	cinemaStart3d(c, m.data->screens[m.screen], 0);
	c.overlays.assign(file.effects.size(), {});
	// The model swaps: the awards', the win cars' and circuit 7's guy1 shows the player's
	// minifig (the win car's: slot 1's, the champion just beaten), their carbody the player's car (the win car's: the
	// champion's quick-build car).
	state.cinemaSwap = m.screen == ScreenWinCar || m.screen == ScreenRocketCar ||
		(m.screen >= ScreenAward1 && m.screen <= ScreenAward1 + 3) || m.screen == ScreenCircuitIntro + 6;
	m.cinemaCar = {};
	state.cinemaRacer = {};
	if (state.cinemaSwap) {
		const RacerRecord* player = g.racer(g.chosen[0]);
		if (player) {
			state.cinemaRacer = *player;
		}
		state.cinemaLooks = {state.cinemaRacer.looks[0], state.cinemaRacer.looks[1], state.cinemaRacer.looks[2], state.cinemaRacer.looks[3],
			state.cinemaRacer.expression};
		if (m.screen == ScreenWinCar && g.circuit >= 0) {
			const std::string& name = m.data->circuits.circuits[g.circuit].drivers[1];
			const DriverTable::Driver* d = m.data->drivers.find(name);
			state.cinemaRacer.pieces.clear();
			if (d) {
				state.cinemaLooks = driverLooks(*d);
				const ChampionTable::Champion* champion = m.data->champions.find(d->champion);
				for (const MenuData::QuickBuild& q : m.data->quickBuilds) {
					if (champion && q.name == u"CHAMP" && equalsIgnoreCase(q.chassis, champion->chassis)) {
						state.cinemaRacer.pieces = q.pieces;
						break;
					}
				}
			}
		}
		if (!state.cinemaRacer.pieces.empty()) {
			Car car;
			car.load(m.data->pieces, state.cinemaRacer.pieces);
			car.chooseOffset(m.data->pieces);
			m.cinemaCar = car.menuMesh(m.data->pieces, m.data->sets.materials);
		}
	}
	// The win car names the champion just beaten (racer slot 1's driver): the other champions' lines are off.
	if (m.screen == 26 && g.circuit >= 0) {
		const std::string& champion = m.data->circuits.circuits[g.circuit].drivers[1];
		for (size_t e = 0; e < file.effects.size(); e++) {
			const std::string& name = file.effects[e].name;
			const bool lettered = name.size() > 4 &&
				std::all_of(name.begin() + 4, name.end(), [](char ch) { return std::isalpha(uint8_t(ch)); });
			if (name.starts_with("text") && lettered && !champion.starts_with(name.substr(4))) {
				c.overlays[e].disabled = true;
			}
		}
	}
	const int32_t music = cinemaTune(m.screen);
	if (music >= 0) {
		m.playTune(music, false);
	}
	if (m.screen >= 22 && m.screen <= 24) {
		g.grantAward(m.screen, *m.data);
	}
}

void Cinema::updateCinema(Menus& m, int32_t dt)
{
	CinemaPlayer& c = state.cinema;
	c.effects.clear();
	if (state.finished) {
		return;
	}
	// The scene's update: its scaled clock, the cues between the last frame and this one; complete at its length. Every
	// update, the first included, runs on the frame's ms.
	const int32_t ms = dt;
	c.first = false;
	if (state.complete) {
		state.finished = true;
	}
	else {
		const uint32_t scaled = state.scaled + uint32_t(ms * state.rate);
		const int32_t to = int32_t(scaled / 1000), from = state.cinemaFrame;
		// Starts and ends interleaved in time order: a cue that starts after another ends waits for that end.
		cueWindow(m, c, from, to);
		if (to >= state.cinemaFrames) {
			state.complete = true;
			// (Its cues stop showing, lights with them; the view stays: the award's last two frames draw the camera with
			// nothing lit.)
			for (const uint16_t cue : std::vector<uint16_t>(c.begun)) {
				cue3d(c, m.data->screens[m.screen], cue, false);
				c.active[cue] = false;
			}
			c.begun.clear();
		}
		else {
			cueTicks(c, m.data->screens[m.screen], ms);
			state.scaled = scaled;
			state.cinemaFrame = to;
			c.scaled = scaled;
			c.frame = to;
		}
	}
	// The effects' update: a fade's time (one that ran out stays two updates more, then frees its slot).
	for (CinemaPlayer::Fade& f : c.fades) {
		if (f.effect < 0) {
			continue;
		}
		if (f.left == 0) {
			if (f.expired) {
				f.effect = -1;
				continue;
			}
			f.expired = true;
		}
		f.left = ms > f.left ? 0 : f.left - ms;
	}
	// The colour effects' offsets move by their speeds, only by a whole step on some channel (QUIRK).
	const float seconds = float(ms) / 1000.0f;
	for (CinemaPlayer::Colouring& k : c.colourings) {
		const CinemaFile::Effect& fx = m.data->screens[m.screen].cinema.effects[k.effect];
		std::array<float, 3> step;
		bool moves = false;
		for (size_t ch = 0; ch < 3; ch++) {
			step[ch] = fx.offsetSpeed[ch] * seconds;
			moves = moves || int32_t(step[ch]) != 0;
		}
		for (size_t ch = 0; ch < 3 && moves; ch++) {
			k.offsets[ch] += step[ch];
		}
	}
}

void Cinema::cueChanged(Menus& m, size_t cue, bool begins)
{
	const CinemaFile& file = m.data->screens[m.screen].cinema;
	const CinemaFile::Cue& q = file.scenes[0].cues[cue];
	CinemaPlayer& c = state.cinema;
	cue3d(c, m.data->screens[m.screen], cue, begins);
	c.active[cue] = begins;
	if (begins) {
		c.begun.insert(c.begun.begin(), uint16_t(cue));
	}
	else {
		c.begun.erase(std::remove(c.begun.begin(), c.begun.end(), uint16_t(cue)), c.begun.end());
	}
	// The links on its name: any cue's channel, then its kind's; of several the first listed first, then the rest
	// from the last.
	for (const uint8_t channel : {uint8_t(begins ? 0x50 : 0x51), uint8_t(0x52 + 2 * q.kind + (begins ? 0 : 1))}) {
		std::vector<const CinemaFile::Link*> on;
		for (const CinemaFile::Link& l : file.links) {
			if (l.channel == channel && l.cue == q.name) {
				on.push_back(&l);
			}
		}
		if (!on.empty()) {
			std::reverse(on.begin() + 1, on.end());
		}
		for (const CinemaFile::Link* l : on) {
			effect(m, size_t(l->effect), l->start, channel >= 0x52 && q.kind == CinemaFile::Cue::Model ? int32_t(cue) : -1);
		}
	}
}

void Cinema::effect(Menus& m, size_t e, bool start, int32_t cue)
{
	const MenuData::Screen& s = m.data->screens[m.screen];
	const CinemaFile::Effect& fx = s.cinema.effects[e];
	CinemaPlayer& c = state.cinema;
	switch (fx.kind) {
	case CinemaFile::Effect::Colour: {
		// On the models of its own entity, else of the model cue's that started it.
		const auto run = std::find_if(c.colourings.begin(), c.colourings.end(),
			[&](const CinemaPlayer::Colouring& k) { return k.effect == int16_t(e); });
		if (!start) {
			if (run != c.colourings.end()) {
				c.colourings.erase(run);
			}
		}
		else if (run == c.colourings.end()) {
			std::array<int16_t, 2> model = {-1, -1};
			for (size_t w = 0; w < s.cinemaWorlds.size() && model[0] < 0 && (fx.entityKind == 0x5d || fx.entityKind == 0x5e); w++) {
				for (const SceneFile::Entity& ent :
					fx.entityKind == 0x5d ? s.cinemaWorlds[w].scene.animated : s.cinemaWorlds[w].scene.entities) {
					if (sameName(ent.name, fx.entity) && ent.model >= 0) {
						model = {int16_t(w), int16_t(ent.model)};
						break;
					}
				}
			}
			if (fx.entityKind == 0 && inRange(c.entityOf, cue) && c.entityOf[cue][0] >= 0) {
				const std::array<int16_t, 3> en = c.entityOf[cue];
				const SceneFile& f = s.cinemaWorlds[en[0]].scene;
				const SceneFile::Entity& ent = en[1] ? f.animated[en[2]] : f.entities[en[2]];
				model = {en[0], int16_t(ent.model)};
			}
			if (model[0] >= 0 && model[1] >= 0) {
				c.colourings.push_back({int16_t(e), model, {float(fx.offset[0]), float(fx.offset[1]), float(fx.offset[2])}});
			}
		}
		c.effects.push_back({int16_t(e), start});
		return;
	}
	case CinemaFile::Effect::Fade:
		if (!start) {
			for (CinemaPlayer::Fade& f : c.fades) {
				f = f.effect == int16_t(e) ? CinemaPlayer::Fade() : f;
			}
			return;
		}
		for (CinemaPlayer::Fade& f : c.fades) {
			if (f.effect < 0) {
				f = {int16_t(e), fx.ms, false};
				return;
			}
		}
		return;
	case CinemaFile::Effect::Text:
	case CinemaFile::Effect::Picture: {
		CinemaPlayer::Overlay& o = c.overlays[e];
		if (!start) {
			o.shown = false;
		}
		else if (!o.shown && !o.disabled) {
			o.shown = o.pending = true;
			o.place = fx.place;
		}
		return;
	}
	default: c.effects.push_back({int16_t(e), start}); return;
	}
}

void Cinema::drawCinema(Menus& m)
{
	Pen pen = m.pen();
	if (state.finished) {
		return;
	}
	const CinemaFile& file = m.data->screens[m.screen].cinema;
	CinemaPlayer& c = state.cinema;
	// The scene, for the presentation to pose at its clock.
	MenuElement view;
	view.kind = MenuElement::View3D;
	view.dest = {0, 0, float(ScreenWidth), float(ScreenHeight)};
	view.scene = int32_t(m.scenes.size());
	MenuScene& scene = m.scenes.emplace_back();
	describeCinema(m, c, m.data->screens[m.screen], scene, state.cinemaSwap ? &state.cinemaLooks : nullptr,
		state.cinemaSwap ? &state.cinemaRacer : nullptr);
	scene.aspect = float(ScreenWidth) / float(ScreenHeight);
	m.lightScene(scene, nullptr);
	m.elements.push_back(view);
	// The overlays, pictures then texts, laid out at their first draw: centred in the screen where not placed.
	const auto drawn = [&](CinemaFile::Effect::Kind kind) {
		for (size_t e = 0; e < file.effects.size(); e++) {
			const CinemaFile::Effect& fx = file.effects[e];
			CinemaPlayer::Overlay& o = c.overlays[e];
			if (fx.kind != kind || !o.shown) {
				continue;
			}
			const int16_t font = kind == CinemaFile::Effect::Text ? m.data->font(m.language, fx.font) : int16_t(-1);
			const int16_t image = kind == CinemaFile::Effect::Picture ? m.data->image(fx.picture, m.screen) : int16_t(-1);
			const List<StringTable>& tables = m.data->screens[m.screen].cinemaText[m.language];
			std::u16string text = fx.text.empty() ? std::u16string() : std::u16string(fx.text.begin(), fx.text.end());
			if (inRange(tables, fx.table)) {
				text = tables[fx.table].at(size_t(fx.string));
			}
			if (o.pending) {
				o.pending = false;
				if (image >= 0) {
					o.width = int32_t(m.data->images[image].width), o.height = int32_t(m.data->images[image].height);
				}
				else {
					const MenuRect size = pen.measure(font, text, 0);
					o.width = size.width(), o.height = size.height();
				}
				const int32_t cw = fx.given[2] ? pixels(fx.place[2], ScreenWidth) : o.width,
							  ch = fx.given[3] ? pixels(fx.place[3], ScreenHeight) : o.height;
				o.place[0] = fx.given[0] ? o.place[0] : float((ScreenWidth - cw) / 2) / ScreenWidth;
				o.place[1] = fx.given[1] ? o.place[1] : float((ScreenHeight - ch) / 2) / ScreenHeight;
				o.place[2] = fx.given[2] ? o.place[2] : float(cw) / ScreenWidth;
				o.place[3] = fx.given[3] ? o.place[3] : float(ch) / ScreenHeight;
			}
			const int32_t x = pixels(o.place[0], ScreenWidth), y = pixels(o.place[1], ScreenHeight), w = pixels(o.place[2], ScreenWidth),
						  h = pixels(o.place[3], ScreenHeight);
			if (o.place[0] < 0 || o.place[1] < 0 || w == 0 || h == 0 || x + w > ScreenWidth || y + h > ScreenHeight) {
				continue;
			}
			if (image >= 0) {
				const MenuData::ImageInfo& info = m.data->images[image];
				pen.drawImage(image, {x, y, x + w, y + h}, {0, 0, int32_t(info.width), int32_t(info.height)}, nullptr,
					fx.tinted ? fx.colour : 0xffffffff);
			}
			else {
				pen.drawScaled(font, text, x, y, float(w) / float(o.width), float(h) / float(o.height), fx.tinted ? fx.colour : 0xffffffff);
			}
		}
	};
	drawn(CinemaFile::Effect::Picture);
	drawn(CinemaFile::Effect::Text);
	// The fades over the whole screen: in, from the colour; out, to it.
	for (const CinemaPlayer::Fade& f : c.fades) {
		const CinemaFile::Effect* fx = f.effect >= 0 ? &file.effects[f.effect] : nullptr;
		if (!fx || fx->ms <= 0 || (!fx->in && !fx->out)) {
			continue;
		}
		const float left = float(f.left) / float(fx->ms);
		const int32_t alpha = int32_t(255.0f * (fx->in ? left : 1 - left)) & 255;
		pen.drawQuad(ScreenRect, (fx->colour & 0xffffff) | uint32_t(alpha) << 24, alpha);
	}
}

void Cinema::leaveCinema(Menus& m)
{
	Game& g = *m.game;
	// A circuit's intro: its races start (an empty stack), unless it was opened over the main menu. So too an award, a
	// win car or the Rocket Racer's intro opened over a menu.
	const bool intro = m.screen >= ScreenCircuitIntro && m.screen < ScreenCircuitIntro + 6;
	m.depth = uint8_t(std::max(0, m.depth - 1));
	if (intro || m.depth > 0) {
		const bool menu = m.top() == ScreenMainMenu;
		m.depth = 0;
		if (menu) {
			m.push(ScreenMainMenu);
		}
		return;
	}
	if (g.partsUnlocked) {
		const bool rocket = g.circuit >= 0 && m.data->circuits.circuits[g.circuit].partSet == 6;
		m.push(rocket ? ScreenRocketCar : ScreenWinCar);
	}
	else {
		m.push(m.screen == ScreenRocketCar ? ScreenCredits : ScreenMainMenu);
	}
	if (g.granted) {
		m.push(ScreenSaveAll);
		g.saveReminder = true;
	}
	g.partsUnlocked = g.granted = false;
}

} // namespace lr
