#pragma once

// The data's indices are signed, as the game stores them (int16_t, int32_t; -1 or less: none). List and Array are a
// std::vector and a std::array that take them as they are. A negative one is the caller's bug (checked in debug
// builds): it tests first, with inRange or a >= 0.

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

namespace lr {

constexpr size_t toIndex(std::integral auto i)
{
	assert(std::cmp_greater_equal(i, 0));
	return size_t(i);
}

template <class T> struct List : std::vector<T> {
	using std::vector<T>::vector;
	using std::vector<T>::operator=;
	constexpr decltype(auto) operator[](std::integral auto i) { return std::vector<T>::operator[](toIndex(i)); }
	constexpr decltype(auto) operator[](std::integral auto i) const { return std::vector<T>::operator[](toIndex(i)); }
};

template <class T, size_t N> struct Array : std::array<T, N> {
	constexpr T& operator[](std::integral auto i) { return std::array<T, N>::operator[](toIndex(i)); }
	constexpr const T& operator[](std::integral auto i) const { return std::array<T, N>::operator[](toIndex(i)); }
};

template <class C> bool inRange(const C& c, long long i) { return i >= 0 && size_t(i) < c.size(); }
// The element at i, nullptr out of range.
template <class C> auto itemAt(C& c, long long i) { return inRange(c, i) ? &c[size_t(i)] : nullptr; }

} // namespace lr
