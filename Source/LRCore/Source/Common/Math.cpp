#include "LRCore/Common/Math.h"

#include <array>

namespace lr {

namespace {

struct Tables {
	std::array<float, 1024> acos, cos;
	Tables()
	{
		for (int i = 0; i < 1024; i++) {
			acos[i] = float(std::acos(double(i) / 511.5 - 1));
			cos[i] = float(std::cos(2 * 3.14159265358979323846 * i / 1024));
		}
	}
};

const Tables& tables()
{
	static const Tables t;
	return t;
}

} // namespace

float tableAcos(float d)
{
	const int i = int((d + 1) * 511.5f);
	return tables().acos[i < 0 ? 0 : i > 1023 ? 1023 : i];
}

float tableCos(float angle) { return cosEntry(int32_t(angle * 162.974655f)); }

float cosEntry(int32_t index) { return tables().cos[index & 1023]; }

} // namespace lr
