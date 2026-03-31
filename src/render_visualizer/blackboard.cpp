#include <render_visualizer/blackboard.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {

constexpr float g_grid_size = 64.0f;
constexpr float g_major_grid_size = g_grid_size * 4.0f;
constexpr float g_node_title_height = 36.0f;
constexpr float g_node_rounding = 6.0f;
constexpr float g_node_pin_radius = 8.0f;
constexpr float g_node_pin_spacing = 32.0f;
constexpr float g_node_body_padding = 16.0f;
constexpr float g_node_pin_text_spacing = 5.0f;
constexpr float g_blackboard_zoom_min = 0.2f;
constexpr float g_blackboard_zoom_max = 3.0f;
ImFont* g_blackboard_font = nullptr;
float g_blackboard_font_size = 16.0f;
ImDrawList* g_blackboard_draw_list = nullptr;
mars::vector2<float> g_blackboard_origin = {};
mars::vector2<float> g_blackboard_camera_offset = {};
float g_blackboard_zoom = 1.0f;

float node_title_height_scaled() {
	return g_node_title_height * g_blackboard_zoom;
}

float node_rounding_scaled() {
	return g_node_rounding * g_blackboard_zoom;
}

float node_pin_radius_scaled() {
	return g_node_pin_radius * g_blackboard_zoom;
}

float node_pin_spacing_scaled() {
	return g_node_pin_spacing * g_blackboard_zoom;
}

float node_body_padding_scaled() {
	return g_node_body_padding * g_blackboard_zoom;
}

float node_pin_text_spacing_scaled() {
	return g_node_pin_text_spacing * g_blackboard_zoom;
}

float font_size_scaled() {
	const float base_font_size = g_blackboard_font ? g_blackboard_font_size : std::max(ImGui::GetFontSize(), 16.0f);
	return base_font_size * g_blackboard_zoom;
}

ImVec2 imgui_vec(const mars::vector2<float>& _value) {
	return { _value.x, _value.y };
}

mars::vector2<float> mars_vec(const ImVec2& _value) {
	return { _value.x, _value.y };
}

mars::vector2<float> canvas_to_screen(const mars::vector2<float>& _canvas_position) {
	return {
		g_blackboard_origin.x + g_blackboard_camera_offset.x + _canvas_position.x * g_blackboard_zoom,
		g_blackboard_origin.y + g_blackboard_camera_offset.y + _canvas_position.y * g_blackboard_zoom
	};
}

mars::vector2<float> screen_to_canvas(const mars::vector2<float>& _screen_position) {
	return {
		(_screen_position.x - g_blackboard_origin.x - g_blackboard_camera_offset.x) / g_blackboard_zoom,
		(_screen_position.y - g_blackboard_origin.y - g_blackboard_camera_offset.y) / g_blackboard_zoom
	};
}

} // namespace

void rv::blackboard_font_set(ImFont* _font, float _size) {
	g_blackboard_font = _font;
	g_blackboard_font_size = _size;
}

void rv::grid_draw(ImDrawList* _draw_list, const ImVec2& _origin, const ImVec2& _size) {
	const ImU32 minor_color = IM_COL32(48, 48, 52, 255);
	const ImU32 major_color = IM_COL32(68, 68, 75, 255);
	const ImVec2 bottom_right = { _origin.x + _size.x, _origin.y + _size.y };
	const float minor_grid_size = g_grid_size * g_blackboard_zoom;
	const float major_grid_size = g_major_grid_size * g_blackboard_zoom;
	const float minor_offset_x = std::fmod(g_blackboard_camera_offset.x, minor_grid_size);
	const float minor_offset_y = std::fmod(g_blackboard_camera_offset.y, minor_grid_size);
	const float major_offset_x = std::fmod(g_blackboard_camera_offset.x, major_grid_size);
	const float major_offset_y = std::fmod(g_blackboard_camera_offset.y, major_grid_size);

	for (float x = minor_offset_x; x <= _size.x; x += minor_grid_size)
		_draw_list->AddLine({ _origin.x + x, _origin.y }, { _origin.x + x, bottom_right.y }, minor_color);
	for (float y = minor_offset_y; y <= _size.y; y += minor_grid_size)
		_draw_list->AddLine({ _origin.x, _origin.y + y }, { bottom_right.x, _origin.y + y }, minor_color);
	for (float x = major_offset_x; x <= _size.x; x += major_grid_size)
		_draw_list->AddLine({ _origin.x + x, _origin.y }, { _origin.x + x, bottom_right.y }, major_color, 1.2f * g_blackboard_zoom);
	for (float y = major_offset_y; y <= _size.y; y += major_grid_size)
		_draw_list->AddLine({ _origin.x, _origin.y + y }, { bottom_right.x, _origin.y + y }, major_color, 1.2f * g_blackboard_zoom);
}

