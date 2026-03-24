#include <render_visualizer/nodes/virtual_struct.hpp>

namespace rv::nodes {

bool validate_virtual_struct_schema(const virtual_struct_schema_state& state, std::string& diagnostics) {
	if (state.name.empty()) {
		diagnostics = "Virtual struct name cannot be empty.";
		return false;
	}
	if (state.fields.empty()) {
		diagnostics = "Virtual struct must declare at least one field.";
		return false;
	}

	for (size_t index = 0; index < state.fields.size(); ++index) {
		const auto& field = state.fields[index];
		if (field.name.empty()) {
			diagnostics = "Field " + std::to_string(index) + " has no name.";
			return false;
		}
		if (virtual_struct_semantic(field).empty()) {
			diagnostics = "Field '" + field.name + "' has no semantic.";
			return false;
		}
		for (size_t other = index + 1; other < state.fields.size(); ++other) {
			if (state.fields[other].name == field.name) {
				diagnostics = "Field '" + field.name + "' is declared more than once.";
				return false;
			}
			if (virtual_struct_semantic(state.fields[other]) == virtual_struct_semantic(field)) {
				diagnostics = "Semantic '" + virtual_struct_semantic(field) + "' is declared more than once.";
				return false;
			}
		}
	}

	diagnostics = "Layout fingerprint " + std::to_string(virtual_struct_layout_fingerprint(state));
	return true;
}

virtual_struct_schema_state make_default_virtual_struct(std::string name) {
	virtual_struct_schema_state schema;
	schema.name = std::move(name);
	return schema;
}

int ensure_default_virtual_struct(NodeGraph& graph, std::string name) {
	if (virtual_struct_schema_state* schema = graph.create_virtual_struct(make_default_virtual_struct(std::move(name))); schema != nullptr)
		return schema->id;
	return -1;
}

} // namespace rv::nodes
