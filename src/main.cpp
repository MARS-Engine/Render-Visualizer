#include <imgui_internal.h>

#include <render_visualizer/blackboard.hpp>
#include <render_visualizer/node/node_reflection.hpp>
#include <render_visualizer/node/node_registry.hpp>
#include <render_visualizer/nodes/add_node.hpp>
#include <render_visualizer/nodes/print_node.hpp>
#include <render_visualizer/nodes/variable_nodes.hpp>
#include <render_visualizer/runtime/frame_executor.hpp>
#include <render_visualizer/type_reflection.hpp>
#include <render_visualizer/ui/ui_render.hpp>
#include <render_visualizer/ui/ui_state_manager.hpp>

#include <mars/console/parser.hpp>
#include <mars/debug/crash_handler.hpp>
#include <mars/debug/env.hpp>
#include <mars/debug/logger.hpp>
#include <mars/graphics/backend/dx12/dx_backend.hpp>
#include <mars/graphics/backend/vk/vk_backend.hpp>
#include <mars/graphics/functional/device.hpp>
#include <mars/graphics/functional/graphics_engine.hpp>
#include <mars/graphics/functional/window.hpp>
#include <mars/graphics/object/command_pool.hpp>
#include <mars/graphics/object/command_recording.hpp>
#include <mars/graphics/object/main_pass_object.hpp>
#include <mars/graphics/object/pass_scope.hpp>
#include <mars/graphics/object/swapchain.hpp>
#include <mars/imgui/backend_bridge.hpp>
#include <mars/engine/input.hpp>

#include <SDL3/SDL_video.h>

#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>
#include <malloc.h>

namespace rv {

struct[[= mars::prop::rp_uses_swapchain()]]
	  [[= mars::prop::rp_clear_color(0.03f, 0.03f, 0.05f, 1.0f)]]
	  [[= mars::prop::rp_clear_depth(1.0f)]]
	  [[= mars::prop::rp_present(true)]]
	main_pass_desc {};

} // namespace rv

namespace {

mars::log_channel g_app_log_channel("app");

struct app_window_state {
	mars::window window;
	bool close_requested = false;
	bool needs_resize = false;
	bool resize_event_requested = false;
	mars::vector2<size_t> pending_size = {};
};

app_window_state g_main_window_state;
bool g_running = true;

rv::frame_executor g_project;

void on_main_window_resize(mars::window&, const mars::vector2<size_t>& _size) {
	if (_size.x == 0 || _size.y == 0)
		return;
	g_main_window_state.needs_resize = true;
	g_main_window_state.resize_event_requested = true;
	g_main_window_state.pending_size = _size;
}

void on_main_window_close(mars::window&) {
	g_main_window_state.close_requested = true;
}

void on_delete(mars::input&) {
	if (g_project.remove_selected_node())
		mars::logger::log(g_app_log_channel, "Removed selected node");
}

enum app_backend_option {
	vulkan[[= mars::meta::display("vk")]] = 0,
	directx12[[= mars::meta::display("dx12")]] = 1,
};

struct app_console_flags {
	app_backend_option backend = app_backend_option::directx12;
};

} // namespace

