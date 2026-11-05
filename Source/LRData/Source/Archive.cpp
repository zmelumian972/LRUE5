#include "LRData/Archive.h"

#include "Bytes.h"

namespace lr {

namespace {

constexpr uint32_t NameSize = 12;
constexpr uint32_t FileRecord = NameSize + 8; // name, offset, size
constexpr uint32_t FolderRecord = NameSize + 4; // name, offset of its listing

std::string nameAt(std::span<const uint8_t> bytes, uint32_t at)
{
	std::string name;
	for (uint32_t i = 0; i < NameSize && bytes[at + i]; i++) {
		name += char(bytes[at + i]);
	}
	return name;
}

} // namespace

bool Archive::open(std::span<const uint8_t> bytes)
{
	m_bytes = bytes;
	m_files.clear();
	return bytes.size() >= 8 && bytes[0] == 'L' && bytes[1] == 'J' && bytes[2] == 'A' && bytes[3] == 'M' && list(4, "", 0);
}

// A listing: its files (count, records), then its folders (count, records). A folder without files skips straight to
// its folders, but still has the file count (0) first.
bool Archive::list(uint32_t at, const std::string& prefix, int depth)
{
	if (depth > 16 || !fits(m_bytes, at, 4)) {
		return false;
	}
	const uint32_t fileCount = u32(m_bytes, at);
	at += 4;
	if (!fits(m_bytes, at, uint64_t(fileCount) * FileRecord + 4)) {
		return false;
	}
	for (uint32_t i = 0; i < fileCount; i++, at += FileRecord) {
		File file{prefix + nameAt(m_bytes, at), u32(m_bytes, at + NameSize), u32(m_bytes, at + NameSize + 4)};
		if (!fits(m_bytes, file.offset, file.size)) {
			return false;
		}
		m_files.push_back(std::move(file));
	}
	const uint32_t folderCount = u32(m_bytes, at);
	at += 4;
	if (!fits(m_bytes, at, uint64_t(folderCount) * FolderRecord)) {
		return false;
	}
	for (uint32_t i = 0; i < folderCount; i++, at += FolderRecord) {
		if (!list(u32(m_bytes, at + NameSize), prefix + nameAt(m_bytes, at) + "/", depth + 1)) {
			return false;
		}
	}
	return true;
}

const Archive::File* Archive::find(std::string_view path) const
{
	for (const File& file : m_files) {
		if (equalsIgnoreCase(file.path, path)) {
			return &file;
		}
	}
	return nullptr;
}

std::span<const uint8_t> Archive::read(std::string_view path) const
{
	const File* file = find(path);
	return file ? read(*file) : std::span<const uint8_t>();
}

} // namespace lr
