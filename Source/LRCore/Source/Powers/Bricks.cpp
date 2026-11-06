// The bricks: a colour brick taken (a held colour swapped in), a white one counted toward the level, dropped by a
// struck racer and sent home; each brick's frame (growing, shrinking, waiting to respawn) and its pose.

#include "Powers/ActionParts.h"
#include "Common/Assert.h"
#include "Show/SoundIds.h"

namespace lr {

namespace {

constexpr int32_t Grows = 500, Shrinks = 250; // ms (strictly over)
constexpr int32_t AiTakenMs = 300; // a racer's AI thinks of using its colour this soon after taking one
constexpr int32_t DroppedMs = 10000; // a dropped white brick nobody takes goes home

// A colour brick growing back where it is (heard once the race is on).
void growBack(World& w, size_t index)
{
	Brick& b = w.powerUps.bricks[index];
	b.state = Brick::Growing;
	if (w.session.phase >= Session::Racing) {
		w.raceView.playSound(BrickBackSound, {Owner::Brick, int16_t(index)}, &b.position, 30, 150);
	}
}

} // namespace

void PowerUps::touchBrick(World& w, size_t index, size_t i)
{
	LR_ASSERT(index < bricks.size() && i < w.racers.powers.size());
	Brick& b = bricks[index];
	b.touched = true;
	Power& pw = w.racers.powers[i];
	if (b.state != Brick::Idle || (w.racers.race[i].flags & RacerGhost) || (b.wasTouched && pw.held != NoColour)) {
		return;
	}
	if (b.colour != 0) {
		// Taken: a held colour is swapped in (it comes back at once), else the brick waits to respawn.
		const uint8_t had = pw.held;
		if (w.session.phase >= Session::Racing) {
			w.raceView.playSound(had ? ColourSwapSound : ColourTakenSound, {Owner::Brick, int16_t(index)}, &b.position, 30, 150);
		}
		pw.held = b.colour;
		pw.aiClockMs = 0;
		pw.aiIntervalMs = AiTakenMs;
		b.nextColour = had ? had : b.assigned;
		b.next = had ? Brick::Growing : Brick::Wait;
		b.state = Brick::Shrinking;
		b.ms = 0;
		return;
	}
	if (pw.whiteCount >= 3) {
		return;
	}
	w.raceView.playSound(BrickSounds + pw.whiteCount, {Owner::Brick, int16_t(index)}, nullptr); // (every racer's, the AI's too)
	if (pw.whiteCount == 2) {
		w.raceView.voice(w, i, RaceEvent::Positive);
	}
	for (int16_t& slot : pw.whites) {
		if (slot < 0) {
			slot = int16_t(index);
			break;
		}
	}
	pw.whiteCount++;
	b.state = Brick::Shrinking;
	b.next = Brick::Wait;
	b.ms = 0;
	b.dropped = false;
	b.droppedMs = 0;
}

void PowerUps::sendHome(World& w, size_t index)
{
	Brick& b = bricks[index];
	b.dropped = false;
	b.droppedMs = 0;
	if (b.state == Brick::Idle) {
		b.state = Brick::Shrinking; // (a dropped brick nobody took: it goes home once it has shrunk)
		b.goHome = true;
		b.ms = 0;
		return;
	}
	b.position = b.home;
	w.triggers.members[b.member].centre = b.home;
	b.state = Brick::Growing;
	b.ms = 0;
}

void PowerUps::dropWhite(World& w, size_t i)
{
	Power& pw = w.racers.powers[i];
	for (int16_t& slot : pw.whites) {
		if (slot < 0) {
			continue;
		}
		Brick& b = bricks[slot];
		const Vec3 at = w.racers.visible[i].position;
		SegmentHit hit;
		b.position = {at.x, at.y,
			(w.track.query(w.track.trackVolume, at + Vec3{0, 0, 25}, at - Vec3{0, 0, 25}, hit) ? hit.point.z : at.z) + 5};
		w.triggers.members[b.member].centre = b.position;
		b.state = Brick::Growing;
		b.ms = 0;
		b.dropped = true;
		slot = -1;
		pw.whiteCount--;
		return;
	}
}

// A brick's frame: its clock, a dropped white brick's way home, growing back, shrinking to its next state.
void PowerUps::brickFrame(World& w, size_t index, int32_t ms)
{
	Brick& b = bricks[index];
	b.ms += ms;
	b.wasTouched = b.touched;
	b.touched = false;
	if (b.colour == 0 && b.dropped) {
		b.droppedMs += ms;
		if (b.droppedMs > DroppedMs) {
			sendHome(w, index);
		}
	}
	switch (b.state) {
	case Brick::Wait:
		if (b.colour != 0 && b.respawnMs <= b.ms) {
			b.ms = 0;
			growBack(w, index);
		}
		break;
	case Brick::Growing:
		if (b.ms > Grows) {
			b.state = Brick::Idle;
			b.ms = 0;
		}
		break;
	case Brick::Shrinking:
		if (b.ms > Shrinks) {
			b.ms = 0;
			if (b.colour != 0) {
				b.state = b.next;
				b.colour = b.nextColour;
				if (b.state == Brick::Growing) {
					growBack(w, index);
				}
			}
			else if (b.goHome) {
				b.goHome = false;
				b.position = b.home;
				w.triggers.members[b.member].centre = b.home;
				b.state = Brick::Growing;
			}
			else {
				b.state = Brick::Wait;
			}
		}
		break;
	case Brick::Idle: break;
	}
}

void PowerUps::brickNodes(const World& w, Track::BrickModel which, Vec3 at, float size, std::vector<NodeMatrix>& out) const
{
	const AnimatedModel& m = w.track.brickModels[which];
	const Clock& c = brickClocks[which];
	const Animation* a = w.track.animationOf(m);
	poseSkeleton(m.nodes, a, {a ? c.part : -1, c.time, true}, {at, Basis{}}, m.scale * size, out);
}

} // namespace lr
