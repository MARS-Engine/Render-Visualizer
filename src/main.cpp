#include <imgui.h>

#include <render_visualizer/blackboard.hpp>
#include <render_visualizer/node/node_reflection.hpp>
#include <render_visualizer/node/node_registry.hpp>
#include <render_visualizer/runtime/frame_executor.hpp>
#include <render_visualizer/runtime/graph_builder.hpp>
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
#include <string_view>
#include <utility>
#include <vector>

namespace rv {

struct[[= mars::prop::rp_uses_swapchain()]]
	  [[= mars::prop::rp_clear_color(0.03f, 0.03f, 0.05f, 1.0f)]]
	  [[= mars::prop::rp_clear_depth(1.0f)]]
	  [[= mars::prop::rp_present(true)]]
	main_pass_desc {};

} // namespace rv

struct [[=mars::meta::display("Add Node")]]
	   [[=rv::node_pure()]]
	add_node {
	[[=rv::input]] float a;
	[[=rv::input]] float b;
	[[=rv::output]] float result;

	[[=rv::execute]]
	void execute() {
		result = a + b;
	}
};
inline const auto g_add_node_registration = rv::auto_register_node_v<add_node>;

struct [[=mars::meta::display("Print Node")]] print_node {
	[[=rv::input]] float a;

	[[=rv::execute]]
	void execute() {
		std::cout << "Print Node: " << a << "\n";
	}
};
inline const auto g_print_node_registration = rv::auto_register_node_v<print_node>;

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
rv::graph_builder g_graph;

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
	if (g_graph.remove_selected_node())
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

		rv::ui_state_manager ui_state(&g_graph, &registry);
		rv::frame_executor executor = {};

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

			rv::blackboard_render_begin();
			ui_state.render_links();
			for (rv::graph_builder_node& node : g_graph)
				rv::node_draw(node);
			ui_state.render();
			rv::blackboard_render_end();
			const rv::ui_render_result ui_result = rv::ui_render(g_graph, executor.running());
			if (ui_result.graph_inputs_changed)
				g_graph.mark_runtime_dirty();
			if (ui_result.stop_requested)
				executor.stop();
			if (ui_result.start_requested) {
				rv::graph_frame_build_result build = g_graph.build_frame();
				if (build.valid)
					executor.start(std::move(build));
				else
					mars::logger::error(g_app_log_channel, "Failed to start graph execution: {}", build.error_message);
			}
			if (executor.running() && g_graph.runtime_revision() != executor.source_revision()) {
				rv::graph_frame_build_result build = g_graph.build_frame();
				if (build.valid)
					executor.start(std::move(build));
				else {
					mars::logger::error(g_app_log_channel, "Failed to rebuild graph execution: {}", build.error_message);
					executor.stop();
				}
			}
			if (executor.running())
				executor.tick();
			
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
