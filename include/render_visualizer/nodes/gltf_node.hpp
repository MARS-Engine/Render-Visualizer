#pragma once

#include <imgui.h>

#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/ui/widgets.hpp>

#include <mars/imgui/struct_editor.hpp>
#include <mars/math/vector2.hpp>
#include <mars/math/vector3.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rv::nodes {

struct file {
	std::string path;
};

struct gltf_node {
	[[=rv::node::output()]]
	std::vector<mars::vector3<float>> position;
	[[=rv::node::output()]]
	std::vector<mars::vector2<float>> uv;
	[[=rv::node::output()]]
	std::vector<unsigned int> indices;

	[[=rv::node::processor()]]
	static bool process(gltf_node& result, const file& selected_file);

	[[=rv::node::get_cpu_output()]]
	static bool get_cpu_output(const NE_Node& node, std::string_view pin_label, std::vector<std::byte>& bytes, size_t& element_count);
};

inline const NodeRegistry::node_auto_registrar gltf_node_registration(
	NodeRegistry::make_reflected_registration<gltf_node>()
);

} // namespace rv::nodes

namespace mars::imgui {
template <>
struct struct_editor<rv::nodes::file> : public struct_editor_base<rv::nodes::file> {
	using struct_editor_base<rv::nodes::file>::struct_editor_base;

	bool render(const std::string_view& label);
};
} // namespace mars::imgui
