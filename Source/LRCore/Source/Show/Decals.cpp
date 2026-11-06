// The show's decals: the cars' shadows, the skid trails, explosion scars and oil slicks, each frame
// projected onto the track's collision triangles (found through a grid of cells).

#include "LRCore/Show/ShowState.h"

#include "LRCore/Race/World.h"
#include "Powers/PowerParts.h"

#include <algorithm>
#include <cmath>

namespace lr {

namespace {

// The wheels a racer's skid marks come from: all four spinning, the back two on a turbo, else one side's pair, by
// which way the car slides; a wheel off the ground for over 150 ms lays none.
uint8_t skiddingWheels(const World& world, size_t i, Effects::Visuals& v, int32_t ms)
{
	const Kart& k = world.racers.karts[i];
	const Pose& car = world.racers.visible[i];
	uint8_t wheels = 0;
	if (v.skidding && !(world.racers.race[i].flags & RacerGhost)) {
		if (v.spinning) {
			wheels = 15;
		}
		else if (v.turbo) {
			wheels = 4 | 8;
		}
		else {
			const Vec3 dir = k.velocityDirection;
			wheels = dot(car.rotation.s, dir) > 0 ? (2 | 8) : (1 | 4);
		}
		for (size_t w = 0; w < 4; w++) {
			v.liftedMs[w] = k.wheels[w].contact ? 0 : v.liftedMs[w] + ms;
			if (v.liftedMs[w] > 150) {
				wheels &= uint8_t(~(1u << w));
			}
		}
	}
	return wheels;
}

} // namespace

void Decals::load(const World& world)
{
	shadowAlpha.assign(world.racers.karts.size(), {});
	scarMs.assign(world.powerUps.explosions.size(), -1);
	groundCells(world);
}

// The ground the decals lie on: the track's collision triangles in the world, in cells of 16 ft.
void Decals::groundCells(const World& world)
{
	if (world.track.trackVolume < world.track.volumes.size()) {
		const Track::Volume& v = world.track.volumes[world.track.trackVolume];
		if (v.mesh < world.track.meshes.size()) {
			for (const CollisionMesh::Triangle& t : world.track.meshes[v.mesh].triangles) {
				Triangle g;
				for (int c = 0; c < 3; c++) {
					g.v[c] = toWorld(v.rotation, t.v[c]) + v.position;
				}
				m_ground.push_back(g);
			}
		}
	}
	if (!m_ground.empty()) {
		float maxX = -1e30f, maxY = -1e30f;
		m_minX = m_minY = 1e30f;
		for (const Triangle& t : m_ground) {
			for (const Vec3& p : t.v) {
				m_minX = std::min(m_minX, p.x), m_minY = std::min(m_minY, p.y);
				maxX = std::max(maxX, p.x), maxY = std::max(maxY, p.y);
			}
		}
		m_columns = int32_t((maxX - m_minX) / m_cell) + 1;
		m_rows = int32_t((maxY - m_minY) / m_cell) + 1;
		m_cells.assign(size_t(m_columns) * size_t(m_rows), {});
		m_taken.assign(m_ground.size(), 0);
		for (uint32_t i = 0; i < m_ground.size(); i++) {
			const Triangle& t = m_ground[i];
			const float x0 = std::min({t.v[0].x, t.v[1].x, t.v[2].x}), x1 = std::max({t.v[0].x, t.v[1].x, t.v[2].x});
			const float y0 = std::min({t.v[0].y, t.v[1].y, t.v[2].y}), y1 = std::max({t.v[0].y, t.v[1].y, t.v[2].y});
			for (int32_t cy = int32_t((y0 - m_minY) / m_cell); cy <= int32_t((y1 - m_minY) / m_cell); cy++) {
				for (int32_t cx = int32_t((x0 - m_minX) / m_cell); cx <= int32_t((x1 - m_minX) / m_cell); cx++) {
					m_cells[size_t(cy) * size_t(m_columns) + size_t(cx)].push_back(i);
				}
			}
		}
	}
}

void Decals::project(Decal& d, Vec3 centre, Vec3 along, float width, float length) const
{
	d.points.clear();
	d.uvs.clear();
	if (m_cells.empty()) {
		return;
	}
	along.z = 0;
	along = isZero(along) ? Vec3{1, 0, 0} : normalize(along);
	const Vec3 across = {-along.y, along.x, 0};
	const float hl = length * 0.5f, hw = width * 0.5f;
	const float reach = std::sqrt(hl * hl + hw * hw);
	const float top = centre.z, bottom = centre.z - 15; // the box: centred above the ground point, 15 deep
	// The triangles under the box, clipped to its four sides (in its own frame: u along the length, v across).
	const uint32_t projection = ++m_projection; // (a triangle in several cells is taken once)
	const int32_t cx0 = std::max(0, int32_t((centre.x - reach - m_minX) / m_cell)),
				  cx1 = std::min(m_columns - 1, int32_t((centre.x + reach - m_minX) / m_cell));
	const int32_t cy0 = std::max(0, int32_t((centre.y - reach - m_minY) / m_cell)),
				  cy1 = std::min(m_rows - 1, int32_t((centre.y + reach - m_minY) / m_cell));
	for (int32_t cy = cy0; cy <= cy1; cy++) {
		for (int32_t cx = cx0; cx <= cx1; cx++) {
			for (uint32_t t : m_cells[size_t(cy) * size_t(m_columns) + size_t(cx)]) {
				if (m_taken[t] == projection) {
					continue;
				}
				m_taken[t] = projection;
				const Triangle& tri = m_ground[t];
				const Vec3 nrm = cross(tri.v[1] - tri.v[0], tri.v[2] - tri.v[0]);
				if (std::fabs(nrm.z) <= 0.1f * std::sqrt(lengthSquared(nrm))) {
					continue; // (a wall: nothing lies on it; the collision mesh winds either way)
				}
				// In the box's frame.
				std::vector<std::array<float, 3>> poly; // u, v, z
				for (const Vec3& p : tri.v) {
					const Vec3 o = p - centre;
					poly.push_back({dot(o, along), dot(o, across), p.z});
				}
				const auto clip = [&](size_t axis, float limit, float sign) {
					std::vector<std::array<float, 3>> out;
					for (size_t k = 0; k < poly.size(); k++) {
						const std::array<float, 3>& a = poly[k];
						const std::array<float, 3>& b = poly[(k + 1) % poly.size()];
						const float da = sign * a[axis] - limit, db = sign * b[axis] - limit;
						if (da <= 0) {
							out.push_back(a);
						}
						if ((da < 0) != (db < 0) && da != db) {
							const float f = da / (da - db);
							out.push_back({a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f});
						}
					}
					poly = out;
				};
				clip(0, hl, 1), clip(0, hl, -1), clip(1, hw, 1), clip(1, hw, -1);
				if (poly.size() < 3) {
					continue;
				}
				bool inside = false;
				for (const std::array<float, 3>& q : poly) {
					inside = inside || (q[2] <= top + 1e-3f && q[2] >= bottom);
				}
				if (!inside) {
					continue;
				}
				for (size_t k = 1; k + 1 < poly.size(); k++) {
					for (const std::array<float, 3>* q : {&poly[0], &poly[k], &poly[k + 1]}) {
						d.points.push_back(centre + along * (*q)[0] + across * (*q)[1] + Vec3{0, 0, (*q)[2] - centre.z});
						d.uvs.push_back({(*q)[1] / width + 0.5f, (*q)[0] / length + 0.5f});
					}
				}
			}
		}
	}
}

void Decals::update(Show& show, const World& world, int32_t ms)
{
	shown.clear();
	carShadows(show, world);
	skidMarks(show, world, ms);
	trailDecals(show, ms);
	groundMarks(world);
}

void Decals::carShadows(const Show& show, const World& world)
{
	const size_t n = world.racers.karts.size();
	// The cars' shadows: at the car's bounds centre, 6 up, 8 x 9 (the chassis') times its scale, along its -row 1;
	// alpha per camera by the eye's distance.
	for (size_t i = 0; i < n; i++) {
		shadowAlpha[i] = {0, 0};
		if (world.racers.race[i].flags & RacerGhost) {
			continue;
		}
		const Pose& car = world.racers.visible[i];
		const float scale = world.racers.powers.empty() ? 1.0f : world.racers.powers[i].carScale;
		const Vec3 centre = toWorld(car.rotation, world.racers.carBounds[i].centre) + car.position;
		bool any = false;
		for (size_t c = 0; c < world.raceView.cameras.size() && c < 2; c++) {
			const Vec3 d = world.raceView.cameras[c].shownEye() - centre;
			const float d2 = lengthSquared(d);
			float a = 0;
			if (d2 < 2500) {
				a = 191;
			}
			else if (d2 < 10000) {
				a = 191 - ((d2 - 2500) / 7500) * 191;
			}
			shadowAlpha[i][c] = uint8_t(std::clamp(a, 0.0f, 255.0f));
			any = any || shadowAlpha[i][c] > 0;
		}
		if (!any) {
			continue;
		}
		Decal& dcl = shown.emplace_back();
		dcl.kind = Decal::Shadow;
		dcl.owner = int16_t(i);
		dcl.material = "carshad" + std::to_string(i % 6);
		const Show::Chassis& ch = show.chassis[i];
		// The shadow's length along the car's forward (its -row 1 is its width's direction).
		project(dcl, centre + Vec3{0, 0, 6}, car.rotation.f, ch.shadowWidth * scale, ch.shadowLength * scale);
	}
}

// Skid marks (players only): which wheels, laying pieces.
void Decals::skidMarks(Show& show, const World& world, int32_t ms)
{
	for (size_t i = 0; i < world.racers.karts.size(); i++) {
		Effects::Visuals& v = show.effects.visuals[i];
		v.skidWheels = skiddingWheels(world, i, v, ms);
		for (uint8_t w = 0; w < 4; w++) {
			layTrail(show, world, i, w, ms);
		}
	}
}

// The trail of a racer's wheel: released or set fading when the wheel stops laying or the mode changes, else a piece
// laid from its anchor to the wheel.
void Decals::layTrail(const Show& show, const World& world, size_t i, uint8_t w, int32_t ms)
{
	const Effects::Visuals& v = show.effects.visuals[i];
	const Pose& car = world.racers.visible[i];
	const uint8_t wheels = v.skidWheels;
	// The trail of this racer's wheel, if one lays it.
	Trail* t = nullptr;
	for (Trail& x : trails) {
		t = x.racer == int16_t(i) && x.wheel == w && !x.fading ? &x : t;
	}
	const bool on = wheels & (1u << w);
	const bool turbo = v.turbo && world.racers.race[i].player;
	if (t && (!on || t->spinning != v.spinning || t->turbo != turbo)) {
		if (on) {
			*t = {}; // (the mode changed: released at once)
		}
		else {
			t->fading = true; // (skid effects stopped: it fades)
			t->fadeMs = 0;
		}
		t = nullptr;
	}
	if (!on) {
		return;
	}
	const Vec3 at = toWorld(car.rotation, show.chassis[i].skidPoints[w]) + car.position;
	if (!t) {
		for (Trail& x : trails) {
			if (x.racer < 0) {
				t = &x;
				break;
			}
		}
		if (!t) {
			return;
		}
		*t = {};
		t->racer = int16_t(i), t->wheel = w, t->spinning = v.spinning, t->turbo = turbo;
		t->anchor = at;
	}
	const int32_t length = t->spinning ? 250 : 1000;
	for (Trail::Piece& p : t->pieces) {
		p.ageMs += ms;
	}
	t->pieces.erase(std::remove_if(t->pieces.begin(), t->pieces.end(), [&](const Trail::Piece& p) { return p.ageMs > length; }),
		t->pieces.end());
	if (lengthSquared(at - t->anchor) >= 0.01f) {
		// A new piece from the anchor to the wheel; the live one is re-laid each frame.
		if (t->live) {
			t->pieces.push_back(t->current);
			if (t->pieces.size() > 3) {
				t->pieces.erase(t->pieces.begin());
			}
		}
		t->current = {t->anchor, at, 0};
		t->live = true;
		t->anchor = at;
	}
}

// The trails' decals, fading over a second once their wheels stop.
void Decals::trailDecals(const Show& show, int32_t ms)
{
	for (Trail& t : trails) {
		if (t.racer < 0) {
			continue;
		}
		uint8_t alpha = 127;
		if (t.fading) {
			t.fadeMs += ms;
			if (t.fadeMs >= 1000) {
				t = {};
				continue;
			}
			alpha = uint8_t((1 - float(t.fadeMs) / 1000) * 127);
		}
		const float width = show.chassis[t.racer].skidWidths[t.wheel < 2 ? 0 : 1];
		std::vector<Trail::Piece> pieces = t.pieces;
		if (t.live) {
			pieces.push_back(t.current);
		}
		for (const Trail::Piece& p : pieces) {
			const Vec3 step = p.to - p.from;
			const float length = std::sqrt(step.x * step.x + step.y * step.y);
			if (length <= 0) {
				continue;
			}
			Decal& dcl = shown.emplace_back();
			dcl.kind = Decal::Skid;
			dcl.owner = int16_t(&t - trails.data());
			dcl.material = t.turbo ? "trbskid" : "skid";
			dcl.alpha = alpha;
			project(dcl, (p.from + p.to) * 0.5f + Vec3{0, 0, 6}, step, width, length);
		}
	}
}

// The marks things leave on the ground.
void Decals::groundMarks(const World& world)
{
	// Explosion scars: the flash's width, 180 while exploding, then down to 0 over 5 s.
	for (size_t e = 0; e < world.powerUps.explosions.size(); e++) {
		const Explosion& x = world.powerUps.explosions[e];
		if (!x.scar || x.state == Explosion::Idle) {
			continue;
		}
		const float g = x.state == Explosion::Exploding ? x.growth : 1.0f;
		const float size = (15 - 0.1f) * g + 0.1f;
		const float alpha = x.state == Explosion::Exploding ? 180 : 180 * std::clamp(float(x.ms) / 5000, 0.0f, 1.0f);
		Decal& dcl = shown.emplace_back();
		dcl.kind = Decal::Scar;
		dcl.owner = int16_t(e);
		dcl.material = "exscar";
		dcl.alpha = uint8_t(alpha);
		project(dcl, x.centre + Vec3{0, 0, 6}, {1, 0, 0}, size, size);
	}
	// Oil slicks while active.
	for (size_t a = 0; a < world.powerUps.actions.size(); a++) {
		const Action& x = world.powerUps.actions[a];
		if (x.kind != Action::Oil || x.state != TrapActive) {
			continue;
		}
		Decal& dcl = shown.emplace_back();
		dcl.kind = Decal::Slick;
		dcl.owner = int16_t(a);
		dcl.material = "oilslck";
		project(dcl, x.point + Vec3{0, 0, 5}, {1, 0, 0}, 15, 15);
	}
}

} // namespace lr
