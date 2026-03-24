#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/simple_nodes.hpp>

#include <render_visualizer/runtime/impl.hpp>

namespace rv::nodes {

bool index_buffer_node::build_index(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Upload";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	runtime_detail::index_buffer_resources resources;
	runtime_detail::resolved_value source = services->runtime->resolve_input_source(node, "indices");
	std::vector<std::byte> bytes;
	size_t count = 0;
	if (!services->runtime->copy_variable_payload(source, bytes, count)) {
		resources.error = "Unsupported index source.";
		error = resources.error + (source.status.empty() ? "" : (" " + source.status));
		result.status = error;
		return false;
	}

	resources.buffer = mars::graphics::buffer_create(*services->device, {
		.buffer_type = MARS_BUFFER_TYPE_INDEX,
		.buffer_property = MARS_BUFFER_PROPERTY_HOST_VISIBLE,
		.allocated_size = bytes.size(),
		.stride = sizeof(unsigned int)
	});
	if (!resources.buffer.engine) {
		resources.error = "Failed to allocate the index buffer.";
		error = resources.error;
		result.status = error;
		return false;
	}

	void* mapped = mars::graphics::buffer_map(resources.buffer, *services->device, bytes.size(), 0);
	if (mapped == nullptr) {
		resources.error = "Failed to map the index buffer.";
		resources.destroy(*services->device);
		error = resources.error;
		result.status = error;
		return false;
	}

	if (!bytes.empty())
		std::memcpy(mapped, bytes.data(), bytes.size());
	mars::graphics::buffer_unmap(resources.buffer, *services->device);

	resources.index_count = count;
	if (bytes.size() >= sizeof(unsigned int)) {
		const unsigned int* indices = reinterpret_cast<const unsigned int*>(bytes.data());
		const size_t index_value_count = bytes.size() / sizeof(unsigned int);
		for (size_t index = 0; index < index_value_count; ++index)
			resources.max_index = std::max(resources.max_index, indices[index]);
	}
	resources.valid = true;
	resources.error.clear();
	auto& stored = publish_owned_resource(*services, node, std::move(resources));
	if (!store_output_resource(*services, node, "index_buffer", &stored, error)) {
		result.status = error;
		return false;
	}
	result.executed_count = count;
	result.status = "Uploaded " + std::to_string(count) + " indices";
	return true;
}

void index_buffer_node::destroy(rv::graph_services& services, NE_Node& node) {
	destroy_current_owned_resource<runtime_detail::index_buffer_resources>(services, node);
}

} // namespace rv::nodes
