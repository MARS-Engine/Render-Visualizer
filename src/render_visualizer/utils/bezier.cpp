#include <render_visualizer/utils/bezier.hpp>

#include <algorithm>
#include <cmath>

float rv::bezier_control_offset(const mars::vector2<float>& _start, const mars::vector2<float>& _end, float _zoom) {
	return std::min(120.0f * _zoom, std::fabs(_end.x - _start.x) * 0.75f + 20.0f * _zoom);
}

rv::bezier_curve rv::calculate_bezier_curve(const mars::vector2<float>& _start, const mars::vector2<float>& _end, float _zoom) {
	const float control_offset = bezier_control_offset(_start, _end, _zoom);
	return {
		.start = _start,
		.control_start = { _start.x + control_offset, _start.y },
		.control_end = { _end.x - control_offset, _end.y },
		.end = _end
	};
}
