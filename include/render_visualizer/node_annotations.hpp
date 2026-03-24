#pragma once

#include <mars/hash/meta.hpp>

enum class enum_type {
	both,
	input_only,
	output_only,
	none
};

namespace rv::node {
	struct input {};
	struct output {};
	struct texture {};
	struct optional {};
	struct processor {};
	struct editor {};
	struct build {};
	struct destroy {};
	struct configure {};
	struct validate {};
	struct refresh {};
	struct on_connect {};
	struct save_state {};
	struct load_state {};
	struct start {};
	struct end_node {};
	struct function_outputs {};
	struct on_tick {};
	struct get_cpu_output {};
	struct build_propagate {};
	struct pure {};
	struct callable {};
	struct event {};
	struct wildcard_annotation {
		const char* group = "";
	};
	struct pin_flow_annotation {
		enum_type value;
	};

	template <size_t N>
	inline consteval wildcard_annotation wildcard(const char (&group)[N]) {
		return { std::define_static_string(std::string_view(group, N - 1)) };
	}

	inline consteval pin_flow_annotation pin_flow(enum_type value) {
		return { value };
	}
} // namespace rv::node

namespace rv::resource_tags {
	struct vertex_buffer {};
	struct index_buffer {};
	struct shader_module {};
	struct render_pass {};
	struct framebuffer {};
	struct depth_buffer {};
	struct graphics_pipeline {};
	struct color_texture {};
	struct texture_slot {};
	struct virtual_struct_schema {};
	struct uniform_resource {};
	struct material_resource {};
} // namespace rv::resource_tags
