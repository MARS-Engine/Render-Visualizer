#include <render_visualizer/ui/app_shell.hpp>

#include <imgui.h>

namespace rv::ui {

editor_actions render_app_shell(app_shell_context& _context) {
	ImGuiIO& _io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(_io.DisplaySize.x - 100.0f, 10.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(160.0f, 40.0f), ImGuiCond_Always);
	ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
	ImGui::Text("FPS: %.1f", _context.fps);
	ImGui::End();

	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(_io.DisplaySize);
	ImGui::SetNextWindowBgAlpha(1.0f);
	ImGui::Begin("Node Editor", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	_context.editor.mouse.right_click_signal = _context.right_click_signal;
	_context.editor.mouse.right_click_held = _context.right_click_held;
	const editor_actions _actions = render_editor(_context.editor, {
		.runtime = _context.runtime,
		.overview_open = _context.overview_open,
		.overview_captured = _context.overview_captured
	}, "##ne", { ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y });
	ImGui::End();
	return _actions;
}

} // namespace rv::ui
