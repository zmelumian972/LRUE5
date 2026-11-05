#include "LRData/Tokens.h"

#include "Bytes.h"

#include <array>
#include <cstring>

namespace lr {

namespace {

// The tags on disk. Any other tag is a keyword (0x13 introduces a 16-bit one).
enum : uint16_t {
	TagText = 0x02,
	TagReal = 0x03,
	TagInt32 = 0x04,
	TagOpen = 0x05,
	TagClose = 0x06,
	TagOpenCount = 0x07,
	TagCloseCount = 0x08,
	TagUInt8 = 0x0b, // 0x0b and 0x0c are both unsigned bytes (0x0b isn't signed, though it looks meant to be)
	TagUInt8b = 0x0c,
	TagInt16 = 0x0d,
	TagUInt16 = 0x0e,
	TagReal16_4096 = 0x0f, // reals as int16 / 4096
	TagReal16_32 = 0x10, // int16 / 32
	TagReal16 = 0x11, // int16
	TagReal8_127 = 0x12, // uint8 / 127
	TagWide = 0x13, // the next two bytes are the tag
	TagRepeat = 0x14, // count (uint16), a tag: that tag, count times (each with its own payload)
	TagCount = 0x15, // "[ int32 ]" and one more "]", in four tags
	TagDefine = 0x16, // a template tag, a field count, the field tags
	TagFirstTemplate = 0x17, // ..0x26: a defined template, expanding to its field tags
	TagLastTemplate = 0x26,
};

class Decoder {
public:
	Decoder(std::span<const uint8_t> bytes, Tokens& out)
		: m_bytes(bytes)
		, m_out(out)
	{
	}

	bool run()
	{
		while (m_at < m_bytes.size() && m_ok) {
			emit(readTag());
		}
		return m_ok;
	}

private:
	bool need(uint64_t size) { return m_ok = m_ok && fits(m_bytes, m_at, size); }

	uint16_t readTag()
	{
		if (!need(1)) {
			return 0;
		}
		const uint8_t tag = m_bytes[m_at++];
		if (tag != TagWide || !need(2)) {
			return tag;
		}
		m_at += 2;
		return u16(m_bytes, m_at - 2);
	}

	void push(Token::Kind kind, uint32_t bits, uint16_t keyword = 0)
	{
		Token token{kind, keyword, {}};
		token.text = bits; // the union's 32 bits, whichever member
		m_out.list.push_back(token);
	}
	void pushReal(float real)
	{
		uint32_t bits;
		std::memcpy(&bits, &real, 4);
		push(Token::Real, bits);
	}

	// One tag and its payload.
	void emit(uint16_t tag)
	{
		if (!m_ok) {
			return;
		}
		switch (tag) {
		case TagText: emitText(); return;
		case TagReal:
		case TagInt32:
			if (need(4)) {
				push(tag == TagReal ? Token::Real : Token::Integer, u32(m_bytes, m_at));
				m_at += 4;
			}
			return;
		case TagOpen: push(Token::Open, 0); return;
		case TagClose: push(Token::Close, 0); return;
		case TagOpenCount: push(Token::OpenCount, 0); return;
		case TagCloseCount: push(Token::CloseCount, 0); return;
		case TagUInt8:
		case TagUInt8b:
		case TagReal8_127:
			if (need(1)) {
				const uint8_t byte = m_bytes[m_at++];
				if (tag == TagReal8_127) {
					pushReal((1.0f / 127.0f) * byte);
				}
				else {
					push(Token::Integer, byte);
				}
			}
			return;
		case TagInt16:
		case TagUInt16:
		case TagReal16_4096:
		case TagReal16_32:
		case TagReal16:
			if (need(2)) {
				const uint16_t bits = u16(m_bytes, m_at);
				m_at += 2;
				switch (tag) {
				case TagInt16: push(Token::Integer, uint32_t(int32_t(int16_t(bits)))); break;
				case TagUInt16: push(Token::Integer, bits); break;
				case TagReal16_4096: pushReal((1.0f / 4096.0f) * int16_t(bits)); break;
				case TagReal16_32: pushReal((1.0f / 32.0f) * int16_t(bits)); break;
				default: pushReal(int16_t(bits)); break;
				}
			}
			return;
		case TagRepeat:
			if (need(2)) {
				const uint16_t count = u16(m_bytes, m_at);
				m_at += 2;
				const uint16_t repeated = readTag();
				for (uint32_t i = 0; i < count && m_ok; i++) {
					emit(repeated);
				}
			}
			return;
		case TagCount: emitCount(); return;
		case TagDefine: emitDefine(); return;
		default:
			if (tag >= TagFirstTemplate && tag <= TagLastTemplate) {
				for (uint16_t field : m_templates[tag - TagFirstTemplate]) {
					emit(field);
				}
			}
			else {
				push(Token::Keyword, 0, tag);
			}
		}
	}

