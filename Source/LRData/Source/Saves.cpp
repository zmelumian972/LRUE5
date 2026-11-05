#include "LRData/Saves.h"

#include "Bytes.h"

namespace lr {

namespace {

constexpr size_t BlockData = 127, Block = 128, RecordCapacity = 100;

uint16_t u16be(std::span<const uint8_t> b, size_t at) { return uint16_t(b[at] << 8 | b[at + 1]); }

std::u16string name14(std::span<const uint8_t> b, size_t at)
{
	std::u16string name;
	for (size_t i = 0; i < 14; i++) {
		const char16_t c = u16(b, at + 2 * i);
		if (!c) {
			break;
		}
		name += c;
	}
	return name;
}

} // namespace

bool OriginalSave::unblock(std::span<const uint8_t> file, std::vector<uint8_t>& data)
{
	data.clear();
	if (file.size() % Block) {
		return false;
	}
	for (size_t at = 0; at < file.size(); at += Block) {
		uint8_t sum = 0;
		for (size_t i = 0; i < BlockData; i++) {
			sum = uint8_t(sum + file[at + i]);
		}
		if (sum != file[at + BlockData]) {
			return false;
		}
		data.insert(data.end(), file.begin() + at, file.begin() + at + BlockData);
	}
	return true;
}

bool OriginalSave::decode(std::span<const uint8_t> b, bool padded)
{
	*this = {};
	if (!fits(b, 0, HeaderSize) || b[0] != 'L' || b[1] != 'R' || b[2] > RecordCapacity) {
		return false;
	}
	const size_t count = b[2];
	parts = b[5];
	circuits = b[6];
	races = u16(b, 7);
	for (size_t i = 0; i < best.size(); i++) {
		const size_t at = 9 + i * 0x40;
		best[i] = {u32(b, at), u32(b, at + 0x20), name14(b, at + 4), name14(b, at + 0x24)};
	}
	opponents = b[0x349];
	for (size_t i = 0; i < 16; i++) {
		device[i] = b[0x34a + i];
	}
	kept = {b[0x35a], b[0x35b]};
	music = b[0x35c];
	sound = b[0x35d];
	stereo = b[0x35e];
	language = b[0x35f];
	laps = b[0x360];
	for (size_t p = 0; p < 2; p++) {
		const size_t at = 0x361 + p * 4;
		picks[p] = {b[at], b[at + 1], b[at + 2], b[at + 3]};
	}
	for (size_t l = 0; l < 5; l++) {
		const size_t at = 0x369 + l * 0x27;
		layouts[l].kind = b[at];
		layouts[l].subKind = b[at + 1];
		layouts[l].device = b[at + 2];
		for (size_t k = 0; k < 9; k++) {
			layouts[l].bindings[k] = u32(b, at + 3 + 4 * k);
		}
	}
	// The records follow the header (9 blocks), in a file each on whole blocks (5: 635 bytes, 557 used).
	const size_t recordSpan = padded ? (RecordSize + BlockData - 1) / BlockData * BlockData : RecordSize;
	const size_t first = HeaderSize;
	for (size_t r = 0; r < count; r++) {
		const size_t at = first + r * recordSpan;
		if (!fits(b, at, RecordSize)) {
			return false;
		}
		Record& rec = records.emplace_back();
		rec.name = name14(b, at);
		for (size_t i = 0; i < 4; i++) {
			rec.looks[i] = b[at + 0x1c + i];
		}
		rec.stockCar = b[at + 0x20] & 0x80;
		rec.expression = b[at + 0x20] & 0x7f;
		for (size_t i = 0; i < 8 && b[at + 0x21 + i]; i++) {
			rec.chassis += char(b[at + 0x21 + i]);
		}
		const size_t pieces = u16be(b, at + 0x29);
		if (pieces > 64) {
			return false;
		}
		for (size_t i = 0; i < pieces; i++) {
			const size_t p = at + 0x2b + i * 8;
			rec.pieces.push_back({u16be(b, p), b[p + 2], b[p + 3], b[p + 4], b[p + 5], u16be(b, p + 6)});
		}
		const uint16_t trophies = u16(b, at + 0x22b);
		for (size_t c = 0; c < 7; c++) {
			rec.trophies[c] = uint8_t(trophies >> (2 * c) & 3);
		}
	}
	return true;
}

bool OriginalSave::decodeFile(std::span<const uint8_t> file)
{
	std::vector<uint8_t> data;
	return unblock(file, data) && decode(data, true);
}

} // namespace lr
