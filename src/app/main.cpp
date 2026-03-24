#include <imgui.h>

#include <render_visualizer/graph_persistence.hpp>
#include <render_visualizer/main_pass.hpp>
#include <render_visualizer/nodes/all.hpp>
#include <render_visualizer/runtime.hpp>
#include <render_visualizer/ui/app_shell.hpp>
#include <render_visualizer/ui/widgets.hpp>

#include <mars/console/parser.hpp>
#include <mars/debug/crash_handler.hpp>
#include <mars/debug/env.hpp>
#include <mars/debug/logger.hpp>
#include <mars/engine/input.hpp>
#include <mars/graphics/backend/dx12/dx_backend.hpp>
#include <mars/graphics/backend/vk/vk_backend.hpp>
#include <mars/graphics/functional/device.hpp>
#include <mars/graphics/functional/graphics_engine.hpp>
#include <mars/graphics/functional/window.hpp>
#include <mars/graphics/object/command_pool.hpp>
#include <mars/graphics/object/command_queue.hpp>
#include <mars/graphics/object/command_recording.hpp>
#include <mars/graphics/object/main_pass_object.hpp>
#include <mars/graphics/object/pass_scope.hpp>
#include <mars/graphics/object/swapchain.hpp>
#include <mars/imgui/backend_bridge.hpp>
#include <mars/math/matrix4.hpp>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

mars::log_channel g_app_log_channel("app");

struct app_window_state {
	mars::window window;
	mars::input input;
	bool close_requested = false;
	bool needs_resize = false;
	bool resize_event_requested = false;
	mars::vector2<size_t> pending_size = {};
	bool right_click = false;
	bool right_click_event_requested = false;
};

struct overview_camera_state {
	mars::vector3<float> position = { 0.0f, 0.0f, -5.0f };
	float yaw = 0.0f;
	float pitch = 0.0f;
	float move_speed = 12.0f;
	float look_sensitivity = 0.0025f;
	mars::matrix4<float> view_proj = mars::matrix4<float>(1.0f);
};

struct overview_window_state {
	bool open = false;
	bool captured = false;
	bool capture_requested = false;
	bool focused = false;
	bool close_requested = false;
	bool needs_resize = false;
	mars::vector2<size_t> pending_size = {};
	app_window_state events;
	std::unique_ptr<mars::graphics::object::swapchain> swapchain;
	std::unique_ptr<mars::graphics::object::main_pass_object<rv::main_pass_desc>> main_pass;
	std::unique_ptr<mars::graphics::object::command_pool> cmd_pool;
	overview_camera_state camera;
};

app_window_state g_main_window_state;
overview_window_state g_overview_window_state;
bool g_running = true;

void release_overview_capture();

