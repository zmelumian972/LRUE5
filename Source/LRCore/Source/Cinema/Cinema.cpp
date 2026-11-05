// The cinematic screens' player: a scene's clock and its cues begun and ended in time order,
// the links that start and stop effects as they do, the 2D overlays and fades it draws, and the flow after an award.
// What the cues show in 3D and what the sounds and particles do is the presentation's.

#include "LRCore/Menus/Menus.h"

#include <algorithm>
#include <cctype>
#include <numeric>

namespace lr {

namespace {

constexpr MenuRect FullScreen = {0, 0, 640, 480};

// The screens' tunes (LEGOMSC's): the awards, the win cars, the circuits' intros.
int32_t cinemaTune(uint16_t screen)
{
	static constexpr int32_t Tunes[16] = {2, 3, 4, 11, 13, 14, 15, -1, -1, 5, 6, 7, 8, 9, 10, 12};
	return screen >= 22 && screen <= 37 ? Tunes[screen - 22] : -1;
}

// Names as the scene files compare them: up to 8 characters, case-insensitively.
bool sameName(std::string_view a, std::string_view b)
{
	a = a.substr(0, 8), b = b.substr(0, 8);
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y)); });
}

} // namespace

bool Menus::cinematicScreen() const { return screen == ScreenLegal || screen == ScreenCredits || (screen >= 22 && screen <= 37 && screen != 29 && screen != 30); }

void Menus::startCinema(Game& g)
{
	const CinemaFile& file = data->screens[screen].cinema;
	const CinemaFile::Scene& scene = file.scenes[0];
	state.rate = scene.rate;
	state.cinemaFrames = scene.frames;
	CinemaPlayer& c = state.cinema;
	cinemaStart3d(c, data->screens[screen], 0);
	c.overlays.assign(file.effects.size(), {});
	// The model swaps: the awards', the win cars' and circuit 7's guy1 shows the player's
	// minifig (the win car's: slot 1's, the champion just beaten), their carbody the player's car (the win car's: the
	// champion's quick-build car).
	state.cinemaSwap = screen == ScreenWinCar || screen == ScreenRocketCar || (screen >= ScreenAward1 && screen <= ScreenAward1 + 3) ||
		screen == ScreenCircuitIntro + 6;
	cinemaCar = {};
	state.cinemaRacer = {};
	if (state.cinemaSwap) {
		const RacerRecord* player = g.racer(g.chosen[0]);
		if (player) {
			state.cinemaRacer = *player;
		}
		state.cinemaLooks = {state.cinemaRacer.looks[0], state.cinemaRacer.looks[1], state.cinemaRacer.looks[2], state.cinemaRacer.looks[3],
			state.cinemaRacer.expression};
		if (screen == ScreenWinCar && g.circuit >= 0) {
			const std::string& name = data->circuits.circuits[size_t(g.circuit)].drivers[1];
			const DriverTable::Driver* d = data->drivers.find(name);
			state.cinemaRacer.pieces.clear();
			if (d) {
				state.cinemaLooks = driverLooks(*d);
				const ChampionTable::Champion* champion = data->champions.find(d->champion);
				for (const MenuData::QuickBuild& q : data->quickBuilds) {
					if (champion && q.name == u"CHAMP" && std::equal(q.chassis.begin(), q.chassis.end(), champion->chassis.begin(), champion->chassis.end(),
						[](char a, char b) { return std::tolower(uint8_t(a)) == std::tolower(uint8_t(b)); })) {
						state.cinemaRacer.pieces = q.pieces;
						break;
					}
				}
			}
		}
		if (!state.cinemaRacer.pieces.empty()) {
			Car car;
			car.load(data->pieces, state.cinemaRacer.pieces);
			car.chooseOffset(data->pieces);
			cinemaCar = car.menuMesh(data->pieces, data->sets.materials);
		}
	}
	// The win car names the champion just beaten (racer slot 1's driver): the other champions' lines are off.
	if (screen == 26 && g.circuit >= 0) {
		const std::string& champion = data->circuits.circuits[size_t(g.circuit)].drivers[1];
		for (size_t e = 0; e < file.effects.size(); e++) {
			const std::string& name = file.effects[e].name;
			const bool lettered = name.size() > 4 && std::all_of(name.begin() + 4, name.end(), [](char ch) { return std::isalpha(uint8_t(ch)); });
			if (name.starts_with("text") && lettered && !champion.starts_with(name.substr(4))) {
				c.overlays[e].disabled = true;
			}
		}
	}
	const int32_t music = cinemaTune(screen);
	if (music >= 0) {
		playTune(music, false);
	}
	if (screen >= 22 && screen <= 24) {
		g.grantAward(screen, *data);
	}
}