float rv::node_title_height() {
	return node_title_height_scaled();
}

float rv::pin_get_text_top_offset() {
	ImFont* font = blackboard_font();
	const float font_size = blackboard_font_size();
	ImFontBaked* baked_font = font->GetFontBaked(font_size);
	const ImFontGlyph* glyph_x = baked_font ? baked_font->FindGlyph('x') : nullptr;
	if (glyph_x)
		return (glyph_x->Y0 + glyph_x->Y1) * 0.5f;
	return baked_font ? baked_font->Ascent * 0.6f : font_size * 0.5f;
}

float rv::pin_radius() {
	return node_pin_radius_scaled();
}

ImU32 rv::mars_to_imgui_colour(const mars::vector3<unsigned char>& _color) {
	return IM_COL32(_color.x, _color.y, _color.z, 255);
}

float rv::text_get_max_width(const std::vector<pin_draw_data>& _pins) {
	float max_width = 0.0f;
	ImFont* font = blackboard_font();
	const float font_size = blackboard_font_size();
	for (const pin_draw_data& pin : _pins) {
		const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, pin.name.data(), pin.name.data() + pin.name.size());
		max_width = std::max(max_width, text_size.x);
	}
	return max_width;
}

rv::node_layout rv::node_layout_calculate(const mars::vector2<float>& _node_pos, std::string_view _title, const std::vector<pin_draw_data>& _inputs, const std::vector<pin_draw_data>& _outputs) {
	ImFont* font = blackboard_font();
	const float font_size = blackboard_font_size();
	const float title_height = node_title_height();
	const float pin_radius = rv::pin_radius();
	const float pin_spacing = node_pin_spacing_scaled();
	const float body_padding = node_body_padding_scaled();
	const float pin_text_spacing = node_pin_text_spacing_scaled();
	const float title_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, _title.data(), _title.data() + _title.size()).x;
	const float left_column_width = text_get_max_width(_inputs);
	const float right_column_width = text_get_max_width(_outputs);
	const std::size_t row_count = std::max(_inputs.size(), _outputs.size());
	const float pin_diameter = pin_radius * 2.0f;
	const float body_height = body_padding * 2.0f + (row_count == 0 ? 0.0f : static_cast<float>(row_count - 1) * pin_spacing) + pin_diameter;
	const float pin_block_width = left_column_width + right_column_width + body_padding * 2.0f + pin_diameter * 2.0f + pin_text_spacing * 2.0f + 80.0f * g_blackboard_zoom;
	const float width = std::max(title_width + body_padding * 2.0f, pin_block_width);

	return {
		.pos = _node_pos,
		.size = { width, title_height + body_height },
		.left_column_width = left_column_width,
		.right_column_width = right_column_width,
		.body_top = _node_pos.y + title_height
	};
}

mars::vector2<float> rv::calculate_node_size(const graph_builder_node& _node) {
	std::vector<pin_draw_data> inputs;
	std::vector<pin_draw_data> outputs;
	graph_builder::collect_pins(_node, inputs, outputs);

	const node_layout layout = node_layout_calculate({}, _node.name, inputs, outputs);
	return { layout.size.x, layout.size.y };
}

mars::vector2<float> rv::calculate_pin_position(const graph_builder_node& _node, std::size_t _pin_index, bool _is_output) {
	std::vector<pin_draw_data> inputs;
	std::vector<pin_draw_data> outputs;
	graph_builder::collect_pins(_node, inputs, outputs);

	const node_layout layout = node_layout_calculate(
		blackboard_canvas_to_screen({ _node.position.x, _node.position.y }),
		_node.name,
		inputs,
		outputs
	);

	const float row_y = layout.body_top + node_body_padding_scaled() + pin_radius() + static_cast<float>(_pin_index) * node_pin_spacing_scaled();
	return _is_output
		? mars::vector2<float> { layout.pos.x + layout.size.x, row_y }
		: mars::vector2<float> { layout.pos.x, row_y };
}

