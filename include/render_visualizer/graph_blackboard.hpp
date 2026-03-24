#pragma once

#include <mars/math/matrix4.hpp>
#include <mars/math/vector3.hpp>
#include <mars/math/vector2.hpp>
#include <mars/meta.hpp>

#include <cstddef>

namespace rv {

struct graph_blackboard {
	[[= mars::meta::display("Time")]]
	float time = 0.0f;
	[[= mars::meta::display("Delta Time")]]
	float delta_time = 0.0f;
	[[= mars::meta::display("Current Item Index")]]
	size_t current_item_index = 0;
	[[= mars::meta::display("Current Item Count")]]
	size_t current_item_count = 1;
	[[= mars::meta::display("Right Mouse Clicked")]]
	bool right_mouse_clicked = false;
	[[= mars::meta::display("Window Close Requested")]]
	bool window_close_requested = false;
	[[= mars::meta::display("Window Size")]]
	mars::vector2<size_t> window_size = {};
	[[= mars::meta::display("Overview Captured")]]
	bool overview_captured = false;
	[[= mars::meta::display("Camera Position")]]
	mars::vector3<float> camera_position = { 0.0f, 0.0f, -5.0f };
	[[= mars::meta::display("Camera Yaw")]]
	float camera_yaw = 0.0f;
	[[= mars::meta::display("Camera Pitch")]]
	float camera_pitch = 0.0f;
	[[= mars::meta::display("Camera Move Speed")]]
	float camera_move_speed = 12.0f;
	[[= mars::meta::display("Camera Look Sensitivity")]]
	float camera_look_sensitivity = 0.0025f;
	[[= mars::meta::display("Camera View Projection")]]
	mars::matrix4<float> camera_view_proj = mars::matrix4<float>(1.0f);
};

} // namespace rv