	// A zero-ended text: kept with its zero.
	void emitText()
	{
		const uint8_t* start = m_bytes.data() + m_at;
		const void* end = std::memchr(start, 0, m_bytes.size() - m_at);
		if (!end) {
			m_ok = false;
			return;
		}
		const size_t length = static_cast<const uint8_t*>(end) - start;
		push(Token::Text, uint32_t(m_out.text.size()));
		m_out.text.insert(m_out.text.end(), start, start + length + 1);
		m_at += length + 1;
	}
	// A template's definition: which template, then its fields' tags (emitted where the template's tag is).
	void emitDefine()
	{
		if (!need(2)) {
			return;
		}
		const uint8_t which = m_bytes[m_at], fieldCount = m_bytes[m_at + 1];
		m_at += 2;
		if (which < TagFirstTemplate || which > TagLastTemplate) {
			m_ok = false;
			return;
		}
		std::vector<uint16_t>& fields = m_templates[which - TagFirstTemplate];
		fields.clear();
		for (uint32_t i = 0; i < fieldCount && m_ok; i++) {
			fields.push_back(readTag());
		}
	}
	void emitCount()
	{
		emit(TagOpenCount);
		emit(TagInt32);
		emit(TagCloseCount);
		emit(TagCloseCount);
	}

	std::span<const uint8_t> m_bytes;
	Tokens& m_out;
	size_t m_at = 0;
	bool m_ok = true;
	std::array<std::vector<uint16_t>, TagLastTemplate - TagFirstTemplate + 1> m_templates;
};

} // namespace

bool Tokens::decode(std::span<const uint8_t> bytes)
{
	list.clear();
	text.clear();
	return Decoder(bytes, *this).run();
}

const Token* TokenReader::take(Token::Kind kind)
{
	if (m_failed || atEnd() || m_tokens.list[m_at].kind != kind) {
		m_failed = true;
		return nullptr;
	}
	return &m_tokens.list[m_at++];
}

int TokenReader::keyword()
{
	if (m_failed || atEnd() || m_tokens.list[m_at].kind != Token::Keyword) {
		return -1;
	}
	return m_tokens.list[m_at++].keyword;
}

bool TokenReader::next(Token::Kind kind)
{
	if (m_failed || atEnd() || m_tokens.list[m_at].kind != kind) {
		return false;
	}
	m_at++;
	return true;
}

void TokenReader::expect(Token::Kind kind) { take(kind); }

int32_t TokenReader::integer()
{
	const Token* token = take(Token::Integer);
	return token ? token->integer : 0;
}

float TokenReader::real()
{
	const Token* token = take(Token::Real);
	return token ? token->real : 0.0f;
}

void TokenReader::skipValues()
{
	while (next(Token::Text) || next(Token::Real) || next(Token::Integer)) {
	}
}

std::string_view TokenReader::text()
{
	const Token* token = take(Token::Text);
	return token ? m_tokens.textOf(*token) : std::string_view();
}

uint32_t TokenReader::openCounted()
{
	expect(Token::OpenCount);
	const int32_t count = integer();
	expect(Token::CloseCount);
	expect(Token::Open);
	if (count < 0) {
		m_failed = true;
	}
	return m_failed ? 0 : uint32_t(count);
}

void TokenReader::skipBlock()
{
	expect(Token::Open);
	skipToClose();
}

void TokenReader::skipCounted()
{
	openCounted();
	skipToClose();
}

void TokenReader::skipToClose()
{
	for (int depth = 1; depth > 0 && !m_failed; m_at++) {
		if (atEnd()) {
			m_failed = true;
			return;
		}
		const Token::Kind kind = m_tokens.list[m_at].kind;
		if (kind == Token::Open) {
			depth++;
		}
		else if (kind == Token::Close) {
			depth--;
		}
	}
}

} // namespace lr