void rv::draw_bezier(const bezier_curve& _curve, ImU32 _color, float _thickness) {
	if (blackboard_draw_list() == nullptr)
		return;

	blackboard_draw_list()->AddBezierCubic(
		imgui_vec(_curve.start),
		imgui_vec(_curve.control_start),
		imgui_vec(_curve.control_end),
		imgui_vec(_curve.end),
		_color,
		_thickness * g_blackboard_zoom
	);
}

void rv::pin_draw(ImDrawList* _draw_list, const ImVec2& _pin_pos, ImU32 _pin_color, ImU32 _text_color, float _pin_radius, std::string_view _label, bool _is_output) {
	ImFont* font = blackboard_font();
	const float font_size = blackboard_font_size();
	_draw_list->AddCircleFilled(_pin_pos, _pin_radius * 0.55f, IM_COL32(22, 22, 26, 255));
	_draw_list->AddCircle(_pin_pos, _pin_radius, _pin_color, 0, 1.5f * g_blackboard_zoom);

	const float pin_text_top_offset = pin_get_text_top_offset();
	const ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, _label.data(), _label.data() + _label.size());
	const float text_x = _is_output
		? std::round(_pin_pos.x - _pin_radius - node_pin_text_spacing_scaled() - text_size.x)
		: std::round(_pin_pos.x + _pin_radius + node_pin_text_spacing_scaled());
	_draw_list->AddText(font, font_size, { text_x, std::round(_pin_pos.y - pin_text_top_offset) }, _text_color, _label.data(), _label.data() + _label.size());
}

void rv::node_draw(ImDrawList* _draw_list, const ImVec2& _node_pos, std::string_view _title, const std::vector<pin_draw_data>& _inputs, const std::vector<pin_draw_data>& _outputs, bool _selected) {
	ImFont* font = blackboard_font();
	const float font_size = blackboard_font_size();
	const node_layout layout = node_layout_calculate(mars_vec(_node_pos), _title, _inputs, _outputs);
	const mars::vector2<float> node_max = { layout.pos.x + layout.size.x, layout.pos.y + layout.size.y };
	const ImU32 shadow_color = IM_COL32(0, 0, 0, 70);
	const ImU32 body_color = IM_COL32(36, 36, 40, 245);
	const ImU32 title_color = IM_COL32(55, 90, 145, 255);
	const ImU32 border_color = _selected ? IM_COL32(255, 220, 64, 255) : IM_COL32(255, 255, 255, 220);
	const ImU32 text_color = IM_COL32(255, 255, 255, 255);
	const ImU32 muted_text_color = IM_COL32(210, 210, 215, 255);
	const float title_height = node_title_height();
	const float rounding = node_rounding_scaled();

	_draw_list->AddRectFilled({ layout.pos.x + 4.0f * g_blackboard_zoom, layout.pos.y + 5.0f * g_blackboard_zoom }, { node_max.x + 4.0f * g_blackboard_zoom, node_max.y + 5.0f * g_blackboard_zoom }, shadow_color, rounding);
	_draw_list->AddRectFilled(imgui_vec(layout.pos), imgui_vec(node_max), body_color, rounding);
	_draw_list->AddRectFilled(imgui_vec(layout.pos), { node_max.x, layout.pos.y + title_height }, title_color, rounding);
	_draw_list->AddRectFilled({ layout.pos.x, layout.pos.y + title_height - rounding }, { node_max.x, layout.pos.y + title_height }, title_color);
	_draw_list->AddRect(imgui_vec(layout.pos), imgui_vec(node_max), border_color, rounding, 0, 1.5f * g_blackboard_zoom);
	_draw_list->AddLine({ layout.pos.x, layout.pos.y + title_height }, { node_max.x, layout.pos.y + title_height }, IM_COL32(75, 75, 82, 200), 1.0f * g_blackboard_zoom);

	const ImVec2 title_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, _title.data(), _title.data() + _title.size());
	const ImVec2 title_pos = {
		std::round(layout.pos.x + (layout.size.x - title_size.x) * 0.5f),
		std::round(layout.pos.y + (title_height - title_size.y) * 0.5f)
	};
	_draw_list->AddText(font, font_size, title_pos, text_color, _title.data(), _title.data() + _title.size());

	const std::size_t row_count = std::max(_inputs.size(), _outputs.size());
	for (std::size_t index = 0; index < row_count; ++index) {
		const float row_y = layout.body_top + node_body_padding_scaled() + pin_radius() + static_cast<float>(index) * node_pin_spacing_scaled();

		if (index < _inputs.size()) {
			const ImVec2 pin_pos = { layout.pos.x, row_y };
			pin_draw(_draw_list, pin_pos, mars_to_imgui_colour(_inputs[index].colour), muted_text_color, pin_radius(), _inputs[index].name, false);
		}

		if (index < _outputs.size()) {
			const ImVec2 pin_pos = { layout.pos.x + layout.size.x, row_y };
			pin_draw(_draw_list, pin_pos, mars_to_imgui_colour(_outputs[index].colour), muted_text_color, pin_radius(), _outputs[index].name, true);
		}
	}
}

