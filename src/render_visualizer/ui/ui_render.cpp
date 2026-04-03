#include <render_visualizer/ui/ui_render.hpp>

#include <imgui.h>
#include <render_visualizer/type_reflection.hpp>

#include <algorithm>

namespace {

constexpr float g_splitter_thickness = 6.0f;
constexpr float g_min_drawer_width = 280.0f;
constexpr float g_min_center_width = 320.0f;
constexpr float g_min_inspect_height = 140.0f;
constexpr float g_min_functions_height = 140.0f;
constexpr float g_min_variables_height = 140.0f;
float g_left_drawer_width = 280.0f;
float g_right_drawer_width = 280.0f;
float g_right_overview_height = 240.0f;
float g_left_functions_height = 240.0f;

void splitter_draw_x(const char* _id, const ImVec2& _position, float _height, float* _size0, float _min_size0, float _max_size0, float _delta_sign = 1.0f) {
	ImGui::SetCursorScreenPos(_position);
	ImGui::InvisibleButton(_id, { g_splitter_thickness, _height });
	const float low = std::min(_min_size0, _max_size0);
	const float high = std::max(_min_size0, _max_size0);
	if (ImGui::IsItemActive())
		*_size0 = std::clamp(*_size0 + ImGui::GetIO().MouseDelta.x * _delta_sign, low, high);
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	const ImU32 color = ImGui::IsItemActive() || ImGui::IsItemHovered() ? IM_COL32(170, 170, 180, 255) : IM_COL32(92, 92, 100, 220);
	draw_list->AddRectFilled(_position, { _position.x + g_splitter_thickness, _position.y + _height }, color);
}

void splitter_draw_y(const char* _id, float* _size0, float _min_size0, float _max_size0) {
	ImVec2 cursor = ImGui::GetCursorScreenPos();
	ImVec2 available = ImGui::GetContentRegionAvail();
	ImGui::InvisibleButton(_id, { available.x, g_splitter_thickness });
	const float low = std::min(_min_size0, _max_size0);
	const float high = std::max(_min_size0, _max_size0);
	if (ImGui::IsItemActive())
		*_size0 = std::clamp(*_size0 + ImGui::GetIO().MouseDelta.y, low, high);
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	const ImU32 color = ImGui::IsItemActive() || ImGui::IsItemHovered() ? IM_COL32(170, 170, 180, 255) : IM_COL32(92, 92, 100, 220);
	draw_list->AddRectFilled(cursor, { cursor.x + available.x, cursor.y + g_splitter_thickness }, color);
}

} // namespace

bool rv::ui_contains_point(const mars::vector2<float>& _screen_position) {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	if (viewport == nullptr)
		return false;

	const ImVec2 work_pos = viewport->WorkPos;
	const ImVec2 work_size = viewport->WorkSize;
	const float left_edge = work_pos.x + g_left_drawer_width;
	const float right_edge = work_pos.x + work_size.x - g_right_drawer_width;

	return _screen_position.x <= left_edge || _screen_position.x >= right_edge;
}

