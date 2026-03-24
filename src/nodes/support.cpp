#include <render_visualizer/nodes/support.hpp>

namespace rv::nodes {

wildcard_type_info wildcard_type_from_pin(const NE_Pin& pin) {
	wildcard_type_info type;
	rv::detail::copy_matching_fields(type, pin);
	return type;
}

void apply_wildcard_type_to_pin(NE_Pin& pin, const wildcard_type_info& type) {
	rv::detail::copy_matching_fields(pin, type);
	pin.wildcard_resolved = type.valid();
}

bool wildcard_types_equal(const wildcard_type_info& lhs, const wildcard_type_info& rhs) {
	bool equal = true;
	constexpr auto ctx = std::meta::access_context::current();
	template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^wildcard_type_info, ctx)))
		equal = equal && (lhs.[:mem:] == rhs.[:mem:]);
	return equal;
}

const NE_Pin* find_pin_by_label(const std::vector<NE_Pin>& pins, std::string_view label) {
	const auto it = std::ranges::find(pins, label, &NE_Pin::label);
	return it == pins.end() ? nullptr : &*it;
}

const NE_Pin* find_pin_by_id(const std::vector<NE_Pin>& pins, int pin_id) {
	const auto it = std::ranges::find(pins, pin_id, &NE_Pin::id);
	return it == pins.end() ? nullptr : &*it;
}

void apply_variable_slot_metadata(NE_Pin& pin, const variable_slot_state& slot) {
	rv::detail::copy_matching_fields(pin, slot);
}


std::string pin_display_label(const NE_Pin& pin) {
	return pin.display_label.empty() ? pin.label : pin.display_label;
}

void clear_pin_template_metadata(NE_Pin& pin) {
	pin.display_label.clear();
	pin.template_base_type_hash = 0;
	pin.template_value_hash = 0;
	pin.template_display_name.clear();
}

void apply_pin_template_metadata(NE_Pin& pin, size_t base_type_hash, size_t value_hash, std::string display_name) {
	pin.template_base_type_hash = base_type_hash;
	pin.template_value_hash = value_hash;
	pin.template_display_name = std::move(display_name);
	pin.display_label = pin.template_display_name.empty()
		? pin.label
		: (pin.label + " <" + pin.template_display_name + ">");
}

bool equivalent_pin_layout(const std::vector<NE_Pin>& lhs, const std::vector<NE_Pin>& rhs) {
	if (lhs.size() != rhs.size())
		return false;
	constexpr auto ctx = std::meta::access_context::current();
	for (size_t index = 0; index < lhs.size(); ++index) {
		bool equal = true;
		template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^NE_Pin, ctx))) {
			if constexpr (std::meta::identifier_of(mem) != std::string_view("id") &&
						  std::meta::identifier_of(mem) != std::string_view("generated")) {
				equal = equal && (lhs[index].[:mem:] == rhs[index].[:mem:]);
			}
		}
		if (!equal)
			return false;
	}
	return true;
}

NodeTypeInfo make_basic_custom_node(size_t type, std::string title, std::vector<NE_Pin> inputs, std::vector<NE_Pin> outputs) {
	NodeTypeInfo info;
	info.meta.type = type;
	info.meta.title = std::move(title);
	info.pins.inputs = std::move(inputs);
	info.pins.outputs = std::move(outputs);
	return info;
}

} // namespace rv::nodes
