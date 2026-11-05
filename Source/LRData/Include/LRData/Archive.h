#pragma once

// LEGO.JAM: the game's files in one archive. A tree of directories, each listing its files (name, offset, size) and
// subdirectories; names are 12-byte, NUL-padded. Paths here are "DIR/SUB/FILE.EXT", matched case-insensitively.

#include "LRData/Api.h"

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
	bool open(std::span<const uint8_t> bytes);

	const std::vector<File>& files() const { return m_files; }
	std::span<const uint8_t> read(const File& file) const { return m_bytes.subspan(file.offset, file.size); }
	const File* find(std::string_view path) const; // null if there's no such file
	std::span<const uint8_t> read(std::string_view path) const; // empty if there's no such file

private:
	bool list(uint32_t at, const std::string& prefix, int depth);

	std::span<const uint8_t> m_bytes;
	std::vector<File> m_files;
};

} // namespace lr
