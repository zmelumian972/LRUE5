// Skeleton sampling: one routine for the rules (the hazards read bones) and
// the drawing (the presentation poses its meshes from it).

#include "LRCore/Race/Track.h"
#include "LRCore/Show/View.h"
#include "LRData/Model.h"

namespace lr {

namespace {

float wrappedTime(float t, float loop) { return t >= loop ? t - loop * float(int32_t(t / loop)) : t; }

// Between the keys around `at` (from the last to the first before the first key and after the last): the two keys'
// indices and the fraction.
float between(const int32_t* k, int32_t count, float at, float n, int32_t& from, int32_t& to)
{
	int32_t j = 0;
	while (j < count && !(float(k[j]) > at)) {
		j++;
	}
	int32_t span;
	float e;
	if (j == 0 || j == count) {
		from = count - 1;
		to = 0;
		span = int32_t(n) + k[0] - k[count - 1];
		e = j == 0 ? float(span - k[0]) + at : at - float(k[count - 1]);
	}
	else {
		from = j - 1;
		to = j;
		span = k[j] - k[j - 1];
		e = at - float(k[j - 1]);
	}
	return span == 0 ? 0.0f : e / float(span);
}

// A node's sampled rotation (as a quaternion, unnormalized) and position; false for a channel without keys.
struct Sample {
	Quat rotation;
	Vec3 position;
	bool rotated = false, moved = false;
};

Sample sample(const Animation& a, size_t node, int32_t part, float time, bool loop)
{
	Sample s;
	if (part < 0 || time < 0 || size_t(part) >= a.parts.size() || size_t(a.parts[size_t(part)].track) + node >= a.tracks.size()) {
		return s;
	}
	const Animation::Part& p = a.parts[size_t(part)];
	const Animation::Track& t = a.tracks[size_t(p.track) + node];
	const float n = float(loop ? p.loopFrames : p.frames);
	const float at = loop ? wrappedTime(time, float(p.loopFrames)) : time;
	int32_t from, to;
	if (t.rotationKeys == 1) {
		s.rotation = a.rotations[size_t(t.rotationFrame)];
		s.rotated = true;
	}
	else if (t.rotationKeys > 1) {
		const float f = between(&a.keys[size_t(t.rotationKey)], t.rotationKeys, at, n, from, to);
		s.rotation = blend(a.rotations[size_t(t.rotationFrame + from)], a.rotations[size_t(t.rotationFrame + to)], f);
		s.rotated = true;
	}
	if (t.positionKeys == 1) {
		s.position = a.positions[size_t(t.positionFrame)];
		s.moved = true;
	}
	else if (t.positionKeys > 1) {
		const float f = between(&a.keys[size_t(t.positionKey)], t.positionKeys, at, n, from, to);
		const Vec3 A = a.positions[size_t(t.positionFrame + from)], B = a.positions[size_t(t.positionFrame + to)];
		s.position = (B - A) * f + A;
		s.moved = true;
	}
	return s;
}

// The quaternion of a rotation's rows (the trace method).
Quat quatOf(const Basis& b)
{
	const float m[3][3] = {{b.f.x, b.f.y, b.f.z}, {b.s.x, b.s.y, b.s.z}, {b.u.x, b.u.y, b.u.z}};
	const float trace = m[0][0] + m[1][1] + m[2][2];
	Quat q;
	if (trace > 0) {
		const float r = std::sqrt(trace + 1);
		const float k = 0.5f / r;
		q = {(m[2][1] - m[1][2]) * k, (m[0][2] - m[2][0]) * k, (m[1][0] - m[0][1]) * k, r * 0.5f};
	}
	else {
		int i = 0;
		i = m[1][1] > m[i][i] ? 1 : i;
		i = m[2][2] > m[i][i] ? 2 : i;
		const int j = (i + 1) % 3, k = (j + 1) % 3;
		const float r = std::sqrt(m[i][i] - m[j][j] - m[k][k] + 1);
		const float h = 0.5f / r;
		float v[3];
		v[i] = r * 0.5f;
		v[j] = (m[i][j] + m[j][i]) * h;
		v[k] = (m[i][k] + m[k][i]) * h;
		q = {v[0], v[1], v[2], (m[k][j] - m[j][k]) * h};
	}
	return q;
}

} // namespace

Quat quaternionOf(const Basis& rows) { return quatOf(rows); }

Pose sampleNode(const std::vector<SkeletonNode>& nodes, const Animation* a, size_t node, int32_t part, float time, bool loop)
{
	const SkeletonNode& rest = nodes[node];
	Pose out{rest.position, rest.rotation};
	if (!a) {
		return out;
	}
	const Sample s = sample(*a, node, part, time, loop);
	if (s.rotated) {
		out.rotation = basisOf(s.rotation);
	}
	if (s.moved) {
		out.position = s.position;
	}
	return out;
}

void sampleLocals(const std::vector<SkeletonNode>& nodes, const Animation* a, const PartPose& pose, std::vector<Pose>& locals)
{
	if (locals.size() != nodes.size()) {
		locals.resize(nodes.size());
		for (size_t n = 0; n < nodes.size(); n++) {
			locals[n] = {nodes[n].position, nodes[n].rotation};
		}
	}
	if (!a) {
		return;
	}
	for (size_t n = 0; n < nodes.size(); n++) {
		Pose& local = locals[n];
		const Sample now = sample(*a, n, pose.part, pose.time, pose.loop);
		if (pose.fromPart >= 0 && pose.blend < 1) {
			// A transition: each channel from the old part's toward the new part's (a channel without keys: the node's
			// current value).
			const Sample old = sample(*a, n, pose.fromPart, pose.fromTime, true);
			const Quat current = quatOf(local.rotation);
			const Quat qa = old.rotated ? old.rotation : current, qb = now.rotated ? now.rotation : current;
			const Vec3 pa = old.moved ? old.position : local.position, pb = now.moved ? now.position : local.position;
			local.rotation = basisOf(blend(qa, qb, pose.blend));
			local.position = (pb - pa) * pose.blend + pa;
			continue;
		}
		if (now.rotated) {
			local.rotation = basisOf(now.rotation);
		}
		if (now.moved) {
			local.position = now.position;
		}
	}
}

void placeNodes(const std::vector<SkeletonNode>& nodes, const std::vector<Pose>& locals, const Pose& place, float scale, std::vector<NodeMatrix>& out)
{
	out.resize(nodes.size());
	for (size_t n = 0; n < nodes.size() && n < locals.size(); n++) {
		// Local, then the parent's (the root's parent: the model scaled and placed).
		const Pose& local = locals[n];
		const int16_t parent = nodes[n].parent;
		Basis pr;
		Vec3 pp;
		if (parent >= 0 && size_t(parent) < n) {
			pr = out[size_t(parent)].rows;
			pp = out[size_t(parent)].position;
		}
		else {
			pr = {place.rotation.f * scale, place.rotation.s * scale, place.rotation.u * scale};
			pp = place.position;
		}
		NodeMatrix& m = out[n];
		m.rows = {toWorld(pr, local.rotation.f), toWorld(pr, local.rotation.s), toWorld(pr, local.rotation.u)};
		m.position = toWorld(pr, local.position) + pp;
	}
}

void poseSkeleton(const std::vector<SkeletonNode>& nodes, const Animation* a, const PartPose& pose, const Pose& place, float scale,
	std::vector<NodeMatrix>& out)
{
	std::vector<Pose> locals;
	sampleLocals(nodes, a, pose, locals);
	placeNodes(nodes, locals, place, scale, out);
}

void skinVertices(const Model& model, const std::vector<NodeMatrix>& nodes, std::vector<Vec3>& out)
{
	const size_t n = model.positions.size() / 3;
	out.resize(n);
	for (size_t v = 0; v < n; v++) {
		const Vec3 p = {model.positions[v * 3], model.positions[v * 3 + 1], model.positions[v * 3 + 2]};
		const size_t node = v < model.vertexParts.size() ? model.vertexParts[v] : 0;
		out[v] = node < nodes.size() ? toWorld(nodes[node].rows, p) + nodes[node].position : p;
	}
}

bool joinsNodes(const Model& model)
{
	for (const Model::Section& s : model.sections) {
		for (const uint32_t v : s.indices) {
			if (v < model.vertexParts.size() && model.vertexParts[v] != s.part) {
				return true;
			}
		}
	}
	return false;
}

} // namespace lr
