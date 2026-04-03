#pragma once

#include <render_visualizer/node/node_reflection.hpp>
#include <render_visualizer/node/node_registry.hpp>
#include <render_visualizer/type_reflection.hpp>

#include <memory>
#include <string>
#include <vector>
#include <imgui.h>

namespace rv {

inline bool inspect_property(const rv::variable*& _var_ptr, std::string_view _label, const std::vector<std::unique_ptr<rv::variable>>* _variables) {
	if (!_variables)
		return false;

	bool changed = false;
	std::string current_name = _var_ptr ? _var_ptr->name : "None";
	if (ImGui::BeginCombo(_label.data(), current_name.c_str())) {
		for (const auto& var : *_variables) {
			bool is_selected = (_var_ptr == var.get());
			if (ImGui::Selectable(var->name.c_str(), is_selected)) {
				_var_ptr = var.get();
				changed = true;
			}
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return changed;
}

struct [[=rv::node_hidden()]] get_variable_node {
	[[=rv::stack]]
	const rv::variable* var = nullptr;

	[[=rv::node_pure()]]

	[[=rv::pins_override]]
	void override_pins(std::vector<pin_draw_data>& /*_inputs*/, std::vector<pin_draw_data>& _outputs) const {
		if (!var || !var->type) return;
		_outputs.push_back({
			.name = var->name,
			.colour = var->type->colour,
			.type_hash = var->type->type_hash,
			.kind = pin_kind::data,
			.ops = {
				.resolve_value = [](mars::meta::type_erased_ptr _instance) -> mars::meta::type_erased_ptr {
					auto* node = static_cast<get_variable_node*>(_instance.get<void>());
					if (node && node->var && node->var->type && node->var->memory)
						return node->var->type->erase_ptr(node->var->memory);
					return {};
				},
				.copy_value = nullptr,
				.render_inspector = nullptr,
			}
		});
	}

	[[=rv::execute]]
	void run() {}
};

struct [[=rv::node_hidden()]] set_variable_node {
	[[=rv::stack]]
	const rv::variable* var = nullptr;

	[[=rv::pins_override]]
	void override_pins(std::vector<pin_draw_data>& _inputs, std::vector<pin_draw_data>& /*_outputs*/) const {
		if (!var || !var->type) return;
		_inputs.push_back({
			.name = var->name,
			.colour = var->type->colour,
			.type_hash = var->type->type_hash,
			.kind = pin_kind::data,
			.ops = {
				.resolve_value = [](mars::meta::type_erased_ptr _instance) -> mars::meta::type_erased_ptr {
					auto* node = static_cast<set_variable_node*>(_instance.get<void>());
					if (node && node->var && node->var->type && node->var->memory)
						return node->var->type->erase_ptr(node->var->memory);
					return {};
				},
				.copy_value = var->type->copy_value,
				.render_inspector = nullptr,
			}
		});
	}

	[[=rv::execute]]
	void run() {}
};

} // namespace rv

template const rv::node_registry::node_auto_registrar rv::auto_register_node_v<rv::get_variable_node>;
template const rv::node_registry::node_auto_registrar rv::auto_register_node_v<rv::set_variable_node>;
