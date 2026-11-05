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

// Reads a Tokens list in order. Any mismatch sets failed() and later reads return zeros, so a reader checks once at
// the end.
class LRDATA_API TokenReader {
public:
	explicit TokenReader(const Tokens& tokens)
		: m_tokens(tokens)
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
	void skipCounted(); // a whole "[ n ] { ... }"
	void skipBlock(); // a whole "{ ... }", nested blocks included
	size_t position() const { return m_at; }
	void seek(size_t position) { m_at = position; }
	const Tokens& tokens() const { return m_tokens; }

private:
	const Token* take(Token::Kind kind);
	void skipToClose(); // past the "}" that closes the block just opened

	const Tokens& m_tokens;
	size_t m_at = 0;
	bool m_failed = false;
};

} // namespace lr