int main(int _argc, char* _argv[]) {
	mars::debug::install_crash_handlers();

	app_console_flags opts = mars::console::parse_arguments<app_console_flags>(
		std::vector<std::string_view>(_argv + 1, _argv + _argc)
	);

	mars::graphics_engine engine;
	switch (opts.backend) {
	case app_backend_option::vulkan:
		engine = mars::graphics_engine::make<mars::graphics::vulkan_t>();
		break;
	case app_backend_option::directx12:
		if constexpr (mars::env::platform_win32)
			engine = mars::graphics_engine::make<mars::graphics::directx12_t>();
		else {
			mars::logger::error(g_app_log_channel, "DirectX 12 backend is only supported on Windows, falling back to Vulkan");
			engine = mars::graphics_engine::make<mars::graphics::vulkan_t>();
		}
		break;
	default:
		std::cerr << "Invalid backend option\n";
		return EXIT_FAILURE;
	}

	g_main_window_state.window = mars::graphics::window_create(engine, {"App", {1280, 720}});
	g_main_window_state.window.listen<&mars::window_event::on_close, &on_main_window_close>();
	g_main_window_state.window.listen<&mars::window_event::on_resize, &on_main_window_resize>();

	mars::input input;
	mars::input_create(input, g_main_window_state.window);
	input.bind<"delete", &on_delete>();

	mars::vector2<size_t> frame_size = g_main_window_state.window.size;

	mars::device device = mars::graphics::device_create(engine);
	{
		mars::graphics::object::swapchain swapchain = mars::graphics::object::swapchain::create(device, g_main_window_state.window);

		ImGui::CreateContext();
		ImGuiIO& imgui_io = ImGui::GetIO();

		ImFont* ne_font = nullptr;
		if constexpr (mars::env::platform_win32)
			ne_font = imgui_io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 16.0f);
		else if constexpr (mars::env::platform_linux)
			ne_font = imgui_io.Fonts->AddFontFromFileTTF("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16.0f);
		rv::blackboard_font_set(ne_font, 16.0f);

		rv::node_registry registry = {};
		rv::selection_manager selection;

		rv::ui_state_manager ui_state(&g_project.active_function().graph, &registry, &selection);

		g_main_window_state.window.listen<&mars::window_event::on_mouse_change, &rv::ui_state_manager::on_window_mouse_change>(ui_state);
		g_main_window_state.window.listen<&mars::window_event::on_mouse_motion, &rv::ui_state_manager::on_window_mouse_motion>(ui_state);
		g_main_window_state.window.listen<&mars::window_event::on_mouse_wheel, &rv::ui_state_manager::on_window_mouse_wheel>(ui_state);

		mars::imgui::initialize_backend(g_main_window_state.window, device, swapchain);

		mars::graphics::object::main_pass_object<rv::main_pass_desc> main_pass(device, swapchain);
		mars::graphics::object::command_pool cmd_pool = mars::graphics::object::command_pool::create(device, 1);

		while (g_running) {
			mars::window_process_events(g_main_window_state.window, [&](const SDL_Event& event) {
				mars::imgui::process_sdl_event(event);
			});

			if (g_main_window_state.needs_resize) {
				g_main_window_state.needs_resize = false;
				mars::graphics::device_flush(device);
				frame_size = g_main_window_state.pending_size;
				main_pass.release_framebuffers();
				swapchain.resize(frame_size);
				main_pass.recreate_framebuffers(device, swapchain);
			}

			mars::imgui::new_frame();

			rv::function_instance& active_func = g_project.active_function();

			rv::blackboard_render_begin();
			ui_state.render_links();
			for (rv::graph_builder_node& node : active_func.graph)
				rv::node_draw(node);
			rv::ui_state_manager::render_result state_res = ui_state.render();

			{
				ImGuiWindow* window = ImGui::GetCurrentWindow();
				const ImVec2 origin = ImGui::GetCursorScreenPos();
				const ImVec2 window_size = ImGui::GetContentRegionAvail();
				const ImRect canvas_drop_rect(ImVec2(origin.x, origin.y), ImVec2(origin.x + window_size.x, origin.y + window_size.y));

				if (ImGui::BeginDragDropTargetCustom(canvas_drop_rect, window->GetID("##canvas_variable_drop_target"))) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("RV_VARIABLE")) {
						if (payload->DataSize == sizeof(std::size_t) && payload->IsDelivery()) {
							std::size_t var_idx = *(const std::size_t*)payload->Data;
							ImVec2 mouse_pos = ImGui::GetMousePos();
							ui_state.open_variable_drop_menu(var_idx, {mouse_pos.x, mouse_pos.y});
						}
					}
					ImGui::EndDragDropTarget();
				}
			}

			rv::blackboard_render_end();

			if (state_res.create_variable_node.has_value()) {
				std::size_t var_idx = state_res.create_variable_node->first;
				bool is_set = state_res.create_variable_node->second;
				if (var_idx < g_project.global_variables().size()) {
					auto& var = g_project.global_variables()[var_idx];
					mars::vector2<float> canvas_pos = rv::blackboard_screen_to_canvas(state_res.drop_position);
					rv::graph_builder_node* node = nullptr;

					if (is_set) {
						node = &active_func.graph.add<rv::set_variable_node>(canvas_pos);
						node->instance_ptr.get<rv::set_variable_node>()->var = var.get();
					} else {
						node = &active_func.graph.add<rv::get_variable_node>(canvas_pos);
						node->instance_ptr.get<rv::get_variable_node>()->var = var.get();
					}

					active_func.graph.mark_runtime_dirty();
				}
			}

			const rv::ui_render_result ui_result = rv::ui_render(
				g_project.functions(), g_project.active_function_index(),
				g_project.global_variables(), selection, active_func.graph, g_project.running());

			const std::size_t prev_func = g_project.active_function_index();

			if (ui_result.start_requested)
				g_project.start(g_project.active_function().graph);
			if (ui_result.stop_requested)
				g_project.stop();
			if (ui_result.graph_inputs_changed)
				g_project.active_function().graph.mark_runtime_dirty();
			if (ui_result.create_function_requested)
				g_project.create_function("New Function " + std::to_string(g_project.functions().size()));
			if (ui_result.select_function_index.has_value())
				g_project.select_function(*ui_result.select_function_index);
			if (ui_result.delete_function_index.has_value())
				g_project.delete_function(*ui_result.delete_function_index);
			if (ui_result.create_variable_requested)
				g_project.create_variable("New Variable " + std::to_string(g_project.global_variables().size()));
			if (ui_result.delete_variable_index.has_value())
				g_project.delete_variable(*ui_result.delete_variable_index, selection);

			if (g_project.active_function_index() != prev_func || ui_result.delete_function_index.has_value())
				ui_state.set_builder(&g_project.active_function().graph);

			g_project.tick();

			ImGui::Render();

			const size_t back_buffer = swapchain.back_buffer_index();
			mars::graphics::object::command_buffer_recording frame_cmd(cmd_pool, device, "frame");

			{
				mars::raster_scope<rv::main_pass_desc> main_scope(frame_cmd.get(), main_pass.get_render_pass(), frame_size);
				main_scope.begin(main_pass.framebuffer(back_buffer), main_pass.depth(back_buffer), main_pass.default_bind_params(back_buffer));
				mars::imgui::render_draw_data(frame_cmd.get());
			}

			frame_cmd.submit();

			if (main_pass.should_present())
				swapchain.present();

			if (g_main_window_state.close_requested)
				g_running = false;
		}

		mars::graphics::device_flush(device);
	}

	return 0;
}
