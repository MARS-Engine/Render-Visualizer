#pragma once

#include <imgui.h>

#include <render_visualizer/node/node_reflection.hpp>
#include <render_visualizer/runtime/graph_builder.hpp>
#include <render_visualizer/utils/bezier.hpp>

#include <mars/math/vector2.hpp>

#include <string_view>
#include <vector>

namespace rv {

// ---------------------------------------------------------------------------
// Blackboard lifecycle
// ---------------------------------------------------------------------------

void blackboard_font_set(ImFont* _font, float _font_size);
void blackboard_render_begin();
void blackboard_render_end();
ImDrawList* blackboard_draw_list();
ImFont*     blackboard_font();
float       blackboard_font_size();

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

mars::vector2<float> blackboard_origin();
mars::vector2<float> blackboard_camera_offset();
float                blackboard_zoom();
mars::vector2<float> blackboard_canvas_to_screen(const mars::vector2<float>& _canvas_position);
mars::vector2<float> blackboard_screen_to_canvas(const mars::vector2<float>& _screen_position);
void                 blackboard_camera_move(const mars::vector2<float>& _delta);
void                 blackboard_zoom_at(float _wheel_delta, const mars::vector2<float>& _screen_position);
void                 blackboard_camera_reset();

// ---------------------------------------------------------------------------
// Metrics / helpers
// ---------------------------------------------------------------------------

float  node_title_height();
float  pin_get_text_top_offset();
float  pin_radius();
float  text_get_max_width(const std::vector<pin_draw_data>& _pins);
ImU32  mars_to_imgui_colour(const mars::vector3<unsigned char>& _color);

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

struct node_layout {
	mars::vector2<float> pos = {};
	mars::vector2<float> size = {};
	float left_column_width = 0.0f;
	float right_column_width = 0.0f;
	float body_top = 0.0f;
};

node_layout          node_layout_calculate(const mars::vector2<float>& _node_pos, std::string_view _title, const std::vector<pin_draw_data>& _inputs, const std::vector<pin_draw_data>& _outputs);
mars::vector2<float> calculate_node_size(const graph_builder_node& _node);
mars::vector2<float> calculate_pin_position(const graph_builder_node& _node, std::size_t _pin_index, bool _is_output);

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void grid_draw(ImDrawList* _draw_list, const ImVec2& _origin, const ImVec2& _size);
void draw_bezier(const bezier_curve& _curve, ImU32 _color, float _thickness = 2.5f);
void pin_draw(ImDrawList* _draw_list, const ImVec2& _pin_pos, ImU32 _pin_color, ImU32 _text_color, float _pin_radius, std::string_view _label, bool _is_output);
void node_draw(ImDrawList* _draw_list, const ImVec2& _node_pos, std::string_view _title, const std::vector<pin_draw_data>& _inputs, const std::vector<pin_draw_data>& _outputs, bool _selected = false);
void node_draw(graph_builder_node& _node);

} // namespace rv
