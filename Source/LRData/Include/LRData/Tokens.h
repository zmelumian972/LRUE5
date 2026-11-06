#pragma once

// The token stream most of the game's data files use (.GDB, .MDB, .RRB, ...): typed values, braces and brackets, and
// keywords the file's format gives meaning to. On disk values come in several widths (integers of 8 to 32 bits; reals
// as floats or 8- and 16-bit fixed point) and can be compressed (a tag repeated, templates of tags); decode() expands
// it into a flat list of integers, reals, texts, brackets and keywords.

#include "LRData/Api.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace lr {

struct Token {
	enum Kind : uint8_t { Text, Real, Integer, Open, Close, OpenCount, CloseCount, Keyword };
	Kind kind;
	uint16_t keyword; // Keyword's tag
	union {
		int32_t integer;
		float real;
		uint32_t text; // offset into Tokens::text
	};
};

struct LRDATA_API Tokens {
	std::vector<Token> list;
	std::vector<char> text; // NUL-terminated strings

	// False on a malformed stream.
	[[nodiscard]] bool decode(std::span<const uint8_t> bytes);
	std::string_view textOf(const Token& token) const { return text.data() + token.text; }
};

// Decodes a token stream and reads it in order. A malformed stream or any mismatch sets failed() and later reads
// return zeros, so a reader checks once at the end.
class LRDATA_API TokenReader {
public:
	explicit TokenReader(std::span<const uint8_t> bytes)
		: m_failed(!m_tokens.decode(bytes))
	{
	}

	bool atEnd() const { return m_at >= m_tokens.list.size(); }
	bool failed() const { return m_failed; }
	void fail() { m_failed = true; }
	// The next token's keyword, or -1 if it isn't one (or at the end). Consumes it if it is.
	int keyword();
	bool next(Token::Kind kind); // consumes the token if it's that kind
	void expect(Token::Kind kind);
	int32_t integer();
	float real();
	float number(); // a real the file may write as an integer
	// x truncated to an integer; one that doesn't fit (NaN, infinite, too large) fails the reader and gives 0.
	int32_t truncated(float x);
	template <size_t N> std::array<float, N> reals()
	{
		std::array<float, N> v;
		for (float& x : v) {
			x = real();
		}
		return v;
	}
	std::string_view text();
	void skipValues(); // texts, reals and integers, up to anything else
	// "[ n ] {": a block of n items (the caller reads them, then close()).
	uint32_t openCounted();
	void close() { expect(Token::Close); }
	// "[n] { ... }": calls item() for each item while nothing has failed, then reads the "}". An item that returns
	// false fails the reader.
	template <typename Item> void counted(Item item)
	{
		const uint32_t count = openCounted();
		for (uint32_t i = 0; i < count && !m_failed; i++) {
			if (!call(item)) {
				m_failed = true;
			}
		}
		close();
	}
	// "[n] { K(key) ... }": as counted(), each item after its keyword.
	template <typename Item> void entries(int key, Item item)
	{
		counted([&] { return keyword() == key && call(item); });
	}
	// "K(key) [n] { K(key) ... }": a table, its entries keyed as it is.
	template <typename Item> void table(int key, Item item)
	{
		if (keyword() != key) {
			m_failed = true;
		}
		entries(key, item);
	}
	void skipCounted(); // a whole "[ n ] { ... }"
	void skipBlock(); // a whole "{ ... }", nested blocks included
	size_t position() const { return m_at; }
	void seek(size_t position) { m_at = position; }
	const Tokens& tokens() const { return m_tokens; }

private:
	template <typename F> static bool call(F& f)
	{
		if constexpr (std::is_void_v<decltype(f())>) {
			f();
			return true;
		}
		else {
			return f();
		}
	}
	const Token* take(Token::Kind kind);
	void skipToClose(); // past the "}" that closes the block just opened

	Tokens m_tokens;
	size_t m_at = 0;
	bool m_failed = false;
};

} // namespace lr
