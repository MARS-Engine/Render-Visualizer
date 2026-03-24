#include <render_visualizer/ui/runtime_panels.hpp>

#include <render_visualizer/runtime/impl.hpp>

#include <imgui.h>
#include <mars/imgui/backend_bridge.hpp>

#include <algorithm>

namespace rv::ui {

void render_runtime_overlay_contents(const graph_runtime& _runtime) {
	const NE_Node* _end_node = _runtime.root_node();
	ImGui::Text("Runtime: %s", runtime_detail::state_name(_runtime.running, _runtime.dirty, _runtime.has_error).c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("| %s", _runtime.last_build_rebuilt ? "rebuilt" : "cached");
	ImGui::Text("Root: %s", _end_node == nullptr ? "none" : _end_node->title.c_str());
	ImGui::TextDisabled("GPU %zu | Exec %zu | Skipped %zu", _runtime.last_gpu_step_count, _runtime.last_exec_instance_count, _runtime.last_skipped_instance_count);
	ImGui::Separator();

	const float _footer_height = _runtime.last_error.empty()
		? ImGui::GetTextLineHeightWithSpacing()
		: (ImGui::GetTextLineHeightWithSpacing() * 3.0f);
	ImGui::BeginChild("##runtime_preview", { 0.0f, -_footer_height }, ImGuiChildFlags_Borders);

	const runtime_detail::framebuffer_attachment_resources* _preview = nullptr;
	const char* _placeholder = nullptr;
	if (!_runtime.running)
		_placeholder = "Runtime not running.";
	else if (_runtime.dirty)
		_placeholder = "Runtime is rebuilding.";
	else if (_runtime.has_error)
		_placeholder = _runtime.last_error.empty() ? "Runtime has errors." : _runtime.last_error.c_str();
	else {
		_preview = _runtime.find_present_source_resources();
		if (_preview == nullptr)
			_placeholder = "No output connected.";
	}

	if (_preview != nullptr) {
		if (_preview->owner == nullptr || _preview->attachment_index >= _preview->owner->targets.size())
			_placeholder = "Preview unavailable.";
		else {
			const mars::texture& _preview_texture = _preview->owner->targets[_preview->attachment_index];
			const ImTextureRef _texture = mars::imgui::texture_ref(_preview_texture);
			if (_texture.GetTexID() == ImTextureID_Invalid)
				_placeholder = "Preview unavailable.";
			else {
				const float _texture_width = static_cast<float>(_preview->owner->extent.x == 0 ? _preview_texture.size.x : _preview->owner->extent.x);
				const float _texture_height = static_cast<float>(_preview->owner->extent.y == 0 ? _preview_texture.size.y : _preview->owner->extent.y);
				const ImVec2 _avail = ImGui::GetContentRegionAvail();
				const float _scale = std::min(_avail.x / _texture_width, _avail.y / _texture_height);
				const ImVec2 _image_size = {
					std::max(1.0f, _texture_width * _scale),
					std::max(1.0f, _texture_height * _scale)
				};
				const float _offset_x = std::max(0.0f, (_avail.x - _image_size.x) * 0.5f);
				const float _offset_y = std::max(0.0f, (_avail.y - _image_size.y) * 0.5f);
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + _offset_x);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + _offset_y);
				ImGui::Image(_texture, _image_size);
			}
		}
	}

	if (_placeholder != nullptr) {
		const ImVec2 _avail = ImGui::GetContentRegionAvail();
		const ImVec2 _text_size = ImGui::CalcTextSize(_placeholder, nullptr, false, _avail.x);
		if (_avail.y > _text_size.y)
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (_avail.y - _text_size.y) * 0.5f);
		ImGui::TextWrapped("%s", _placeholder);
	}

	ImGui::EndChild();
	if (_preview != nullptr)
		ImGui::TextDisabled("Showing texture linked to swapchain color input");
	if (!_runtime.last_error.empty())
		ImGui::TextWrapped("Error: %s", _runtime.last_error.c_str());
}

void render_runtime_overlay(const graph_runtime& _runtime) {
	ImGuiIO& _io = ImGui::GetIO();
	const ImVec2 _overlay_size = { 420.0f, 360.0f };
	const ImVec2 _overlay_pos = { _io.DisplaySize.x - _overlay_size.x - 16.0f, _io.DisplaySize.y - _overlay_size.y - 16.0f };
	ImGui::SetNextWindowPos(_overlay_pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(_overlay_size, ImGuiCond_Always);
	ImGui::Begin("##runtime_overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
	render_runtime_overlay_contents(_runtime);
	ImGui::End();
}

bool render_runtime_panel(const runtime_panel_context& _context) {
	if (_context.runtime != nullptr)
		render_runtime_overlay_contents(*_context.runtime);
	else
		ImGui::TextDisabled("No runtime panel bound");
	ImGui::Separator();
	if (ImGui::Button(_context.overview_open ? "Close Overview" : "Open Overview", { -1.0f, 0.0f }))
		return true;
	if (_context.overview_open && _context.overview_captured)
		ImGui::TextDisabled("Overview captured, press Esc to release");
	return false;
}

} // namespace rv::ui