void register_default_pin_colors(NodeRegistry& registry) {
	registry.register_pin_color<float>({ 240.f / 255.f, 190.f / 255.f, 90.f / 255.f, 1.0f });
	registry.register_pin_color<unsigned int>({ 250.f / 255.f, 160.f / 255.f, 60.f / 255.f, 1.0f });
	registry.register_pin_color<mars::matrix4<float>>({ 255.f / 255.f, 230.f / 255.f, 120.f / 255.f, 1.0f });
	registry.register_pin_color<mars::vector2<float>>({ 80.f / 255.f, 200.f / 255.f, 120.f / 255.f, 1.0f });
	registry.register_pin_color<mars::vector3<float>>({ 110.f / 255.f, 175.f / 255.f, 255.f / 255.f, 1.0f });
	registry.register_pin_color<mars::vector3<unsigned char>>({ 245.f / 255.f, 120.f / 255.f, 90.f / 255.f, 1.0f });
	registry.register_pin_color<mars::vector4<float>>({ 220.f / 255.f, 130.f / 255.f, 255.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::vertex_buffer>({ 100.f / 255.f, 150.f / 255.f, 255.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::index_buffer>({ 70.f / 255.f, 125.f / 255.f, 235.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::shader_module>({ 255.f / 255.f, 200.f / 255.f, 70.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::render_pass>({ 120.f / 255.f, 210.f / 255.f, 180.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::framebuffer>({ 120.f / 255.f, 190.f / 255.f, 230.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::depth_buffer>({ 110.f / 255.f, 130.f / 255.f, 255.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::graphics_pipeline>({ 230.f / 255.f, 120.f / 255.f, 210.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::color_texture>({ 245.f / 255.f, 120.f / 255.f, 90.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::texture_slot>({ 245.f / 255.f, 170.f / 255.f, 90.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::virtual_struct_schema>({ 120.f / 255.f, 235.f / 255.f, 205.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::uniform_resource>({ 255.f / 255.f, 205.f / 255.f, 95.f / 255.f, 1.0f });
	registry.register_pin_color<rv::resource_tags::material_resource>({ 255.f / 255.f, 135.f / 255.f, 175.f / 255.f, 1.0f });
}

void register_default_pin_types(NodeRegistry& registry) {
	// Numeric / virtual-struct-field scalars
	registry.register_pin_type<float>("float", true, false, true);
	registry.register_pin_type<mars::vector2<float>>("float2", true, false, true);
	registry.register_pin_type<mars::vector3<float>>("float3", true, false, true);
	registry.register_pin_type<mars::vector4<float>>("float4", true, false, true);
	registry.register_pin_type<unsigned int>("uint", true, false, true);
	registry.register_pin_type<mars::vector2<unsigned int>>("uint2", true, false, true);
	registry.register_pin_type<mars::vector3<unsigned int>>("uint3", true, false, true);
	registry.register_pin_type<mars::vector4<unsigned int>>("uint4", true, false, true);
	// Non-numeric scalars
	registry.register_pin_type<mars::matrix4<float>>("float4x4", false, false, false);
	registry.register_pin_type<bool>("bool", false, false, false);
	registry.register_pin_type<std::string>("string", false, false, false);
	// Container types
	registry.register_pin_type<std::vector<float>>("float[]", false, false, false);
	registry.register_pin_type<std::vector<mars::vector2<float>>>("float2[]", false, false, false);
	registry.register_pin_type<std::vector<mars::vector3<float>>>("float3[]", false, false, false);
	registry.register_pin_type<std::vector<unsigned int>>("uint[]", false, false, false);
	// Resource types
	registry.register_pin_type<rv::resource_tags::vertex_buffer>("Vertex Buffer", false, true, false);
	registry.register_pin_type<rv::resource_tags::index_buffer>("Index Buffer", false, true, false);
	registry.register_pin_type<rv::resource_tags::uniform_resource>("Uniform Resource", false, true, false);
	registry.register_pin_type<mars::vector3<unsigned char>>("Color Texture", false, true, false);
	registry.register_pin_type<rv::resource_tags::texture_slot>("Texture Slot", false, true, false);
	registry.register_pin_type<rv::resource_tags::shader_module>("Shader Module", false, true, false);
	registry.register_pin_type<rv::resource_tags::render_pass>("Render Pass", false, true, false);
	registry.register_pin_type<rv::resource_tags::framebuffer>("Framebuffer", false, true, false);
	registry.register_pin_type<rv::resource_tags::depth_buffer>("Depth Buffer", false, true, false);
	registry.register_pin_type<rv::resource_tags::graphics_pipeline>("Graphics Pipeline", false, true, false);
	registry.register_pin_type<rv::resource_tags::material_resource>("Material Resource", false, true, false);
	registry.register_pin_type<rv::resource_tags::virtual_struct_schema>("Virtual Struct", false, true, false);
}

void on_main_window_resize(mars::window&, const mars::vector2<size_t>& size) {
	if (size.x == 0 || size.y == 0)
		return;
	g_main_window_state.needs_resize = true;
	g_main_window_state.resize_event_requested = true;
	g_main_window_state.pending_size = size;
}

void on_main_window_close(mars::window&) {
	g_main_window_state.close_requested = true;
}

void on_main_right_mouse_click(mars::input&) {
	g_main_window_state.right_click = true;
	g_main_window_state.right_click_event_requested = true;
}

void on_overview_window_resize(mars::window&, const mars::vector2<size_t>& size) {
	if (size.x == 0 || size.y == 0)
		return;
	g_overview_window_state.needs_resize = true;
	g_overview_window_state.pending_size = size;
}

void on_overview_window_close(mars::window&) {
	g_overview_window_state.close_requested = true;
}

void on_overview_window_mouse_change(mars::window&, const mars::vector2<size_t>&, const mars::window_event_buttons& click) {
	if (click.left_button)
		g_overview_window_state.capture_requested = true;
}

void on_overview_window_focus_changed(mars::window&, const bool& focused) {
	g_overview_window_state.focused = focused;
	if (!focused) {
		g_overview_window_state.capture_requested = false;
		release_overview_capture();
	}
}

enum app_backend_option {
	vulkan[[= mars::meta::display("vk")]] = 0,
	directx12[[= mars::meta::display("dx12")]] = 1,
};

struct app_console_flags {
	app_backend_option backend = app_backend_option::directx12;
};

bool event_targets_window(const SDL_Event& event, SDL_Window* sdl_window) {
	if (sdl_window == nullptr)
		return false;

	const Uint32 window_id = SDL_GetWindowID(sdl_window);
	switch (event.type) {
	case SDL_EVENT_WINDOW_RESIZED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		return event.window.windowID == window_id;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		return event.button.windowID == window_id;
	case SDL_EVENT_MOUSE_WHEEL:
		return event.wheel.windowID == window_id;
	case SDL_EVENT_KEY_DOWN:
		return event.key.windowID == window_id;
	default:
		return true;
	}
}

bool overview_window_has_focus(const overview_window_state& overview) {
	if (overview.events.window.sdl_window == nullptr)
		return false;
	return SDL_GetKeyboardFocus() == overview.events.window.sdl_window ||
		SDL_GetMouseFocus() == overview.events.window.sdl_window;
}

void release_overview_capture() {
	if (g_overview_window_state.events.window.sdl_window != nullptr) {
		SDL_SetWindowRelativeMouseMode(g_overview_window_state.events.window.sdl_window, false);
		SDL_SetWindowMouseGrab(g_overview_window_state.events.window.sdl_window, false);
		SDL_SetWindowKeyboardGrab(g_overview_window_state.events.window.sdl_window, false);
	}
	SDL_ShowCursor();
	g_overview_window_state.captured = false;
}

void update_overview_camera(rv::graph_runtime& runtime) {
	auto& overview = g_overview_window_state;
	if (overview.open)
		overview.focused = overview_window_has_focus(overview);
	else
		overview.focused = false;

	if (overview.capture_requested && overview.events.window.sdl_window != nullptr) {
		overview.capture_requested = false;
		SDL_RaiseWindow(overview.events.window.sdl_window);
		SDL_SetWindowMouseGrab(overview.events.window.sdl_window, true);
		SDL_SetWindowKeyboardGrab(overview.events.window.sdl_window, true);
		SDL_SetWindowRelativeMouseMode(overview.events.window.sdl_window, true);
		float discard_x = 0.0f;
		float discard_y = 0.0f;
		SDL_GetRelativeMouseState(&discard_x, &discard_y);
		SDL_HideCursor();
		overview.focused = true;
		overview.captured = true;
	}

	if (!overview.focused && overview.captured)
		release_overview_capture();

	const bool* keyboard_state = SDL_GetKeyboardState(nullptr);
	if (keyboard_state == nullptr)
		keyboard_state = overview.events.input.keystate;

	if (overview.captured && keyboard_state != nullptr && keyboard_state[SDL_SCANCODE_ESCAPE])
		release_overview_capture();

	auto& camera = overview.camera;
	if (overview.captured) {
		float relative_x = 0.0f;
		float relative_y = 0.0f;
		SDL_GetRelativeMouseState(&relative_x, &relative_y);
		camera.yaw += relative_x * camera.look_sensitivity;
		camera.pitch -= relative_y * camera.look_sensitivity;
		camera.pitch = std::clamp(camera.pitch, -1.5f, 1.5f);

		const mars::vector3<float> forward = mars::normalize(mars::vector3<float> {
			std::cos(camera.pitch) * std::sin(camera.yaw),
			std::sin(camera.pitch),
			std::cos(camera.pitch) * std::cos(camera.yaw)
		});
		const mars::vector3<float> world_up = { 0.0f, 1.0f, 0.0f };
		const mars::vector3<float> right = mars::normalize(mars::cross(world_up, forward));
		const mars::vector3<float> up = mars::cross(forward, right);
		const float move_step = camera.move_speed * overview.events.window.delta_time;

		if (keyboard_state != nullptr && keyboard_state[SDL_SCANCODE_W])
			camera.position = camera.position + forward * move_step;
		if (keyboard_state != nullptr && keyboard_state[SDL_SCANCODE_S])
			camera.position = camera.position - forward * move_step;
		if (keyboard_state != nullptr && keyboard_state[SDL_SCANCODE_D])
			camera.position = camera.position + right * move_step;
		if (keyboard_state != nullptr && keyboard_state[SDL_SCANCODE_A])
			camera.position = camera.position - right * move_step;
		if (keyboard_state != nullptr && keyboard_state[SDL_SCANCODE_E])
			camera.position = camera.position + up * move_step;
		if (keyboard_state != nullptr && keyboard_state[SDL_SCANCODE_Q])
			camera.position = camera.position - up * move_step;
	}

	const mars::vector2<size_t> window_size = overview.events.window.size.x > 0 && overview.events.window.size.y > 0
		? overview.events.window.size
		: g_main_window_state.window.size;
	const float width = static_cast<float>(std::max<size_t>(window_size.x, 1u));
	const float height = static_cast<float>(std::max<size_t>(window_size.y, 1u));
	const mars::vector3<float> forward = mars::normalize(mars::vector3<float> {
		std::cos(camera.pitch) * std::sin(camera.yaw),
		std::sin(camera.pitch),
		std::cos(camera.pitch) * std::cos(camera.yaw)
	});
	const mars::matrix4<float> view = mars::math::look_at(
		camera.position,
		camera.position + forward,
		mars::vector3<float> { 0.0f, 1.0f, 0.0f }
	);
	const mars::matrix4<float> projection = mars::math::perspective_fov_reversed_z(60.0f, width, height, 0.1f, 1000.0f);
	camera.view_proj = view * projection;

	runtime.set_blackboard("overview_captured", overview.captured);
	runtime.set_blackboard("camera_position", camera.position);
	runtime.set_blackboard("camera_yaw", camera.yaw);
	runtime.set_blackboard("camera_pitch", camera.pitch);
	runtime.set_blackboard("camera_move_speed", camera.move_speed);
	runtime.set_blackboard("camera_look_sensitivity", camera.look_sensitivity);
	runtime.set_blackboard("camera_view_proj", camera.view_proj);
}

void destroy_overview_window(const mars::device& device) {
	auto& overview = g_overview_window_state;
	if (!overview.open)
		return;

	release_overview_capture();
	mars::graphics::device_flush(device);
	overview.cmd_pool.reset();
	overview.main_pass.reset();
	overview.swapchain.reset();
	if (overview.events.window.sdl_window != nullptr)
		mars::graphics::window_destroy(overview.events.window);
	overview.events = {};
	overview.open = false;
	overview.close_requested = false;
	overview.needs_resize = false;
	overview.pending_size = {};
	overview.capture_requested = false;
	overview.focused = false;
}

void ensure_overview_window(mars::graphics_engine& engine, const mars::device& device) {
	auto& overview = g_overview_window_state;
	if (overview.open)
		return;

	overview.events.window = mars::graphics::window_create(engine, {"Overview", {1280, 720}});
	overview.events.window.listen<&mars::window_event::on_close, &on_overview_window_close>();
	overview.events.window.listen<&mars::window_event::on_resize, &on_overview_window_resize>();
	overview.events.window.listen<&mars::window_event::on_mouse_change, &on_overview_window_mouse_change>();
	overview.events.window.listen<&mars::window_event::on_focus_changed, &on_overview_window_focus_changed>();
	mars::input_create(overview.events.input, overview.events.window);

	overview.swapchain = std::make_unique<mars::graphics::object::swapchain>(
		mars::graphics::object::swapchain::create(device, overview.events.window)
	);
	overview.main_pass = std::make_unique<mars::graphics::object::main_pass_object<rv::main_pass_desc>>(device, *overview.swapchain);
	overview.cmd_pool = std::make_unique<mars::graphics::object::command_pool>(
		mars::graphics::object::command_pool::create(device, 1)
	);
	overview.open = true;
}

} // namespace

int main(int argc, char* argv[]) {
	mars::debug::install_crash_handlers();

	app_console_flags opts = mars::console::parse_arguments<app_console_flags>(
		std::vector<std::string_view>(argv + 1, argv + argc)
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
	rv::ui::set_file_dialog_parent(g_main_window_state.window.sdl_window);

	mars::input_create(g_main_window_state.input, g_main_window_state.window);
	g_main_window_state.input.listen<&mars::input_events::on_right_mouse_click, &on_main_right_mouse_click>();

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

		mars::imgui::initialize_backend(g_main_window_state.window, device, swapchain);

		mars::graphics::object::main_pass_object<rv::main_pass_desc> main_pass(device, swapchain);
		mars::graphics::object::command_pool cmd_pool = mars::graphics::object::command_pool::create(device, 1);

		mars::vector2<size_t> frame_size = g_main_window_state.window.size;

		NodeRegistry registry;
		register_default_pin_colors(registry);
		register_default_pin_types(registry);
		NodeGraph graph(registry);
		rv::ui::editor_state node_editor(graph);
		node_editor.font = ne_font;
		node_editor.font_size = 20.0f;
		{
			std::string load_error;
			const bool has_default_graph = std::filesystem::exists(std::filesystem::path(rv::default_graph_snapshot_path));
			if (has_default_graph && !rv::load_graph_from_file(graph, rv::default_graph_snapshot_path, &load_error)) {
				if (has_default_graph && !load_error.empty())
					mars::logger::warning(g_app_log_channel, "Failed to load '{}': {}. Starting with an empty graph.", rv::default_graph_snapshot_path, load_error);
			}
		}

		rv::graph_runtime runtime(graph, registry, device, main_pass, swapchain);
		runtime.attach_graph_callbacks();
		runtime.set_blackboard("window_size", frame_size);
		runtime.set_blackboard("camera_position", g_overview_window_state.camera.position);
		runtime.set_blackboard("camera_yaw", g_overview_window_state.camera.yaw);
		runtime.set_blackboard("camera_pitch", g_overview_window_state.camera.pitch);
		runtime.set_blackboard("camera_move_speed", g_overview_window_state.camera.move_speed);
		runtime.set_blackboard("camera_look_sensitivity", g_overview_window_state.camera.look_sensitivity);
		runtime.set_blackboard("camera_view_proj", g_overview_window_state.camera.view_proj);
		bool runtime_stop_requested = false;

		while (g_running) {
			std::vector<mars::window*> windows = { &g_main_window_state.window };
			if (g_overview_window_state.open)
				windows.push_back(&g_overview_window_state.events.window);

			mars::window_process_events(std::span<mars::window*>(windows), [&](const SDL_Event& event) {
				if (event_targets_window(event, g_main_window_state.window.sdl_window))
					mars::imgui::process_sdl_event(event);
			});

			if (g_main_window_state.needs_resize) {
				g_main_window_state.needs_resize = false;
				mars::graphics::device_flush(device);
				frame_size = g_main_window_state.pending_size;
				main_pass.release_framebuffers();
				swapchain.resize(frame_size);
				main_pass.recreate_framebuffers(device, swapchain);
				runtime.mark_dirty();
			}

			if (g_overview_window_state.open && g_overview_window_state.needs_resize) {
				g_overview_window_state.needs_resize = false;
				mars::graphics::device_flush(device);
				g_overview_window_state.main_pass->release_framebuffers();
				g_overview_window_state.swapchain->resize(g_overview_window_state.pending_size);
				g_overview_window_state.main_pass->recreate_framebuffers(device, *g_overview_window_state.swapchain);
			}

			runtime.set_blackboard("window_size", frame_size);
			if (g_main_window_state.resize_event_requested) {
				g_main_window_state.resize_event_requested = false;
				runtime.queue_event(rv::nodes::function_node_type_id<^^rv::nodes::WindowResize>());
			}
			if (g_main_window_state.right_click_event_requested) {
				g_main_window_state.right_click_event_requested = false;
				runtime.set_blackboard("right_mouse_clicked", true);
				runtime.queue_event(rv::nodes::function_node_type_id<^^rv::nodes::RightMouseClick>());
			}
			if (g_main_window_state.close_requested) {
				runtime.set_blackboard("window_close_requested", true);
				runtime.queue_event(rv::nodes::function_node_type_id<^^rv::nodes::WindowClose>());
			}

			update_overview_camera(runtime);

			const bool runtime_was_dirty_before_ui = runtime.is_dirty();
			mars::imgui::new_frame();

			rv::ui::app_shell_context ui_context {
				.editor = node_editor,
				.runtime = &runtime,
				.fps = imgui_io.Framerate,
				.overview_open = g_overview_window_state.open,
				.overview_captured = g_overview_window_state.captured,
				.right_click_signal = g_main_window_state.right_click,
				.right_click_held = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_RMASK) != 0
			};
			const rv::ui::editor_actions ui_actions = rv::ui::render_app_shell(ui_context);
			g_main_window_state.right_click = false;
			if (ui_actions.runtime_toggle_requested) {
				if (runtime.is_running())
					runtime_stop_requested = true;
				else
					runtime.start();
			}
			if (ui_actions.overview_toggle_requested) {
				if (g_overview_window_state.open)
					destroy_overview_window(device);
				else
					ensure_overview_window(engine, device);
			}
			if (ui_actions.save_requested) {
				std::string save_error;
				if (!rv::save_graph_to_file(graph, rv::default_graph_snapshot_path, &save_error))
					mars::logger::error(g_app_log_channel, "Failed to save '{}': {}", rv::default_graph_snapshot_path, save_error);
				else
					mars::logger::warning(g_app_log_channel, "Saved graph to '{}'", rv::default_graph_snapshot_path);
			}
			if (ui_actions.load_requested) {
				std::string load_error;
				if (!rv::load_graph_from_file(graph, rv::default_graph_snapshot_path, &load_error))
					mars::logger::error(g_app_log_channel, "Failed to load '{}': {}", rv::default_graph_snapshot_path, load_error);
				else {
					rv::ui::clear_selection(node_editor);
					runtime.mark_dirty();
					mars::logger::warning(g_app_log_channel, "Loaded graph from '{}'", rv::default_graph_snapshot_path);
				}
			}
			rv::nodes::refresh_dynamic_nodes(graph);

			const bool defer_runtime_rebuild_this_frame = !runtime_was_dirty_before_ui && runtime.is_dirty();
			if (!defer_runtime_rebuild_this_frame) {
				runtime.ensure_built(frame_size);
				runtime.tick(g_main_window_state.window.delta_time);
			}
			if (!g_main_window_state.close_requested)
				runtime.set_blackboard("right_mouse_clicked", false);
			ImGui::Render();

			const size_t back_buffer = swapchain.back_buffer_index();
			mars::graphics::object::command_buffer_recording frame_cmd(cmd_pool, device, "frame");
			if (!defer_runtime_rebuild_this_frame)
				runtime.record_pre_swapchain(frame_cmd.get(), back_buffer);

			{
				mars::raster_scope<rv::main_pass_desc> main_scope(frame_cmd.get(), main_pass.get_render_pass(), frame_size);
				main_scope.begin(main_pass.framebuffer(back_buffer), main_pass.depth(back_buffer), main_pass.default_bind_params(back_buffer));
				if (!defer_runtime_rebuild_this_frame)
					runtime.record_swapchain(main_scope, frame_size);
				mars::imgui::render_draw_data(frame_cmd.get());
			}

			frame_cmd.submit();

			if (main_pass.should_present())
				swapchain.present();

			if (g_overview_window_state.open) {
				const size_t overview_back_buffer = g_overview_window_state.swapchain->back_buffer_index();
				mars::graphics::object::command_buffer_recording overview_cmd(*g_overview_window_state.cmd_pool, device, "overview");
				{
					mars::raster_scope<rv::main_pass_desc> overview_scope(
						overview_cmd.get(),
						g_overview_window_state.main_pass->get_render_pass(),
						g_overview_window_state.events.window.size
					);
					overview_scope.begin(
						g_overview_window_state.main_pass->framebuffer(overview_back_buffer),
						g_overview_window_state.main_pass->depth(overview_back_buffer),
						g_overview_window_state.main_pass->default_bind_params(overview_back_buffer)
					);
					if (!defer_runtime_rebuild_this_frame)
						runtime.record_preview(overview_scope, g_overview_window_state.events.window.size);
				}
				overview_cmd.submit();
				if (g_overview_window_state.main_pass->should_present())
					g_overview_window_state.swapchain->present();
			}

			if (runtime_stop_requested) {
				runtime.stop();
				runtime_stop_requested = false;
			}
			if (g_overview_window_state.close_requested)
				destroy_overview_window(device);
			if (g_main_window_state.close_requested)
				g_running = false;
		}

		destroy_overview_window(device);
		mars::graphics::device_flush(device);
		runtime.destroy_all();
	}

	return EXIT_SUCCESS;
}