void rv::node_draw(graph_builder_node& _node) {
	if (_node.get_pin_draw_info == nullptr) {
		_node.size = {};
		return;
	}

	std::vector<pin_draw_data> inputs;
	std::vector<pin_draw_data> outputs;
	graph_builder::collect_pins(_node, inputs, outputs);
	_node.size = calculate_node_size(_node);
	node_draw(blackboard_draw_list(), imgui_vec(blackboard_canvas_to_screen({ _node.position.x, _node.position.y })), _node.name, inputs, outputs, _node.selected);
}

void rv::blackboard_render_begin() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

	constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	const bool visible = ImGui::Begin("blackboard", nullptr, window_flags);
	ImGui::PopStyleVar(2);

	if (!visible) {
		ImGui::End();
		return;
	}

	g_blackboard_draw_list = ImGui::GetWindowDrawList();
	g_blackboard_origin = mars_vec(ImGui::GetWindowPos());
	const ImVec2 canvas_size = ImGui::GetWindowSize();

	g_blackboard_draw_list->AddRectFilled(imgui_vec(g_blackboard_origin), { g_blackboard_origin.x + canvas_size.x, g_blackboard_origin.y + canvas_size.y }, IM_COL32(14, 14, 18, 255));
	grid_draw(g_blackboard_draw_list, imgui_vec(g_blackboard_origin), canvas_size);
}

void rv::blackboard_render_end() {
	g_blackboard_draw_list = nullptr;
	ImGui::End();
}

ImDrawList* rv::blackboard_draw_list() {
	return g_blackboard_draw_list;
}

ImFont* rv::blackboard_font() {
	return g_blackboard_font ? g_blackboard_font : ImGui::GetFont();
}

float rv::blackboard_font_size() {
	return font_size_scaled();
}

mars::vector2<float> rv::blackboard_origin() {
	return g_blackboard_origin;
}

mars::vector2<float> rv::blackboard_camera_offset() {
	return g_blackboard_camera_offset;
}

float rv::blackboard_zoom() {
	return g_blackboard_zoom;
}

mars::vector2<float> rv::blackboard_canvas_to_screen(const mars::vector2<float>& _canvas_position) {
	return canvas_to_screen(_canvas_position);
}

mars::vector2<float> rv::blackboard_screen_to_canvas(const mars::vector2<float>& _screen_position) {
	return screen_to_canvas(_screen_position);
}

void rv::blackboard_camera_move(const mars::vector2<float>& _delta) {
	g_blackboard_camera_offset.x += _delta.x;
	g_blackboard_camera_offset.y += _delta.y;
}

void rv::blackboard_zoom_at(float _wheel_delta, const mars::vector2<float>& _screen_position) {
	if (_wheel_delta == 0.0f)
		return;

	const float old_zoom = g_blackboard_zoom;
	g_blackboard_zoom = std::clamp(g_blackboard_zoom * (1.0f + _wheel_delta * 0.12f), g_blackboard_zoom_min, g_blackboard_zoom_max);
	if (g_blackboard_zoom == old_zoom)
		return;

	const mars::vector2<float> canvas_position = {
		(_screen_position.x - g_blackboard_origin.x - g_blackboard_camera_offset.x) / old_zoom,
		(_screen_position.y - g_blackboard_origin.y - g_blackboard_camera_offset.y) / old_zoom
	};
	g_blackboard_camera_offset = {
		_screen_position.x - g_blackboard_origin.x - canvas_position.x * g_blackboard_zoom,
		_screen_position.y - g_blackboard_origin.y - canvas_position.y * g_blackboard_zoom
	};
}

void rv::blackboard_camera_reset() {
	g_blackboard_camera_offset = {};
	g_blackboard_zoom = 1.0f;
}