void Menus::updateCinema(int32_t dt)
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
		cueWindow(c, from, to);
		if (to >= state.cinemaFrames) {
			state.complete = true;
			// (Its cues stop showing, lights with them; the view stays: the award's last two frames draw the camera with
			// nothing lit.)
			for (const uint16_t cue : std::vector<uint16_t>(c.begun)) {
				cue3d(c, data->screens[screen], cue, false);
				c.active[cue] = false;
			}
			c.begun.clear();
		}
		else {
			cueTicks(c, data->screens[screen], ms);
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
		const CinemaFile::Effect& fx = data->screens[screen].cinema.effects[size_t(k.effect)];
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

void Menus::cueChanged(size_t cue, bool begins)
{
	const CinemaFile& file = data->screens[screen].cinema;
	const CinemaFile::Cue& q = file.scenes[0].cues[cue];
	CinemaPlayer& c = state.cinema;
	cue3d(c, data->screens[screen], cue, begins);
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
			effect(size_t(l->effect), l->start, channel >= 0x52 && q.kind == CinemaFile::Cue::Model ? int32_t(cue) : -1);
		}
	}
}

void Menus::effect(size_t e, bool start, int32_t cue)
{
	const MenuData::Screen& s = data->screens[screen];
	const CinemaFile::Effect& fx = s.cinema.effects[e];
	CinemaPlayer& c = state.cinema;
	switch (fx.kind) {
	case CinemaFile::Effect::Colour: {
		// On the models of its own entity, else of the model cue's that started it.
		const auto run = std::find_if(c.colourings.begin(), c.colourings.end(), [&](const CinemaPlayer::Colouring& k) { return k.effect == int16_t(e); });
		if (!start) {
			if (run != c.colourings.end()) {
				c.colourings.erase(run);
			}
		}
		else if (run == c.colourings.end()) {
			std::array<int16_t, 2> model = {-1, -1};
			for (size_t w = 0; w < s.cinemaWorlds.size() && model[0] < 0 && (fx.entityKind == 0x5d || fx.entityKind == 0x5e); w++) {
				for (const SceneFile::Entity& ent : fx.entityKind == 0x5d ? s.cinemaWorlds[w].scene.animated : s.cinemaWorlds[w].scene.entities) {
					if (sameName(ent.name, fx.entity) && ent.model >= 0) {
						model = {int16_t(w), int16_t(ent.model)};
						break;
					}
				}
			}
			if (fx.entityKind == 0 && cue >= 0 && size_t(cue) < c.entityOf.size() && c.entityOf[size_t(cue)][0] >= 0) {
				const std::array<int16_t, 3> en = c.entityOf[size_t(cue)];
				const SceneFile& f = s.cinemaWorlds[size_t(en[0])].scene;
				const SceneFile::Entity& ent = en[1] ? f.animated[size_t(en[2])] : f.entities[size_t(en[2])];
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
	default:
		c.effects.push_back({int16_t(e), start});
		return;
	}
}

void Menus::drawCinema()
{
	if (state.finished) {
		return;
	}
	const CinemaFile& file = data->screens[screen].cinema;
	CinemaPlayer& c = state.cinema;
	// The scene, for the presentation to pose at its clock.
	MenuElement view;
	view.kind = MenuElement::View3D;
	view.dest = {0, 0, 640, 480};
	view.scene = int32_t(scenes.size());
	MenuScene& scene = scenes.emplace_back();
	describeCinema(c, data->screens[screen], scene, state.cinemaSwap ? &state.cinemaLooks : nullptr, state.cinemaSwap ? &state.cinemaRacer : nullptr);
	scene.aspect = 640.0f / 480.0f;
	lightScene(scene, nullptr);
	elements.push_back(view);
	// The overlays, pictures then texts, laid out at their first draw: centred in the screen where not placed.
	const auto drawn = [&](CinemaFile::Effect::Kind kind) {
		for (size_t e = 0; e < file.effects.size(); e++) {
			const CinemaFile::Effect& fx = file.effects[e];
			CinemaPlayer::Overlay& o = c.overlays[e];
			if (fx.kind != kind || !o.shown) {
				continue;
			}
			const int16_t font = kind == CinemaFile::Effect::Text ? data->font(language, fx.font) : int16_t(-1);
			const int16_t image = kind == CinemaFile::Effect::Picture ? data->image(fx.picture, screen) : int16_t(-1);
			const std::vector<StringTable>& tables = data->screens[screen].cinemaText[language];
			std::u16string text = fx.text.empty() ? std::u16string() : std::u16string(fx.text.begin(), fx.text.end());
			if (fx.table >= 0 && size_t(fx.table) < tables.size()) {
				text = tables[size_t(fx.table)].at(size_t(fx.string));
			}
			if (o.pending) {
				o.pending = false;
				if (image >= 0) {
					o.width = int32_t(data->images[size_t(image)].width), o.height = int32_t(data->images[size_t(image)].height);
				}
				else {
					const MenuRect m = measure(font, text, 0);
					o.width = m.width(), o.height = m.height();
				}
				const int32_t cw = fx.given[2] ? int32_t(fx.place[2] * 640) : o.width, ch = fx.given[3] ? int32_t(fx.place[3] * 480) : o.height;
				o.place[0] = fx.given[0] ? o.place[0] : float((640 - cw) / 2) / 640;
				o.place[1] = fx.given[1] ? o.place[1] : float((480 - ch) / 2) / 480;
				o.place[2] = fx.given[2] ? o.place[2] : float(cw) / 640;
				o.place[3] = fx.given[3] ? o.place[3] : float(ch) / 480;
			}
			const int32_t x = int32_t(o.place[0] * 640), y = int32_t(o.place[1] * 480), w = int32_t(o.place[2] * 640), h = int32_t(o.place[3] * 480);
			if (o.place[0] < 0 || o.place[1] < 0 || w == 0 || h == 0 || x + w > 640 || y + h > 480) {
				continue;
			}
			if (image >= 0) {
				const MenuData::ImageInfo& info = data->images[size_t(image)];
				drawImage(image, {x, y, x + w, y + h}, {0, 0, int32_t(info.width), int32_t(info.height)}, nullptr, fx.tinted ? fx.colour : 0xffffffff);
			}
			else {
				drawScaled(font, text, x, y, float(w) / float(o.width), float(h) / float(o.height), fx.tinted ? fx.colour : 0xffffffff);
			}
		}
	};
	drawn(CinemaFile::Effect::Picture);
	drawn(CinemaFile::Effect::Text);
	// The fades over the whole screen: in, from the colour; out, to it.
	for (const CinemaPlayer::Fade& f : c.fades) {
		const CinemaFile::Effect* fx = f.effect >= 0 ? &file.effects[size_t(f.effect)] : nullptr;
		if (!fx || fx->ms <= 0 || (!fx->in && !fx->out)) {
			continue;
		}
		const float left = float(f.left) / float(fx->ms);
		const int32_t alpha = int32_t(255.0f * (fx->in ? left : 1 - left)) & 255;
		drawQuad(FullScreen, (fx->colour & 0xffffff) | uint32_t(alpha) << 24, alpha);
	}
}

void Menus::leaveCinema(Game& g)
{
	const auto pop = [&](int n) { depth = uint8_t(std::max(0, depth - n)); };
	if (screen >= ScreenCircuitIntro && screen < ScreenCircuitIntro + 6) {
		// A circuit's intro: its races start (an empty stack), unless it was opened over the main menu.
		pop(1);
		const bool menu = top() == ScreenMainMenu;
		depth = 0;
		if (menu) {
			push(ScreenMainMenu);
		}
		return;
	}
	// An award, a win car or the Rocket Racer's intro.
	pop(1);
	if (depth > 0) {
		const bool menu = top() == ScreenMainMenu;
		depth = 0;
		if (menu) {
			push(ScreenMainMenu);
		}
		return;
	}
	if (g.partsUnlocked) {
		const bool rocket = g.circuit >= 0 && data->circuits.circuits[size_t(g.circuit)].partSet == 6;
		push(rocket ? ScreenRocketCar : ScreenWinCar);
	}
	else {
		push(screen == ScreenRocketCar ? ScreenCredits : ScreenMainMenu);
	}
	if (g.granted) {
		push(ScreenSaveAll);
		g.saveReminder = true;
	}
	g.partsUnlocked = g.granted = false;
}

} // namespace lr
