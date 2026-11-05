#pragma once

// The small vector math the simulation runs on, all in 32-bit float as the original game computes it.
//
// Space: right-handed, Z up, lengths in feet, time in milliseconds. An orientation is a Basis of three rows: forward
// (local +x), left (local +y), up (local +z). Points are row vectors: world = x*forward + y*left + z*up + position.

#include "LRCore/Common/Api.h"

#include <array>
#include <cmath>

namespace lr {

struct Vec3 {
	float x = 0, y = 0, z = 0;
};

// From a plain array (as LRData reads them).
inline Vec3 vec(const std::array<float, 3>& a) { return {a[0], a[1], a[2]}; }

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { return a = a + b; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { return a = a - b; }
inline bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool isZero(Vec3 a) { return a.x == 0 && a.y == 0 && a.z == 0; }

inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float lengthSquared(Vec3 a) { return dot(a, a); }
// a.z*b.z + a.y*b.y + a.x*b.x: the order many of the game's dot products sum in.
inline float dotZYX(Vec3 a, Vec3 b) { return a.z * b.z + a.y * b.y + a.x * b.x; }
inline float lengthZYX(Vec3 a) { return std::sqrt(dotZYX(a, a)); }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
// Zero stays zero; otherwise times the reciprocal of the length.
inline Vec3 normalize(Vec3 a)
{
	const float len = length(a);
	return len == 0 ? Vec3{} : a * (1.0f / len);
}

// The game's sines and cosines: in double, rounded to float.
inline float sinf64(float a) { return float(std::sin(double(a))); }
inline float cosf64(float a) { return float(std::cos(double(a))); }

struct Basis {
	Vec3 f{1, 0, 0}, s{0, 1, 0}, u{0, 0, 1}; // forward, left, up
};

struct Pose {
	Vec3 position;
	Basis rotation;
};

struct Sphere {
	Vec3 centre;
	float radius = 0;
};

inline Vec3 toWorld(const Basis& b, Vec3 local) { return b.f * local.x + b.s * local.y + b.u * local.z; }
inline Vec3 toLocal(const Basis& b, Vec3 world) { return {dot(b.f, world), dot(b.s, world), dot(b.u, world)}; }

// Orthonormal bases from two vectors: the first is kept (normalized), the second made perpendicular to it.
inline Basis basisFromForwardUp(Vec3 forward, Vec3 up)
{
	Basis b;
	b.f = normalize(forward);
	b.u = normalize(up - b.f * dot(up, b.f));
	b.s = cross(b.u, b.f);
	return b;
}
inline Basis basisFromForwardSide(Vec3 forward, Vec3 side)
{
	Basis b;
	b.f = normalize(forward);
	b.s = normalize(side - b.f * dot(side, b.f));
	b.u = cross(b.f, b.s);
	return b;
}
inline Basis basisFromUpForward(Vec3 up, Vec3 forward)
{
	Basis b;
	b.u = normalize(up);
	b.f = normalize(forward - b.u * dot(forward, b.u));
	b.s = cross(b.u, b.f);
	return b;
}

// v turned by -angle about axis (right-hand rule), with the axis used as given: for a non-unit axis the result is
// scaled and sheared, and the callers normalize it.
inline Vec3 turnAbout(Vec3 v, Vec3 a, float angle)
{
	const float c = cosf64(angle), s = sinf64(angle), t = 1 - c;
	return {
		(a.x * a.x * t + c) * v.x + (a.x * a.y * t + a.z * s) * v.y + (a.x * a.z * t - a.y * s) * v.z,
		(a.x * a.y * t - a.z * s) * v.x + (a.y * a.y * t + c) * v.y + (a.y * a.z * t + a.x * s) * v.z,
		(a.x * a.z * t + a.y * s) * v.x + (a.y * a.z * t - a.x * s) * v.y + (a.z * a.z * t + c) * v.z,
	};
}

// A rotation as a quaternion (x, y, z, w).
struct Quat {
	float x = 0, y = 0, z = 0, w = 1;
};

// The rows of q's rotation; q needn't be unit (the scale 2/|q|^2 renormalizes it).
inline Basis basisOf(Quat q)
{
	const float s = 2 / (q.w * q.w + q.z * q.z + q.y * q.y + q.x * q.x);
	const float sx = s * q.x, sy = q.y * s, sz = s * q.z;
	const float wx = q.w * sx, wy = q.w * sy, wz = q.w * sz;
	const float xx = sx * q.x, xy = sy * q.x, xz = sz * q.x, yy = q.y * sy, yz = q.y * sz, zz = sz * q.z;
	return {{1 - (zz + yy), xy - wz, xz + wy}, {xy + wz, 1 - (zz + xx), yz - wx}, {xz - wy, yz + wx, 1 - (yy + xx)}};
}

// From a toward b by t, along the shorter way; not normalized.
inline Quat blend(Quat a, Quat b, float t)
{
	const float d = a.y * b.y + a.z * b.z + a.w * b.w + a.x * b.x;
	if (d > 0) {
		return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
	}
	return {a.x - (a.x + b.x) * t, a.y - (a.y + b.y) * t, a.z - (a.z + b.z) * t, a.w - (a.w + b.w) * t};
}

// The game's 1024-entry angle tables. Their steps (about 0.0625 rad near d = +-1 for the arc cosine) show in the
// karts' headings, so they're kept rather than computing acos and cos exactly.
LRCORE_API float tableAcos(float d);     // acos(d), from d in [-1, 1]
LRCORE_API float tableCos(float angle); // cos(angle), any angle >= 0
LRCORE_API float cosEntry(int32_t index); // the cosine table's entry index & 1023: cos(2*pi*index/1024)

} // namespace lr
