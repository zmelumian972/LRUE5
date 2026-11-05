#pragma once

// LEGO.JAM: the game's files in one archive. A tree of directories, each listing its files (name, offset, size) and
// subdirectories; names are 12-byte, NUL-padded. Paths here are "DIR/SUB/FILE.EXT", matched case-insensitively.

#include "LRData/Api.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lr {

class LRDATA_API Archive {
public:
	struct File {
		std::string path;
		uint32_t offset;
		uint32_t size;
	};

	// False if the bytes aren't an archive or its index runs past them. The archive views the bytes: keep them alive.
	[[nodiscard]] bool open(std::span<const uint8_t> bytes);

	const std::vector<File>& files() const { return m_files; }
	std::span<const uint8_t> read(const File& file) const { return m_bytes.subspan(file.offset, file.size); }
	const File* find(std::string_view path) const; // null if there's no such file
	std::span<const uint8_t> read(std::string_view path) const; // empty if there's no such file

private:
	[[nodiscard]] bool list(uint32_t at, const std::string& prefix, int depth);

	std::span<const uint8_t> m_bytes;
	std::vector<File> m_files;
};

// ASCII case, as the game's file and asset names compare.
inline std::string upper(std::string_view s)
{
	std::string u(s);
	for (char& c : u) {
		c = c >= 'a' && c <= 'z' ? char(c - 32) : c;
	}
	return u;
}
inline bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
	const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [&](char x, char y) { return lower(x) == lower(y); });
}
// Names as the scene files compare them: up to `most` characters, case-insensitively.
inline bool sameName(std::string_view a, std::string_view b, size_t most = 8) { return equalsIgnoreCase(a.substr(0, most), b.substr(0, most)); }

} // namespace lr
