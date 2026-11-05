#include "LRCore/Common/Game.h"
#include "LRCore/Common/Tables.h"

#include "LRCore/Race/World.h"

namespace lr {

namespace {

constexpr uint32_t Keyboard = 1u << 28;

// The keyboard layouts' bindings (DirectInput scan codes): arrows, Enter, right Ctrl, M, Space, right Alt; A D W X,
// left Alt, Q E, left Shift, Z; the keypad's 4 6 8 2, right Shift, 3 7 Enter 9.
constexpr uint32_t KeyboardDefaults[3][9] = {
	{0xcb, 0xcd, 0xc8, 0xd0, 0x1c, 0x9d, 0x32, 0x39, 0xb8},
	{0x1e, 0x20, 0x11, 0x2d, 0x38, 0x10, 0x12, 0x2a, 0x2c},
	{0x4b, 0x4d, 0x48, 0x50, 0x36, 0x51, 0x47, 0x9c, 0x49},
};

bool joystickLayout(size_t layout) { return layout < 2; }

// The save file: "LRSV", version, payload size, CRC-32 of the payload, the payload.
constexpr uint32_t Magic = 0x5653524c, Version = 1;

uint32_t crc32(std::span<const uint8_t> bytes)
{
	uint32_t crc = 0xffffffff;
	for (uint8_t b : bytes) {
		crc ^= b;
		for (int k = 0; k < 8; k++) {
			crc = crc >> 1 ^ (0xedb88320u & (0u - (crc & 1)));
		}
	}
	return ~crc;
}

struct Writer {
	std::vector<uint8_t> bytes;
	void u8(uint32_t v) { bytes.push_back(uint8_t(v)); }
	void u16(uint32_t v) { u8(v), u8(v >> 8); }
	void u32(uint32_t v) { u16(v), u16(v >> 16); }
	void text(std::u16string_view s)
	{
		u16(uint32_t(s.size()));
		for (char16_t c : s) {
			u16(c);
		}
	}
	void narrow(std::string_view s)
	{
		u16(uint32_t(s.size()));
		bytes.insert(bytes.end(), s.begin(), s.end());
	}
};

struct Reader {
	std::span<const uint8_t> bytes;
	size_t at = 0;
	bool ok = true;
	uint32_t u8()
	{
		ok = ok && at < bytes.size();
		return ok ? bytes[at++] : 0;
	}
	uint32_t u16() { return u8() | u8() << 8; }
	uint32_t u32() { return u16() | u16() << 16; }
	std::u16string text(size_t most)
	{
		const size_t n = u16();
		ok = ok && n <= most;
		std::u16string s;
		for (size_t i = 0; i < n && ok; i++) {
			s += char16_t(u16());
		}
		return s;
	}
	std::string narrow(size_t most)
	{
		const size_t n = u16();
		ok = ok && n <= most;
		std::string s;
		for (size_t i = 0; i < n && ok; i++) {
			s += char(u8());
		}
		return s;
	}
	// A value the format bounds: out of range makes the file malformed.
	uint32_t below(uint32_t value, uint32_t limit)
	{
		ok = ok && value < limit;
		return value;
	}
};

// A save's settings (Game::loadSlot's), each value kept in its range.
void readSettings(Reader& r, Settings& s)
{
	s.opponents = uint8_t(r.below(r.u8(), 6));
	s.laps = uint8_t(r.u8());
	r.below(s.laps - 1u, 5);
	s.music = uint8_t(r.u8()), s.sound = uint8_t(r.u8());
	s.stereo = uint8_t(r.below(r.u8(), 2));
	s.language = uint8_t(r.below(r.u8(), 9));
	s.kept = {uint8_t(r.u8()), uint8_t(r.u8())};
	for (uint8_t& v : s.device) {
		v = uint8_t(r.u8());
	}
	s.parts = uint8_t(r.u8()), s.circuits = uint8_t(r.u8()), s.races = uint16_t(r.u16());
	for (BestTime& b : s.best) {
		b.lapMs = r.u32(), b.lapHolder = r.text(14), b.raceMs = r.u32(), b.raceHolder = r.text(14);
	}
	for (size_t p = 0; p < 2; p++) {
		const uint32_t l = r.below(r.u8(), 4);
		s.picks[p] = {l == 1 ? RacerList::Card0 : l == 2 ? RacerList::Card1 : l == 3 ? RacerList::Defaults : RacerList::None, uint16_t(r.u16())};
		s.layout[p] = uint8_t(r.below(r.u8(), 5));
	}
	for (KeyLayout& k : s.layouts) {
		k.kind = uint8_t(r.u8()), k.subKind = uint8_t(r.u8()), k.device = uint8_t(r.u8());
		for (uint32_t& b : k.bindings) {
			b = r.u32();
		}
	}
}

// A save's racer (Game::loadSlot's), each value kept in its range.
void readRacer(Reader& r, RacerRecord& rec)
{
	rec.name = r.text(14);
	for (uint8_t& v : rec.looks) {
		v = uint8_t(r.u8());
	}
	rec.expression = uint8_t(r.below(r.u8(), 128));
	rec.stockCar = r.below(r.u8(), 2) != 0;
	rec.chassis = r.narrow(8);
	rec.pieces.resize(r.below(r.u8(), 65));
	for (CarPiece& p : rec.pieces) {
		p.type = uint16_t(r.u16()), p.x = uint8_t(r.u8()), p.y = uint8_t(r.u8()), p.rotation = uint8_t(r.u8());
		p.colour = uint8_t(r.u8()), p.part = uint16_t(r.u16());
	}
	for (uint8_t& t : rec.trophies) {
		t = uint8_t(r.below(r.u8(), 4));
	}
}

} // namespace

void Game::reset(uint8_t padCount)
{
	std::vector<RacerRecord> stock = std::move(defaults);
	*this = {};
	defaults = std::move(stock);
	pads = padCount;
	for (size_t l = 0; l < 5; l++) {
		KeyLayout& k = settings.layouts[l];
		if (joystickLayout(l)) {
			// A present pad: its buttons on slots 2.. (sub-kind 0: no axis buttons).
			k = {KeyLayout::Joystick, uint8_t(l < pads ? 0 : 2), uint8_t(l), {}};
			for (size_t s = 2; l < pads && s < 9; s++) {
				k.bindings[s] = 0x30000000u | uint32_t(s - 2);
			}
			continue;
		}
		// QUIRK: only the last keyboard layout gets the keyboard's sub-kind (0 anyway).
		k = {KeyLayout::Keyboard, 0, 0, {}};
		for (size_t s = 0; s < 9; s++) {
			k.bindings[s] = Keyboard | KeyboardDefaults[l - 2][s];
		}
	}
	fixAbsentLayouts();
}

uint16_t Game::draw() { return randomTable(random = (random + 1) & 1023); }

std::vector<RacerRecord>& Game::list(RacerList which) { return which == RacerList::Card0 ? cards[0] : which == RacerList::Card1 ? cards[1] : session; }

const RacerRecord* Game::racer(RacerRef ref) const
{
	const std::vector<RacerRecord>* l = ref.list == RacerList::Card0 ? &cards[0]
		: ref.list == RacerList::Card1								 ? &cards[1]
		: ref.list == RacerList::Session							 ? &session
		: ref.list == RacerList::Defaults							 ? &defaults
																	 : nullptr;
	return l && ref.index < l->size() ? &(*l)[ref.index] : nullptr;
}

void Game::markRacers(RacerList which)
{
	if (which == RacerList::Card0 || which == RacerList::Card1) {
		racersChanged[which == RacerList::Card1] = true;
	}
}

void Game::bind(size_t layout, size_t slot, uint32_t value)
{
	if (settings.layouts[layout].bindings[slot] == value) {
		return;
	}
	// A keyboard key is unbound everywhere else; anything else only in this layout.
	for (size_t l = 0; l < 5; l++) {
		if (l != layout && value >> 28 != 1) {
			continue;
		}
		for (uint32_t& b : settings.layouts[l].bindings) {
			b = b == value ? 0 : b;
		}
	}
	settings.layouts[layout].bindings[slot] = value;
	dirty = true;
}

size_t Game::spareKeyboard(size_t player) const
{
	const size_t other = settings.layout[1 - player];
	return other == 2 ? 3 : 2;
}

void Game::pickLayout(size_t player, size_t layout)
{
	settings.layout[player] = uint8_t(layout);
	dirty = true;
	const size_t other = 1 - player;
	if (settings.layout[other] != layout) {
		return;
	}
	for (size_t j = 0; j < 2; j++) {
		if (j != layout && j < pads) {
			settings.layout[other] = uint8_t(j);
			return;
		}
	}
	settings.layout[other] = uint8_t(spareKeyboard(other));
}

void Game::fixAbsentLayouts()
{
	for (size_t p = 0; p < 2; p++) {
		const size_t l = settings.layout[p];
		if (!joystickLayout(l) || l < pads) {
			continue;
		}
		size_t pick = spareKeyboard(p);
		for (size_t j = 0; j < 2; j++) {
			if (j != settings.layout[1 - p] && j < pads) {
				pick = j;
				break;
			}
		}
		settings.layout[p] = uint8_t(pick);
		dirty = true;
	}
}

void Game::applyCheat(std::u16string_view name)
{
	const std::array<std::string, 13>& Codes = executableTables().cheats;
	// What turning a code on turns off.
	static const uint32_t Clears[13] = {0, 0, CheatYellowBricks | CheatGreenBricks | CheatRedOnly, CheatRedBricks | CheatGreenBricks | CheatRedOnly,
		CheatRedBricks | CheatYellowBricks | CheatRedOnly, 0, CheatRedBricks | CheatYellowBricks | CheatGreenBricks | CheatMaxPower, CheatRedOnly, 0, 0, 0, 0, 0};
	std::string text;
	for (size_t i = 0; i < name.size() && i < 13; i++) {
		text += char(name[i] & 255);
	}
	for (uint32_t c = 0; c < 13; c++) {
		if (text != Codes[c]) {
			continue;
		}
		const uint32_t bit = 1u << c;
		if (bit == CheatNone) {
			cheats = 0;
			return;
		}
		if (cheats & bit) {
			cheats &= ~bit;
			return;
		}
		cheats = (cheats & ~Clears[c]) | bit;
		// The three hiding codes keep at most two on: the newest wins over the one before it.
		const uint32_t hide = CheatNoWheels | CheatNoBody | CheatNoDriver;
		if ((cheats & hide) == hide) {
			cheats &= ~(bit == CheatNoWheels ? CheatNoDriver : bit == CheatNoBody ? CheatNoWheels : CheatNoBody);
		}
		return;
	}
}

std::vector<uint8_t> Game::encodeSlot(size_t slot) const
{
	Writer w;
	const bool state = activeSlot == int8_t(slot);
	w.u8(state);
	if (state) {
		const Settings& s = settings;
		for (uint8_t v : {s.opponents, s.laps, s.music, s.sound, s.stereo, s.language, s.kept[0], s.kept[1]}) {
			w.u8(v);
		}
		for (uint8_t v : s.device) {
			w.u8(v);
		}
		w.u8(s.parts), w.u8(s.circuits), w.u16(s.races);
		for (const BestTime& b : s.best) {
			w.u32(b.lapMs), w.text(b.lapHolder), w.u32(b.raceMs), w.text(b.raceHolder);
		}
		for (size_t p = 0; p < 2; p++) {
			// Saved: which slot's racer (a session racer isn't saved: none).
			const RacerList l = s.picks[p].list;
			w.u8(l == RacerList::Card0 ? 1 : l == RacerList::Card1 ? 2 : l == RacerList::Defaults ? 3 : 0);
			w.u16(s.picks[p].index);
			w.u8(s.layout[p]);
		}
		for (const KeyLayout& k : s.layouts) {
			w.u8(k.kind), w.u8(k.subKind), w.u8(k.device);
			for (uint32_t b : k.bindings) {
				w.u32(b);
			}
		}
	}
	w.u16(uint32_t(cards[slot].size()));
	for (const RacerRecord& r : cards[slot]) {
		w.text(r.name);
		for (uint8_t v : r.looks) {
			w.u8(v);
		}
		w.u8(r.expression), w.u8(r.stockCar);
		w.narrow(r.chassis);
		w.u8(uint32_t(r.pieces.size()));
		for (const CarPiece& p : r.pieces) {
			w.u16(p.type), w.u8(p.x), w.u8(p.y), w.u8(p.rotation), w.u8(p.colour), w.u16(p.part);
		}
		for (uint8_t t : r.trophies) {
			w.u8(t);
		}
	}
	Writer file;
	file.u32(Magic), file.u32(Version), file.u32(uint32_t(w.bytes.size())), file.u32(crc32(w.bytes));
	file.bytes.insert(file.bytes.end(), w.bytes.begin(), w.bytes.end());
	return file.bytes;
}

bool Game::loadSlot(size_t slot, std::span<const uint8_t> bytes)
{
	Reader head{bytes};
	if (head.u32() != Magic || head.u32() != Version || !head.ok) {
		return false;
	}
	const uint32_t size = head.u32(), crc = head.u32();
	if (!head.ok || bytes.size() != 16 + uint64_t(size) || crc32(bytes.subspan(16)) != crc) {
		return false;
	}
	Reader r{bytes.subspan(16)};
	Settings s;
	const bool state = r.below(r.u8(), 2);
	if (state) {
		readSettings(r, s);
	}
	std::vector<RacerRecord> racers(r.below(r.u16(), CardCapacity + 1));
	for (RacerRecord& rec : racers) {
		if (!r.ok) {
			break;
		}
		readRacer(r, rec);
	}
	if (!r.ok || r.at != r.bytes.size()) {
		return false;
	}
	usable[slot] = true;
	racersChanged[slot] = false;
	cards[slot] = std::move(racers);
	if (state && activeSlot < 0) {
		// The stored controls are taken only for the devices as they are now.
		// (When a device changed, the players' layouts go back to the defaults.)
		const std::array<KeyLayout, 5> present = settings.layouts;
		bool changed = false;
		for (size_t l = 0; l < 5; l++) {
			const KeyLayout& stored = s.layouts[l];
			const bool same = stored.kind == present[l].kind && stored.subKind == present[l].subKind && stored.device == present[l].device;
			s.layouts[l] = same ? stored : present[l];
			changed |= !same;
		}
		if (changed) {
			s.layout = Settings().layout;
		}
		settings = s;
		activeSlot = int8_t(slot);
		dirty = false;
		fixAbsentLayouts();
	}
	return true;
}

std::vector<std::pair<uint8_t, std::vector<uint8_t>>> Game::openSlots()
{
	std::array<bool, 2> made = {};
	for (size_t slot : {size_t(1), size_t(0)}) {
		made[slot] = !usable[slot];
		if (made[slot]) {
			usable[slot] = true;
			cards[slot].clear();
		}
	}
	const bool activating = activeSlot < 0;
	if (activating) {
		activeSlot = 0;
	}
	// Only a slot just made is written (the active one with the settings).
	std::vector<std::pair<uint8_t, std::vector<uint8_t>>> files;
	for (size_t slot : {size_t(1), size_t(0)}) {
		if (made[slot]) {
			files.push_back({uint8_t(slot), encodeSlot(slot)});
			racersChanged[slot] = false;
		}
	}
	// QUIRK: the slot made active loads its settings back, and the controls' fallback marks them changed again.
	dirty = dirty || activating;
	return files;
}

std::vector<uint8_t> Game::saveSlot(size_t slot)
{
	if (!usable[slot]) {
		return {};
	}
	const bool state = dirty && activeSlot == int8_t(slot);
	if (!state && !racersChanged[slot]) {
		return {};
	}
	if (state) {
		dirty = false;
	}
	racersChanged[slot] = false;
	return encodeSlot(slot);
}

uint8_t volumeByte(int32_t step) { return uint8_t(float(step) * 0.05f * 255.0f); }
int32_t volumeStep(uint8_t byte) { return int32_t(float(byte) * 0.00392156886f * 20.0f + 0.5f); }

RacerRecord Game::racerFrom(const OriginalSave::Record& r)
{
	RacerRecord out;
	out.name = r.name;
	out.looks = r.looks;
	out.expression = r.expression;
	out.stockCar = r.stockCar;
	out.chassis = r.chassis;
	for (const OriginalSave::Piece& p : r.pieces) {
		out.pieces.push_back({p.type, p.x, p.y, p.rotation, p.colour, p.part});
	}
	out.trophies = r.trophies;
	return out;
}

std::array<std::vector<uint8_t>, 2> Game::fromOriginal(const std::array<std::vector<uint8_t>, 2>& files)
{
	Game from;
	from.reset(0);
	for (size_t slot = 0; slot < 2; slot++) {
		OriginalSave o;
		if (files[slot].empty() || !o.decodeFile(files[slot])) {
			continue;
		}
		if (from.activeSlot < 0) {
			Settings& s = from.settings;
			s.opponents = o.opponents, s.laps = o.laps, s.music = o.music, s.sound = o.sound, s.stereo = o.stereo, s.language = o.language;
			s.kept = o.kept, s.device = o.device, s.parts = o.parts, s.circuits = o.circuits, s.races = o.races;
			for (size_t i = 0; i < 13; i++) {
				s.best[i] = {o.best[i].lapMs, o.best[i].raceMs, o.best[i].lapHolder, o.best[i].raceHolder};
			}
			// A pick's list: 2 a card (its slot), 3 the stock racers.
			for (size_t p = 0; p < 2; p++) {
				const OriginalSave::Pick& k = o.picks[p];
				s.picks[p] = {k.list == 2 ? (k.slot ? RacerList::Card1 : RacerList::Card0) : k.list == 3 ? RacerList::Defaults : RacerList::None, k.record};
				s.layout[p] = k.layout;
			}
			for (size_t l = 0; l < 5; l++) {
				s.layouts[l] = {o.layouts[l].kind, o.layouts[l].subKind, o.layouts[l].device, o.layouts[l].bindings};
			}
			from.activeSlot = int8_t(slot);
		}
		for (const OriginalSave::Record& r : o.records) {
			from.cards[slot].push_back(racerFrom(r));
		}
	}
	return {from.encodeSlot(0), from.encodeSlot(1)};
}

} // namespace lr
