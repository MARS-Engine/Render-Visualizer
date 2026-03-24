#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/gltf_node.hpp>

#include <mars/debug/logger.hpp>

namespace rv::nodes {

namespace {

mars::log_channel g_app_log_channel("app");

}

bool gltf_node::process(gltf_node& result, const file& selected_file) {
	if (selected_file.path.empty()) {
		mars::logger::warning(g_app_log_channel, "gltf_node.process called with an empty path");
		rv::detail::set_processor_status_message("No file selected.");
		return false;
	}

	std::filesystem::path asset_path(selected_file.path);
	std::error_code path_error;
	if (asset_path.is_relative())
		asset_path = std::filesystem::absolute(asset_path, path_error);
	if (path_error) {
		mars::logger::error(g_app_log_channel, "Failed to resolve glTF path '{}': {}", selected_file.path, path_error.message());
		rv::detail::set_processor_status_message("Failed to resolve glTF path: " + path_error.message());
		return false;
	}

	if (!std::filesystem::exists(asset_path, path_error) || path_error) {
		mars::logger::error(g_app_log_channel, "glTF file does not exist: '{}'", asset_path.string());
		rv::detail::set_processor_status_message("glTF file does not exist: " + asset_path.string());
		return false;
	}

	auto data = fastgltf::GltfDataBuffer::FromPath(asset_path);
	if (data.error() != fastgltf::Error::None) {
		mars::logger::error(g_app_log_channel, "Failed to read glTF '{}': fastgltf error {}", asset_path.string(), static_cast<int>(data.error()));
		rv::detail::set_processor_status_message("Failed to read glTF file.");
		return false;
	}

	fastgltf::Parser parser;
	auto asset = parser.loadGltf(data.get(), asset_path.parent_path(), fastgltf::Options::LoadExternalBuffers);
	if (asset.error() != fastgltf::Error::None) {
		mars::logger::error(g_app_log_channel, "Failed to parse glTF '{}': fastgltf error {}", asset_path.string(), static_cast<int>(asset.error()));
		rv::detail::set_processor_status_message("Failed to parse glTF file.");
		return false;
	}

	for (const auto& mesh : asset->meshes) {
		for (const auto& prim : mesh.primitives) {
			const size_t primitive_vertex_base = result.position.size();

			auto pos_it = prim.findAttribute("POSITION");
			if (pos_it != prim.attributes.end()) {
				const auto& acc = asset->accessors[pos_it->accessorIndex];
				fastgltf::iterateAccessor<fastgltf::math::fvec3>(asset.get(), acc, [&](const fastgltf::math::fvec3& v) {
					result.position.push_back({ v.x(), v.y(), v.z() });
				});
			}

			auto uv_it = prim.findAttribute("TEXCOORD_0");
			if (uv_it != prim.attributes.end()) {
				const auto& acc = asset->accessors[uv_it->accessorIndex];
				fastgltf::iterateAccessor<fastgltf::math::fvec2>(asset.get(), acc, [&](const fastgltf::math::fvec2& v) {
					result.uv.push_back({ v.x(), v.y() });
				});
			}

			if (prim.indicesAccessor.has_value()) {
				const auto& acc = asset->accessors[*prim.indicesAccessor];
				fastgltf::iterateAccessor<std::uint32_t>(asset.get(), acc, [&](std::uint32_t value) {
					result.indices.push_back(static_cast<unsigned int>(primitive_vertex_base + static_cast<size_t>(value)));
				});
			}
		}
	}

	if (result.position.empty()) {
		rv::detail::set_processor_status_message("glTF loaded but no POSITION attribute data was found.");
		return false;
	}

	rv::detail::set_processor_status_message(
		"Loaded " + std::to_string(result.position.size()) + " vertices, " +
		std::to_string(result.indices.size()) + " indices."
	);
	return true;
}

bool gltf_node::get_cpu_output(const NE_Node& node, std::string_view pin_label, std::vector<std::byte>& bytes, size_t& element_count) {
	if (node.runtime_value.storage == nullptr)
		return false;
	const gltf_node& data = node.runtime_value.as<gltf_node>();
	if (pin_label == "position") {
		element_count = data.position.size();
		bytes.resize(sizeof(mars::vector3<float>) * element_count);
		if (!bytes.empty())
			std::memcpy(bytes.data(), data.position.data(), bytes.size());
		return true;
	}
	if (pin_label == "uv") {
		element_count = data.uv.size();
		bytes.resize(sizeof(mars::vector2<float>) * element_count);
		if (!bytes.empty())
			std::memcpy(bytes.data(), data.uv.data(), bytes.size());
		return true;
	}
	if (pin_label == "indices") {
		element_count = data.indices.size();
		bytes.resize(sizeof(unsigned int) * element_count);
		if (!bytes.empty())
			std::memcpy(bytes.data(), data.indices.data(), bytes.size());
		return true;
	}
	return false;
}

} // namespace rv::nodes

namespace mars::imgui {

bool struct_editor<rv::nodes::file>::render(const std::string_view& label) {
	if (!this->ref)
		return false;

	bool changed = false;
	std::string text(label);
	const bool dialog_open = rv::nodes::ui::is_file_picker_open();
	ImGui::PushID(this->ref);
	changed |= rv::nodes::ui::input_text_string(text.c_str(), this->ref->path);
	ImGui::SameLine();
	if (dialog_open)
		ImGui::BeginDisabled();
	if (ImGui::Button("Browse..."))
		rv::nodes::ui::open_file_picker(this->ref->path);
	if (dialog_open)
		ImGui::EndDisabled();

	if (std::optional<std::string> selected = rv::nodes::ui::consume_selected_file_path(); selected.has_value()) {
		this->ref->path = *selected;
		changed = true;
	}
	ImGui::PopID();
	return changed;
}

} // namespace mars::imgui