rv::ui_render_result rv::ui_render(const std::vector<std::unique_ptr<rv::function_instance>>& _functions, std::size_t _active_function_index, const std::vector<std::unique_ptr<rv::variable>>& _variables, selection_manager& _selection, graph_builder& _graph, bool _running) {
	ui_render_result result = {};

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	if (viewport == nullptr)
		return result;

	const ImVec2 work_pos = viewport->WorkPos;
	const ImVec2 work_size = viewport->WorkSize;
	const float max_total_drawers = std::max(2.0f * g_min_drawer_width, work_size.x - g_min_center_width - g_splitter_thickness * 2.0f);
	g_left_drawer_width = std::clamp(g_left_drawer_width, g_min_drawer_width, std::max(g_min_drawer_width, max_total_drawers - g_right_drawer_width));
	g_right_drawer_width = std::clamp(g_right_drawer_width, g_min_drawer_width, std::max(g_min_drawer_width, max_total_drawers - g_left_drawer_width));
	const float max_left_drawer_width = std::max(g_min_drawer_width, work_size.x - g_right_drawer_width - g_splitter_thickness * 2.0f - g_min_center_width);
	const float max_right_drawer_width = std::max(g_min_drawer_width, work_size.x - g_left_drawer_width - g_splitter_thickness * 2.0f - g_min_center_width);
	g_left_drawer_width = std::clamp(g_left_drawer_width, g_min_drawer_width, max_left_drawer_width);
	g_right_drawer_width = std::clamp(g_right_drawer_width, g_min_drawer_width, max_right_drawer_width);

	constexpr ImGuiWindowFlags drawer_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

	ImGui::SetNextWindowPos(work_pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize({ g_left_drawer_width, work_size.y }, ImGuiCond_Always);
	if (ImGui::Begin("rv_left_drawer", nullptr, drawer_flags)) {
		const float left_drawer_available_y = ImGui::GetContentRegionAvail().y;
		g_left_functions_height = std::clamp(g_left_functions_height, g_min_functions_height, std::max(g_min_functions_height, left_drawer_available_y - g_min_variables_height - g_splitter_thickness));

		ImGui::BeginChild("##rv_functions_panel", { 0.0f, g_left_functions_height }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
		ImGui::TextUnformatted("Functions");
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("+").x - ImGui::GetStyle().ItemSpacing.x);
		if (ImGui::Button("+##func"))
			result.create_function_requested = true;
		
		ImGui::Separator();
		for (std::size_t i = 0; i < _functions.size(); ++i) {

			const bool is_selected = (i == _active_function_index);
			ImGui::PushID(static_cast<int>(i));
			std::string display_name = _functions[i]->name;

			if (i == _active_function_index && _running)
				display_name += " [Running]";

			if (ImGui::Selectable(display_name.c_str(), is_selected))
				result.select_function_index = i;
				

			if (ImGui::BeginPopupContextItem("##rv_function_context")) {
				if (ImGui::Selectable("Delete"))
					result.delete_function_index = i;
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		ImGui::EndChild();

		splitter_draw_y("##rv_left_drawer_splitter", &g_left_functions_height, g_min_functions_height, std::max(g_min_functions_height, left_drawer_available_y - g_min_variables_height - g_splitter_thickness));

		ImGui::BeginChild("##rv_variables_panel", { 0.0f, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
		ImGui::TextUnformatted("Variables");
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("+").x - ImGui::GetStyle().ItemSpacing.x);

		if (ImGui::Button("+##var"))
			result.create_variable_requested = true;

		ImGui::Separator();
		for (std::size_t i = 0; i < _variables.size(); ++i) {
			const bool is_selected = (_selection.selected_variable().value_or(static_cast<std::size_t>(-1)) == i);
			const auto& var = _variables[i];
			ImGui::PushID(static_cast<int>(i + _functions.size() + 1000));
			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			ImVec2 p = ImGui::GetCursorScreenPos();
			p.y += (ImGui::GetTextLineHeight() - 10.0f) * 0.5f;

			if (var->type)
				draw_list->AddRectFilled(p, ImVec2(p.x + 10, p.y + 10), IM_COL32(var->type->colour.x, var->type->colour.y, var->type->colour.z, 255));

			ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + 15, ImGui::GetCursorScreenPos().y));
			
			if (ImGui::Selectable(var->name.c_str(), is_selected))
				_selection.select_variable(i);
			
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
				ImGui::SetDragDropPayload("RV_VARIABLE", &i, sizeof(std::size_t));
				ImGui::TextUnformatted(var->name.c_str());
				ImGui::EndDragDropSource();
			}

			if (ImGui::BeginPopupContextItem("##rv_variable_context")) {
				if (ImGui::Selectable("Delete")) {
					result.delete_variable_index = i;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	}
	ImGui::End();

	ImGui::SetNextWindowPos({ work_pos.x + work_size.x - g_right_drawer_width, work_pos.y }, ImGuiCond_Always);
	ImGui::SetNextWindowSize({ g_right_drawer_width, work_size.y }, ImGuiCond_Always);
	if (ImGui::Begin("rv_right_drawer", nullptr, drawer_flags)) {
		const float right_drawer_available_y = ImGui::GetContentRegionAvail().y;
		g_right_overview_height = std::clamp(g_right_overview_height, g_min_inspect_height, std::max(g_min_inspect_height, right_drawer_available_y - g_min_inspect_height - g_splitter_thickness));

		ImGui::BeginChild("##rv_overview_panel", { 0.0f, g_right_overview_height }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
		ImGui::TextUnformatted("Overview");
		ImGui::Separator();
		ImGui::TextUnformatted(_running ? "Running" : "Stopped");
		if (ImGui::Button(_running ? "Stop" : "Start", { -1.0f, 0.0f })) {
			if (_running)
				result.stop_requested = true;
			else
				result.start_requested = true;
		}
		ImGui::EndChild();

		splitter_draw_y("##rv_right_drawer_splitter", &g_right_overview_height, g_min_inspect_height, std::max(g_min_inspect_height, right_drawer_available_y - g_min_inspect_height - g_splitter_thickness));

		ImGui::BeginChild("##rv_inspect_panel", { 0.0f, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
		ImGui::TextUnformatted("Inspect");
		ImGui::Separator();
		if (const graph_builder_node* selected_node = _graph.selected_node(); selected_node != nullptr) {
			std::vector<pin_draw_data> inputs;
			std::vector<pin_draw_data> outputs;
			graph_builder::collect_pins(*selected_node, inputs, outputs);
			ImGui::Text("Node: %.*s", static_cast<int>(selected_node->name.size()), selected_node->name.data());

			bool rendered_properties = false;
			if (selected_node->metadata.operations.inspect_properties != nullptr) {
				if (selected_node->metadata.operations.inspect_properties(selected_node->instance_ptr, &_variables))
					result.graph_inputs_changed = true;
				rendered_properties = true;
			}

			bool has_inspectable_inputs = false;
			for (const pin_draw_data& pin : inputs) {
				if (pin.kind == pin_kind::execution || pin.ops.render_inspector == nullptr)
					continue;
				if (pin.ops.resolve_value == nullptr)
					continue;
				has_inspectable_inputs = true;
				if (pin.ops.render_inspector(pin.ops.resolve_value(selected_node->instance_ptr), pin.name))
					result.graph_inputs_changed = true;
			}
			if (!has_inspectable_inputs && !rendered_properties)
				ImGui::TextDisabled("No inspectable properties");
		}
		else if (_selection.selected_variable().has_value() && *_selection.selected_variable() < _variables.size()) {
			auto& var = _variables[*_selection.selected_variable()];
			ImGui::TextUnformatted("Variable:");
			char buf[256];
			strncpy(buf, var->name.c_str(), sizeof(buf));
			buf[sizeof(buf) - 1] = '\0';
			if (ImGui::InputText("Name", buf, sizeof(buf))) {
				var->name = buf;
			}
			
			std::string current_type_name = var->type ? std::string(var->type->name) : "Unknown";

			if (ImGui::BeginCombo("Type", current_type_name.c_str())) {
				for (const auto& type_desc : rv::get_available_variable_types()) {
					if (ImGui::Selectable(type_desc.name.data(), var->type && type_desc.type_hash == var->type->type_hash)) {
						var->set_type(type_desc.type_hash);
						result.graph_inputs_changed = true;
					}
				}
				ImGui::EndCombo();
			}
		}
		else {
			ImGui::TextDisabled("No node or variable selected");
		}
		ImGui::EndChild();
	}
	ImGui::End();

	ImGui::SetNextWindowPos(work_pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(work_size, ImGuiCond_Always);
	constexpr ImGuiWindowFlags splitter_host_flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0.0f, 0.0f });
	if (ImGui::Begin("rv_splitter_host", nullptr, splitter_host_flags)) {
		splitter_draw_x("##rv_left_splitter", { work_pos.x + g_left_drawer_width, work_pos.y }, work_size.y, &g_left_drawer_width, g_min_drawer_width, max_left_drawer_width);
		splitter_draw_x(
			"##rv_right_splitter",
			{ work_pos.x + work_size.x - g_right_drawer_width - g_splitter_thickness, work_pos.y },
			work_size.y,
			&g_right_drawer_width,
			g_min_drawer_width,
			max_right_drawer_width,
			-1.0f
		);
	}
	ImGui::End();
	ImGui::PopStyleVar();

	return result;
}
