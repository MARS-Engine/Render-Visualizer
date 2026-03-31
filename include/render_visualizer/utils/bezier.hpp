#pragma once

#include <mars/math/vector2.hpp>

namespace rv {

struct bezier_curve {
	mars::vector2<float> start = {};
	mars::vector2<float> control_start = {};
	mars::vector2<float> control_end = {};
	mars::vector2<float> end = {};
};

float bezier_control_offset(const mars::vector2<float>& _start, const mars::vector2<float>& _end, float _zoom = 1.0f);
bezier_curve calculate_bezier_curve(const mars::vector2<float>& _start, const mars::vector2<float>& _end, float _zoom = 1.0f);

} // namespace rv
