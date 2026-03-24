#include <render_visualizer/ui/editor.hpp>

#include <imgui_internal.h>

#include <render_visualizer/nodes/function_graph_nodes.hpp>
#include <render_visualizer/nodes/shader_module_node.hpp>
#include <render_visualizer/runtime.hpp>
#include <render_visualizer/nodes/variable_nodes.hpp>
#include <render_visualizer/ui/runtime_panels.hpp>
#include <render_visualizer/ui/shared_editors.hpp>
#include <render_visualizer/ui/widgets.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>

using NodeEditorState = rv::ui::editor_state;

namespace {

constexpr float NE_NODE_W = 240.0f;
constexpr float NE_TITLE_H = 36.0f;
constexpr float NE_PIN_H = 30.0f;
constexpr float NE_PIN_PAD = 10.0f;
constexpr float NE_PIN_R = 8.0f;
constexpr float NE_GRID = 64.0f;
constexpr float NE_ZOOM_MIN = 0.2f;
constexpr float NE_ZOOM_MAX = 3.0f;

bool ne_pin_type_is_texture(size_t _type_hash) {
    return _type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned char>>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::texture_slot>();
}

bool ne_pin_type_is_resource(size_t _type_hash) {
    return _type_hash == rv::detail::pin_type_hash<rv::resource_tags::vertex_buffer>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::index_buffer>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::uniform_resource>() ||
        _type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned char>>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::texture_slot>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::shader_module>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::render_pass>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::framebuffer>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::depth_buffer>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::graphics_pipeline>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::material_resource>() ||
        _type_hash == rv::detail::pin_type_hash<rv::resource_tags::virtual_struct_schema>();
}

void ne_render_selectable_text_block(const char* id, const std::string& text, ImVec2 size) {
    std::vector<char> buffer(text.begin(), text.end());
    buffer.push_back('\0');
    ImGui::InputTextMultiline(
        id,
        buffer.data(),
        buffer.size(),
        size,
        ImGuiInputTextFlags_ReadOnly
    );
}

int ne_left_pin_count(const NE_Node& node) {
    return static_cast<int>(node.inputs.size()) + (node.has_exec_input ? 1 : 0);
}

int ne_right_pin_count(const NE_Node& node) {
    return static_cast<int>(node.outputs.size() + node.exec_outputs.size());
}

float ne_node_h(const NE_Node& node) {
    const int pins = std::max(ne_left_pin_count(node), ne_right_pin_count(node));
    return NE_TITLE_H + NE_PIN_PAD + pins * NE_PIN_H + NE_PIN_PAD;
}

mars::vector2<float> ne_c2s(mars::vector2<float> origin, mars::vector2<float> scroll, float zoom, mars::vector2<float> canvas_pos) {
    return { origin.x + scroll.x + canvas_pos.x * zoom, origin.y + scroll.y + canvas_pos.y * zoom };
}

mars::vector2<float> ne_s2c(mars::vector2<float> origin, mars::vector2<float> scroll, float zoom, mars::vector2<float> screen_pos) {
    return { (screen_pos.x - origin.x - scroll.x) / zoom, (screen_pos.y - origin.y - scroll.y) / zoom };
}

mars::vector2<float> ne_in_pin_pos(const NE_Node& node, int index) {
    const int offset = node.has_exec_input ? 1 : 0;
    return { node.pos.x, node.pos.y + NE_TITLE_H + NE_PIN_PAD + (offset + index + 0.5f) * NE_PIN_H };
}

mars::vector2<float> ne_out_pin_pos(const NE_Node& node, int index) {
    const int offset = static_cast<int>(node.exec_outputs.size());
    return { node.pos.x + NE_NODE_W, node.pos.y + NE_TITLE_H + NE_PIN_PAD + (offset + index + 0.5f) * NE_PIN_H };
}

mars::vector2<float> ne_exec_in_pin_pos(const NE_Node& node) {
    return { node.pos.x, node.pos.y + NE_TITLE_H + NE_PIN_PAD + 0.5f * NE_PIN_H };
}

mars::vector2<float> ne_exec_out_pin_pos(const NE_Node& node, int index) {
    return { node.pos.x + NE_NODE_W, node.pos.y + NE_TITLE_H + NE_PIN_PAD + (index + 0.5f) * NE_PIN_H };
}

bool ne_pin_hit(mars::vector2<float> pin_pos, float zoom, mars::vector2<float> mouse_pos) {
    const float dx = pin_pos.x - mouse_pos.x;
    const float dy = pin_pos.y - mouse_pos.y;
    const float radius = 14.0f * zoom;
    return dx * dx + dy * dy <= radius * radius;
}

float ne_pt_seg_dist2(mars::vector2<float> point, mars::vector2<float> segment_a, mars::vector2<float> segment_b) {
    const float vx = segment_b.x - segment_a.x;
    const float vy = segment_b.y - segment_a.y;
    const float wx = point.x - segment_a.x;
    const float wy = point.y - segment_a.y;
    const float c1 = vx * wx + vy * wy;
    if (c1 <= 0.0f)
        return (point.x - segment_a.x) * (point.x - segment_a.x) + (point.y - segment_a.y) * (point.y - segment_a.y);
    const float c2 = vx * vx + vy * vy;
    if (c2 <= c1)
        return (point.x - segment_b.x) * (point.x - segment_b.x) + (point.y - segment_b.y) * (point.y - segment_b.y);
    const float t = c1 / c2;
    const float qx = segment_a.x + t * vx;
    const float qy = segment_a.y + t * vy;
    return (point.x - qx) * (point.x - qx) + (point.y - qy) * (point.y - qy);
}

mars::vector2<float> ne_bezier_point(
    mars::vector2<float> p0,
    mars::vector2<float> p1,
    mars::vector2<float> p2,
    mars::vector2<float> p3,
    float t
) {
    const float u = 1.0f - t;
    const float b0 = u * u * u;
    const float b1 = 3.0f * u * u * t;
    const float b2 = 3.0f * u * t * t;
    const float b3 = t * t * t;
    return {
        b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
        b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y
    };
}

bool ne_bezier_hit(mars::vector2<float> a, mars::vector2<float> b, float zoom, mars::vector2<float> mouse_pos) {
    const float cx = std::min(120.0f * zoom, std::fabs(b.x - a.x) * 0.75f + 20.0f * zoom);
    const mars::vector2<float> p0 = a;
    const mars::vector2<float> p1 = { a.x + cx, a.y };
    const mars::vector2<float> p2 = { b.x - cx, b.y };
    const mars::vector2<float> p3 = b;

    mars::vector2<float> prev = p0;
    float best = 1e30f;
    for (int i = 1; i <= 24; ++i) {
        const float t = i / 24.0f;
        const mars::vector2<float> cur = ne_bezier_point(p0, p1, p2, p3, t);
        best = std::min(best, ne_pt_seg_dist2(mouse_pos, prev, cur));
        prev = cur;
    }
    const float tolerance = 7.0f * zoom;
    return best <= tolerance * tolerance;
}

bool ne_contains_case_insensitive(std::string_view haystack, std::string_view needle) {
    if (needle.empty())
        return true;

    auto lower = [](unsigned char ch) -> char {
        return static_cast<char>(std::tolower(ch));
    };

    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (lower(static_cast<unsigned char>(haystack[i + j])) != lower(static_cast<unsigned char>(needle[j]))) {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

} // namespace

ImU32 ne_to_imgui_color(mars::vector4<float> color) {
    return IM_COL32(
        static_cast<int>(color.x * 255.0f),
        static_cast<int>(color.y * 255.0f),
        static_cast<int>(color.z * 255.0f),
        static_cast<int>(color.w * 255.0f)
    );
}

void ne_bezier(ImDrawList* draw_list, mars::vector2<float> a, mars::vector2<float> b, float zoom, ImU32 color, float thickness = 2.5f) {
    float cx = std::min(120.0f * zoom, std::fabs(b.x - a.x) * 0.75f + 20.0f * zoom);
    draw_list->AddBezierCubic({ a.x, a.y }, { a.x + cx, a.y }, { b.x - cx, b.y }, { b.x, b.y }, color, thickness * zoom);
}

bool ne_selection_contains(const NodeEditorState& state, int node_id) {
    return std::ranges::find(state.selection.node_ids, node_id) != state.selection.node_ids.end();
}

bool ne_node_visible(const NodeEditorState& state, const NE_Node& node) {
    return node.function_id == state.graph.active_function_id();
}

bool ne_link_visible(const NodeEditorState& state, const NE_Link& link) {
    const NE_Node* from_node = state.graph.find_node(link.from_node);
    const NE_Node* to_node = state.graph.find_node(link.to_node);
    if (from_node == nullptr || to_node == nullptr)
        return false;
    return ne_node_visible(state, *from_node) && ne_node_visible(state, *to_node);
}

bool ne_render_function_missing_endpoint(const NodeEditorState& state) {
    if (state.graph.active_function_id() != state.graph.render_function_id())
        return false;

    for (const NE_Node* node : state.graph.nodes_in_function(state.graph.render_function_id())) {
        const NodeRegistry* registry = state.graph.node_registry();
        const NodeTypeInfo* info = registry != nullptr ? registry->find(node->type) : nullptr;
        if (info != nullptr && info->meta.is_end)
            return false;
    }
    return true;
}

bool ne_should_show_hidden_spawn_node(const NodeEditorState& state, const NodeTypeInfo& info) {
    if (!info.meta.is_end)
        return false;
    return ne_render_function_missing_endpoint(state);
}

const NE_Pin* ne_find_link_pin(const NE_Node& node, bool is_out, int pin_id) {
    if (is_out) {
        if (const NE_Pin* pin = rv::nodes::find_pin_by_id(node.exec_outputs, pin_id); pin != nullptr)
            return pin;
        return rv::nodes::find_pin_by_id(node.outputs, pin_id);
    }

    if (node.has_exec_input && node.exec_input.id == pin_id)
        return &node.exec_input;
    return rv::nodes::find_pin_by_id(node.inputs, pin_id);
}

void ne_clear_spawn_menu_link_context(NodeEditorState& state) {
    state.spawn_menu.from_link = false;
    state.spawn_menu.link_node_id = -1;
    state.spawn_menu.link_pin_id = -1;
    state.spawn_menu.link_is_output = false;
    state.spawn_menu.link_pin_kind = NE_PinKind::data;
}

const NE_Pin* ne_find_spawn_menu_drag_pin(const NodeEditorState& state) {
    if (!state.spawn_menu.from_link)
        return nullptr;
    const NE_Node* node = state.graph.find_node(state.spawn_menu.link_node_id);
    if (node == nullptr || node->function_id != state.graph.active_function_id())
        return nullptr;
    return ne_find_link_pin(*node, state.spawn_menu.link_is_output, state.spawn_menu.link_pin_id);
}

bool ne_are_spawn_pins_compatible(const NE_Pin& drag_pin, const NE_Pin& candidate, bool drag_is_output) {
    if (drag_pin.kind != candidate.kind)
        return false;
    if (drag_pin.kind == NE_PinKind::data) {
        const bool drag_unresolved_wildcard = drag_pin.is_wildcard && !drag_pin.wildcard_resolved;
        const bool candidate_unresolved_wildcard = candidate.is_wildcard && !candidate.wildcard_resolved;
        if (drag_unresolved_wildcard || candidate_unresolved_wildcard) {
            if (!drag_unresolved_wildcard && !candidate_unresolved_wildcard) {
                return drag_is_output
                    ? NodeGraph::is_data_link_compatible(drag_pin, candidate)
                    : NodeGraph::is_data_link_compatible(candidate, drag_pin);
            }

            const NE_Pin& resolved_pin = drag_unresolved_wildcard ? candidate : drag_pin;
            const NE_Pin& unresolved_pin = drag_unresolved_wildcard ? drag_pin : candidate;
            if (resolved_pin.has_virtual_struct && !unresolved_pin.has_virtual_struct && !unresolved_pin.is_wildcard)
                return false;
            return true;
        }
        return drag_is_output
            ? NodeGraph::is_data_link_compatible(drag_pin, candidate)
            : NodeGraph::is_data_link_compatible(candidate, drag_pin);
    }
    return true;
}

const NE_Pin* ne_find_first_compatible_type_pin(const NodeTypeInfo& info, const NE_Pin& drag_pin, bool drag_is_output) {
    if (drag_is_output) {
        if (drag_pin.kind == NE_PinKind::exec)
            return info.pins.has_exec_input ? &info.pins.exec_input : nullptr;
        for (const NE_Pin& candidate : info.pins.inputs) {
            if (ne_are_spawn_pins_compatible(drag_pin, candidate, drag_is_output))
                return &candidate;
        }
        return nullptr;
    }

    if (drag_pin.kind == NE_PinKind::exec)
        return info.pins.exec_outputs.empty() ? nullptr : &info.pins.exec_outputs.front();
    for (const NE_Pin& candidate : info.pins.outputs) {
        if (ne_are_spawn_pins_compatible(drag_pin, candidate, drag_is_output))
            return &candidate;
    }
    return nullptr;
}

const NE_Pin* ne_find_first_compatible_node_pin(const NE_Node& node, const NE_Pin& drag_pin, bool drag_is_output) {
    if (drag_is_output) {
        if (drag_pin.kind == NE_PinKind::exec)
            return node.has_exec_input ? &node.exec_input : nullptr;
        for (const NE_Pin& candidate : node.inputs) {
            if (ne_are_spawn_pins_compatible(drag_pin, candidate, drag_is_output))
                return &candidate;
        }
        return nullptr;
    }

    if (drag_pin.kind == NE_PinKind::exec)
        return node.exec_outputs.empty() ? nullptr : &node.exec_outputs.front();
    for (const NE_Pin& candidate : node.outputs) {
        if (ne_are_spawn_pins_compatible(drag_pin, candidate, drag_is_output))
            return &candidate;
    }
    return nullptr;
}

void ne_sync_primary_selection(NodeEditorState& state) {
    if (state.selection.node_ids.empty()) {
        state.selection.node_id = -1;
        return;
    }
    if (!ne_selection_contains(state, state.selection.node_id))
        state.selection.node_id = state.selection.node_ids.back();
}

void ne_clear_sidebar_selection(NodeEditorState& state) {
    state.selection.sidebar_kind = NodeEditorState::sidebar_selection_kind::none;
    state.selection.sidebar_id = -1;
}

void ne_clear_selection(NodeEditorState& state) {
    state.selection.node_ids.clear();
    state.selection.node_id = -1;
    ne_clear_sidebar_selection(state);
}

void ne_select_sidebar_entry(NodeEditorState& state, NodeEditorState::sidebar_selection_kind kind, int id) {
    state.selection.node_ids.clear();
    state.selection.node_id = -1;
    state.selection.sidebar_kind = kind;
    state.selection.sidebar_id = id;
}

void ne_set_single_selection(NodeEditorState& state, int node_id) {
    state.selection.node_ids.clear();
    if (node_id != -1)
        state.selection.node_ids.push_back(node_id);
    state.selection.node_id = node_id;
    if (node_id != -1)
        ne_clear_sidebar_selection(state);
}

void ne_add_selection(NodeEditorState& state, int node_id) {
    if (node_id == -1 || ne_selection_contains(state, node_id))
        return;
    state.selection.node_ids.push_back(node_id);
    state.selection.node_id = node_id;
    ne_clear_sidebar_selection(state);
}

void ne_remove_selection(NodeEditorState& state, int node_id) {
    state.selection.node_ids.erase(std::remove(state.selection.node_ids.begin(), state.selection.node_ids.end(), node_id), state.selection.node_ids.end());
    if (state.selection.node_id == node_id)
        ne_sync_primary_selection(state);
}

void ne_toggle_selection(NodeEditorState& state, int node_id) {
    if (ne_selection_contains(state, node_id))
        ne_remove_selection(state, node_id);
    else
        ne_add_selection(state, node_id);
}

void ne_prune_selection(NodeEditorState& state) {
    state.selection.node_ids.erase(std::remove_if(state.selection.node_ids.begin(), state.selection.node_ids.end(), [&](int node_id) {
        const NE_Node* node = state.graph.find_node(node_id);
        return node == nullptr || !ne_node_visible(state, *node);
    }), state.selection.node_ids.end());
    ne_sync_primary_selection(state);
    switch (state.selection.sidebar_kind) {
    case NodeEditorState::sidebar_selection_kind::function:
        if (state.graph.find_function(state.selection.sidebar_id) == nullptr)
            ne_clear_sidebar_selection(state);
        break;
    case NodeEditorState::sidebar_selection_kind::virtual_struct:
        if (state.graph.find_virtual_struct(state.selection.sidebar_id) == nullptr)
            ne_clear_sidebar_selection(state);
        break;
    case NodeEditorState::sidebar_selection_kind::shader_module: {
        const NE_Node* node = state.graph.find_node(state.selection.sidebar_id);
        if (node == nullptr ||
            node->type != rv::nodes::node_type_v<rv::nodes::shader_module_node_tag> ||
            !ne_node_visible(state, *node)) {
            ne_clear_sidebar_selection(state);
        }
        break;
    }
    case NodeEditorState::sidebar_selection_kind::texture_slot:
        if (state.graph.find_texture_slot(state.selection.sidebar_id) == nullptr)
            ne_clear_sidebar_selection(state);
        break;
    case NodeEditorState::sidebar_selection_kind::variable:
        if (state.graph.find_variable_slot(state.selection.sidebar_id) == nullptr)
            ne_clear_sidebar_selection(state);
        break;
    case NodeEditorState::sidebar_selection_kind::none:
    default:
        break;
    }
}

bool ne_rects_overlap(
    const mars::vector2<float>& a_min,
    const mars::vector2<float>& a_max,
    const mars::vector2<float>& b_min,
    const mars::vector2<float>& b_max
) {
    return a_min.x <= b_max.x && a_max.x >= b_min.x &&
        a_min.y <= b_max.y && a_max.y >= b_min.y;
}

mars::vector4<float> ne_resolve_pin_color(const NodeEditorState& state, const NE_Pin& pin) {
    if (pin.has_virtual_struct) {
        if (const auto* schema = state.graph.find_virtual_struct(pin.virtual_struct_name, pin.virtual_struct_layout_fingerprint); schema != nullptr)
            return schema->color;
    }
    if (const NodeRegistry* registry = state.graph.node_registry(); registry != nullptr)
        return registry->pin_color(pin.type_hash);
    return { 0.5f, 0.5f, 0.5f, 1.0f };
}

bool ne_node_has_incoming_link(const NodeGraph& graph, const NE_Node& node, int pin_id) {
    return std::any_of(graph.links.begin(), graph.links.end(), [&](const NE_Link& link) {
        const NE_Node* from_node = graph.find_node(link.from_node);
        const NE_Node* to_node = graph.find_node(link.to_node);
        return from_node != nullptr &&
            to_node != nullptr &&
            from_node->function_id == node.function_id &&
            to_node->function_id == node.function_id &&
            link.to_node == node.id &&
            link.to_pin == pin_id;
    });
}

NE_InlineInputValue* ne_find_inline_input_value(NE_Node& node, std::string_view label) {
    for (auto& value : node.inline_input_values) {
        if (value.label == label)
            return &value;
    }
    return nullptr;
}

const NE_InlineInputValue* ne_find_inline_input_value(const NE_Node& node, std::string_view label) {
    for (const auto& value : node.inline_input_values) {
        if (value.label == label)
            return &value;
    }
    return nullptr;
}

bool ne_supports_inline_input_type_hash(size_t type_hash) {
    return type_hash == rv::detail::pin_type_hash<float>() ||
        type_hash == rv::detail::pin_type_hash<mars::vector2<float>>() ||
        type_hash == rv::detail::pin_type_hash<mars::vector3<float>>() ||
        type_hash == rv::detail::pin_type_hash<mars::vector4<float>>() ||
        type_hash == rv::detail::pin_type_hash<unsigned int>() ||
        type_hash == rv::detail::pin_type_hash<mars::vector2<unsigned int>>() ||
        type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned int>>() ||
        type_hash == rv::detail::pin_type_hash<mars::vector4<unsigned int>>() ||
        type_hash == rv::detail::pin_type_hash<bool>() ||
        type_hash == rv::detail::pin_type_hash<std::string>();
}

std::string ne_default_inline_input_json(const NE_Pin& pin) {
    if (pin.type_hash == rv::detail::pin_type_hash<float>()) {
        float value = 0.0f;
        return rv::nodes::json_stringify(value);
    }
    if (pin.type_hash == rv::detail::pin_type_hash<mars::vector2<float>>()) {
        mars::vector2<float> value = {};
        return rv::nodes::json_stringify(value);
    }
    if (pin.type_hash == rv::detail::pin_type_hash<mars::vector3<float>>()) {
        mars::vector3<float> value = {};
        return rv::nodes::json_stringify(value);
    }
    if (pin.type_hash == rv::detail::pin_type_hash<mars::vector4<float>>()) {
        mars::vector4<float> value = {};
        return rv::nodes::json_stringify(value);
    }
    if (pin.type_hash == rv::detail::pin_type_hash<unsigned int>()) {
        unsigned int value = 0;
        return rv::nodes::json_stringify(value);
    }
    if (pin.type_hash == rv::detail::pin_type_hash<mars::vector2<unsigned int>>()) {
        mars::vector2<unsigned int> value = {};
        return rv::nodes::json_stringify(value);
    }
    if (pin.type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned int>>()) {
        mars::vector3<unsigned int> value = {};
        return rv::nodes::json_stringify(value);
    }
    if (pin.type_hash == rv::detail::pin_type_hash<mars::vector4<unsigned int>>()) {
        mars::vector4<unsigned int> value = {};
        return rv::nodes::json_stringify(value);
    }
    if (pin.type_hash == rv::detail::pin_type_hash<bool>()) {
        bool value = false;
        return rv::nodes::json_stringify(value);
    }
    if (pin.type_hash == rv::detail::pin_type_hash<std::string>()) {
        std::string value;
        return rv::nodes::json_stringify(value);
    }
    return {};
}

bool ne_pin_supports_inline_editor(const NE_Pin& pin) {
    if (pin.kind != NE_PinKind::data || ne_pin_type_is_resource(pin.type_hash) || pin.is_container)
        return false;
    return ne_supports_inline_input_type_hash(pin.type_hash);
}

bool ne_node_has_inline_input_value(const NE_Node& node, const NE_Pin& pin) {
    const NE_InlineInputValue* value = ne_find_inline_input_value(node, pin.label);
    return value != nullptr && value->enabled;
}

bool ne_render_inline_input_editor(NE_Node& node, const NE_Pin& pin) {
    NE_InlineInputValue* inline_value = ne_find_inline_input_value(node, pin.label);
    if (inline_value == nullptr) {
        node.inline_input_values.push_back({ .label = pin.label, .json = ne_default_inline_input_json(pin), .enabled = true });
        inline_value = &node.inline_input_values.back();
    } else if (!inline_value->enabled) {
        inline_value->enabled = true;
        if (inline_value->json.empty())
            inline_value->json = ne_default_inline_input_json(pin);
    }

    bool changed = false;
    const std::string imgui_label = pin.label + "##inline_input";
    if (pin.type_hash == rv::detail::pin_type_hash<float>()) {
        float value = 0.0f;
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    } else if (pin.type_hash == rv::detail::pin_type_hash<mars::vector2<float>>()) {
        mars::vector2<float> value = {};
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    } else if (pin.type_hash == rv::detail::pin_type_hash<mars::vector3<float>>()) {
        mars::vector3<float> value = {};
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    } else if (pin.type_hash == rv::detail::pin_type_hash<mars::vector4<float>>()) {
        mars::vector4<float> value = {};
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    } else if (pin.type_hash == rv::detail::pin_type_hash<unsigned int>()) {
        unsigned int value = 0;
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    } else if (pin.type_hash == rv::detail::pin_type_hash<mars::vector2<unsigned int>>()) {
        mars::vector2<unsigned int> value = {};
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    } else if (pin.type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned int>>()) {
        mars::vector3<unsigned int> value = {};
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    } else if (pin.type_hash == rv::detail::pin_type_hash<mars::vector4<unsigned int>>()) {
        mars::vector4<unsigned int> value = {};
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    } else if (pin.type_hash == rv::detail::pin_type_hash<bool>()) {
        bool value = false;
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    } else if (pin.type_hash == rv::detail::pin_type_hash<std::string>()) {
        std::string value;
        if (!inline_value->json.empty())
            rv::nodes::json_parse(inline_value->json, value);
        changed = rv::ui::render_typed_value_editor(imgui_label.c_str(), value);
        if (changed) inline_value->json = rv::nodes::json_stringify(value);
    }

    return changed;
}

std::string ne_missing_required_input_message(const NodeEditorState& state, const NE_Node& node) {
    std::vector<std::string> missing_labels;
    missing_labels.reserve(node.inputs.size());
    for (const auto& pin : node.inputs) {
        if (!pin.required)
            continue;
        if (!ne_node_has_incoming_link(state.graph, node, pin.id) &&
            !(ne_pin_supports_inline_editor(pin) && ne_node_has_inline_input_value(node, pin))) {
            missing_labels.push_back(pin.label);
        }
    }

    if (missing_labels.empty())
        return {};
    if (missing_labels.size() == 1)
        return "Missing input: " + missing_labels.front();

    std::string result = "Missing inputs: ";
    for (size_t index = 0; index < missing_labels.size(); ++index) {
        if (index != 0)
            result += ", ";
        result += missing_labels[index];
    }
    return result;
}

std::string ne_resolve_node_error_message(const NodeEditorState& state, const NE_Node& node) {
    std::string missing_input_message = ne_missing_required_input_message(state, node);
    if (!missing_input_message.empty())
        return missing_input_message;
    if (node.has_run_result && !node.last_run_success)
        return node.last_run_message.empty() ? std::string("Node failed.") : node.last_run_message;
    return {};
}

std::string ne_pin_type_name(const NodeEditorState&, const NE_Pin& pin) {
    if (pin.kind == NE_PinKind::exec)
        return "exec";

    for (const auto& descriptor : rv::nodes::variable_type_descriptors()) {
        if (descriptor.type_hash == pin.type_hash &&
            descriptor.is_container == pin.is_container) {
            std::string result = descriptor.label;
            if (pin.has_virtual_struct && !pin.virtual_struct_name.empty())
                result += " <" + pin.virtual_struct_name + ">";
            return result;
        }
    }

    if (pin.has_virtual_struct && !pin.virtual_struct_name.empty())
        return pin.virtual_struct_name;

    if (const auto* descriptor = rv::nodes::find_virtual_struct_type_descriptor(pin.type_hash); descriptor != nullptr) {
        std::string result = std::string(descriptor->name);
        if (pin.is_container)
            result += "[]";
        return result;
    }

    return "Unknown";
}

std::string ne_make_unique_variable_name(const NodeGraph& graph, std::string base_name) {
    if (base_name.empty())
        base_name = "NewVariable";
    for (char& c : base_name) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            c = '_';
    }

    std::string candidate = base_name;
    int suffix = 1;
    auto exists = [&](std::string_view name) {
        return std::any_of(graph.variable_slots.begin(), graph.variable_slots.end(), [&](const auto& slot) {
            return slot.name == name;
        });
    };
    while (exists(candidate))
        candidate = base_name + "_" + std::to_string(suffix++);
    return candidate;
}

bool ne_can_promote_pin_to_variable(const NE_Pin& pin) {
    if (pin.kind != NE_PinKind::data)
        return false;
    for (const auto& descriptor : rv::nodes::variable_type_descriptors()) {
        if (descriptor.type_hash == pin.type_hash &&
            descriptor.is_container == pin.is_container) {
            return true;
        }
    }
    return false;
}

int ne_create_variable_from_pin(NodeEditorState& state, const NE_Pin& pin) {
    rv::nodes::variable_slot_state slot = rv::nodes::make_default_variable_slot(
        ne_make_unique_variable_name(state.graph, "NewVariable_" + pin.label)
    );
    slot.type_hash = pin.type_hash;
    slot.is_container = pin.is_container;
    slot.has_virtual_struct = pin.has_virtual_struct;
    slot.virtual_struct_name = pin.virtual_struct_name;
    slot.virtual_struct_layout_fingerprint = pin.virtual_struct_layout_fingerprint;
    slot.template_base_type_hash = pin.template_base_type_hash;
    slot.template_value_hash = pin.template_value_hash;
    slot.template_display_name = pin.template_display_name;
    rv::nodes::reset_variable_slot_default(slot);

    if (rv::nodes::variable_slot_state* created = state.graph.create_variable_slot(std::move(slot)); created != nullptr)
        return created->id;
    return -1;
}

void ne_promote_pin_to_variable(NodeEditorState& state, int node_id, int pin_id, bool is_output) {
    NE_Node* node = state.graph.find_node(node_id);
    if (node == nullptr)
        return;

    const NE_Pin* pin = is_output
        ? rv::nodes::find_pin_by_id(node->outputs, pin_id)
        : rv::nodes::find_pin_by_id(node->inputs, pin_id);
    if (pin == nullptr || !ne_can_promote_pin_to_variable(*pin))
        return;

    const int variable_id = ne_create_variable_from_pin(state, *pin);
    if (variable_id == -1)
        return;

    if (is_output) {
        NE_Node* set_node = rv::nodes::spawn_bound_variable_node<rv::nodes::variable_set_node_tag>(
            state.graph, variable_id, { node->pos.x + 240.0f + 80.0f, node->pos.y }
        );
        if (set_node == nullptr)
            return;
        const NE_Pin* value_pin = rv::nodes::find_pin_by_label(set_node->inputs, "value");
        if (value_pin != nullptr)
            state.graph.add_link({ .from_node = node->id, .from_pin = pin->id, .to_node = set_node->id, .to_pin = value_pin->id });
        if (!node->exec_outputs.empty() && set_node->has_exec_input)
            state.graph.add_link({ .from_node = node->id, .from_pin = node->exec_outputs.front().id, .to_node = set_node->id, .to_pin = set_node->exec_input.id });
    } else {
        NE_Node* get_node = rv::nodes::spawn_bound_variable_node<rv::nodes::variable_get_node_tag>(
            state.graph, variable_id, { node->pos.x - 240.0f - 80.0f, node->pos.y }
        );
        if (get_node == nullptr)
            return;
        if (!get_node->outputs.empty())
            state.graph.add_link({ .from_node = get_node->id, .from_pin = get_node->outputs.front().id, .to_node = node->id, .to_pin = pin->id });
    }
}

void ne_draw_splitter_x(const char* id, float thickness, float* size0, float min_size0, float max_size0, float delta_sign = 1.0f) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton(id, ImVec2(thickness, avail.y));
    const float low = std::min(min_size0, max_size0);
    const float high = std::max(min_size0, max_size0);
    if (ImGui::IsItemActive())
        *size0 = std::clamp(*size0 + (ImGui::GetIO().MouseDelta.x * delta_sign), low, high);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::IsItemActive() || ImGui::IsItemHovered() ? IM_COL32(170, 170, 180, 255) : IM_COL32(92, 92, 100, 220);
    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + thickness, cursor.y + avail.y), color);
}

void ne_draw_splitter_y(const char* id, float thickness, float* size0, float min_size0, float max_size0) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton(id, ImVec2(avail.x, thickness));
    const float low = std::min(min_size0, max_size0);
    const float high = std::max(min_size0, max_size0);
    if (ImGui::IsItemActive())
        *size0 = std::clamp(*size0 + ImGui::GetIO().MouseDelta.y, low, high);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::IsItemActive() || ImGui::IsItemHovered() ? IM_COL32(170, 170, 180, 255) : IM_COL32(92, 92, 100, 220);
    draw_list->AddRectFilled(cursor, ImVec2(cursor.x + avail.x, cursor.y + thickness), color);
}

void ne_render_types_panel(NodeEditorState& state) {
    ImGui::TextUnformatted("Functions");
    ImGui::SameLine();
    if (ImGui::SmallButton("Add Function")) {
        if (graph_function_definition* function = state.graph.create_function("Function"); function != nullptr) {
            rv::nodes::ensure_function_boundary_nodes(state.graph);
            state.graph.set_active_function(function->id);
            ne_select_sidebar_entry(state, NodeEditorState::sidebar_selection_kind::function, function->id);
        }
    }
    ImGui::Separator();

    for (auto& function : state.graph.functions) {
        ImGui::PushID(function.id);
        const bool selected =
            state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::function
            ? state.selection.sidebar_id == function.id
            : (state.selection.node_id == -1 && function.id == state.graph.active_function_id());
        if (ImGui::Selectable(function.name.c_str(), selected)) {
            state.graph.set_active_function(function.id);
            ne_select_sidebar_entry(state, NodeEditorState::sidebar_selection_kind::function, function.id);
        }
        if (!state.graph.is_builtin_function(function.id)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                const bool was_active = function.id == state.graph.active_function_id();
                state.graph.remove_function(function.id);
                if (was_active)
                    ne_clear_selection(state);
                ImGui::PopID();
                break;
            }
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", function.id == state.graph.setup_function_id() ? "(setup)" : "(render)");
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Types");
    ImGui::SameLine();
    if (ImGui::SmallButton("+"))
        ImGui::OpenPopup("##ne_shared_asset_add");
    ImGui::SameLine();
    if (ImGui::SmallButton("Save"))
        state.request_save = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Load"))
        state.request_load = true;
    if (ImGui::BeginPopup("##ne_shared_asset_add")) {
        if (ImGui::MenuItem("Struct")) {
            const int suffix = static_cast<int>(state.graph.virtual_structs.size());
            rv::nodes::ensure_default_virtual_struct(state.graph, std::string("Struct") + std::to_string(suffix));
        }
        if (ImGui::MenuItem("Texture")) {
            const int suffix = static_cast<int>(state.graph.texture_slots.size());
            rv::nodes::ensure_default_texture_slot(state.graph, std::string("Texture") + std::to_string(suffix));
        }
        if (ImGui::MenuItem("Variable")) {
            const int suffix = static_cast<int>(state.graph.variable_slots.size());
            rv::nodes::ensure_default_variable_slot(state.graph, std::string("Variable") + std::to_string(suffix));
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    ImGui::TextUnformatted("Structs");
    bool any_type = false;
    for (auto& schema : state.graph.virtual_structs) {
        any_type = true;
        std::string diagnostics;
        const bool valid = rv::nodes::validate_virtual_struct_schema(schema, diagnostics);
        ImGui::PushID(schema.id);
        ImGui::ColorButton("##type_color", ImVec4(schema.color.x, schema.color.y, schema.color.z, schema.color.w),
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(12.0f, 12.0f));
        ImGui::SameLine();
        const std::string header = schema.name.empty() ? std::string("Unnamed Struct") : schema.name;
        if (ImGui::Selectable(header.c_str(), state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::virtual_struct && state.selection.sidebar_id == schema.id))
            ne_select_sidebar_entry(state, NodeEditorState::sidebar_selection_kind::virtual_struct, schema.id);
        if (!valid) {
            ImGui::SameLine();
            ImGui::TextDisabled("invalid");
        }
        ImGui::PopID();
    }

    if (!any_type)
        ImGui::TextDisabled("No virtual struct types");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Textures");

    bool any_texture = false;
    ImGui::PushID("shared_textures");
    for (auto& slot : state.graph.texture_slots) {
        any_texture = true;
        ImGui::PushID(slot.id);
        const std::string header = slot.name.empty() ? std::string("Unnamed Texture") : slot.name;
        if (ImGui::Selectable(header.c_str(), state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::texture_slot && state.selection.sidebar_id == slot.id))
            ne_select_sidebar_entry(state, NodeEditorState::sidebar_selection_kind::texture_slot, slot.id);
        ImGui::PopID();
    }
    ImGui::PopID();

    if (!any_texture)
        ImGui::TextDisabled("No shared texture slots");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Shader Modules");

    bool any_shader_module = false;
    ImGui::PushID("shared_shader_modules");
    for (auto& node : state.graph.nodes) {
        if (node.type != rv::nodes::node_type_v<rv::nodes::shader_module_node_tag>)
            continue;
        any_shader_module = true;
        ImGui::PushID(node.id);
        const std::string header = node.title.empty() ? std::string("Unnamed Shader Module") : node.title;
        if (ImGui::Selectable(header.c_str(), state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::shader_module && state.selection.sidebar_id == node.id))
            ne_select_sidebar_entry(state, NodeEditorState::sidebar_selection_kind::shader_module, node.id);
        ImGui::PopID();
    }
    ImGui::PopID();

    if (!any_shader_module)
        ImGui::TextDisabled("No shader modules");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Variables");

    bool any_variable = false;
    ImGui::PushID("shared_variables");
    for (auto& slot : state.graph.variable_slots) {
        any_variable = true;
        ImGui::PushID(slot.id);
        NE_Pin variable_pin {};
        variable_pin.type_hash = slot.type_hash;
        variable_pin.is_container = slot.is_container;
        variable_pin.has_virtual_struct = slot.has_virtual_struct;
        variable_pin.virtual_struct_name = slot.virtual_struct_name;
        variable_pin.virtual_struct_layout_fingerprint = slot.virtual_struct_layout_fingerprint;
        ImU32 color = ne_to_imgui_color(ne_resolve_pin_color(state, variable_pin));
        ImGui::ColorButton("##variable_color", ImGui::ColorConvertU32ToFloat4(color),
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(12.0f, 12.0f));
        ImGui::SameLine();
        const std::string header = slot.name.empty() ? std::string("Unnamed Variable") : slot.name;
        if (ImGui::Selectable(header.c_str(), state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::variable && state.selection.sidebar_id == slot.id))
            ne_select_sidebar_entry(state, NodeEditorState::sidebar_selection_kind::variable, slot.id);
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            const int variable_id = slot.id;
            ImGui::SetDragDropPayload("NE_VARIABLE_SLOT", &variable_id, sizeof(variable_id));
            ImGui::TextUnformatted(slot.name.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
    ImGui::PopID();

    if (!any_variable)
        ImGui::TextDisabled("No shared variables");
}

void ne_render_selected_node_sidebar(NodeGraph& graph, const NodeRegistry& registry, int selected_node_id) {
    if (selected_node_id == -1) {
        ImGui::TextDisabled("No node selected");
        return;
    }

    NE_Node* node = graph.find_node(selected_node_id);
    if (node == nullptr) {
        ImGui::TextDisabled("Selected node missing");
        return;
    }

    const NodeTypeInfo* info = registry.find(node->type);
    if (info == nullptr) {
        ImGui::TextDisabled("Unknown node type");
        return;
    }

    if (info->hooks.render_selected_sidebar) {
        info->hooks.render_selected_sidebar(graph, *info, *node);
        return;
    }

    ImGui::TextUnformatted(node->title.c_str());
    ImGui::Separator();
    ImGui::Text("Type: %zu", node->type);
    ImGui::Text("Inputs: %d", static_cast<int>(node->inputs.size()));
    ImGui::Text("Outputs: %d", static_cast<int>(node->outputs.size()));
    if (!node->processor_params.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Parameters");
        bool changed = false;
        for (auto& param : node->processor_params)
            changed |= param.render_editor();
        if (changed)
            graph.notify_graph_dirty();

        if (node->has_run_result) {
            ImGui::Spacing();
            ImGui::TextDisabled("Last run: %s", node->last_run_success ? "success" : "failed");
            if (!node->last_run_message.empty())
                ImGui::TextWrapped("%s", node->last_run_message.c_str());
        }
    }
}

void ne_render_selection_editor_panel(NodeEditorState& state, const rv::ui::runtime_panel_context& runtime_panel, bool& runtime_toggle_requested) {
    ImGui::TextUnformatted("Inspector");
    const bool runtime_running = runtime_panel.runtime != nullptr && runtime_panel.runtime->is_running();
    runtime_toggle_requested = ImGui::Button(runtime_running ? "Stop" : "Start", { -1.0f, 0.0f });
    ImGui::Separator();
    if (state.selection.node_ids.size() > 1) {
        ImGui::Text("%zu nodes selected", state.selection.node_ids.size());
        ImGui::TextDisabled("Multi-edit is not implemented yet.");
    } else if (const NodeRegistry* registry = state.graph.node_registry(); registry != nullptr) {
        if (state.selection.node_id == -1) {
            const graph_function_definition* selected_function =
                state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::function
                ? state.graph.find_function(state.selection.sidebar_id)
                : state.graph.find_function(state.graph.active_function_id());

            if (state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::virtual_struct) {
                if (auto* schema = state.graph.find_virtual_struct(state.selection.sidebar_id); schema != nullptr) {
                    ImGui::TextUnformatted(schema->name.empty() ? "Unnamed Struct" : schema->name.c_str());
                    ImGui::Separator();
                    rv::ui::render_virtual_struct_editor(state.graph, *schema);
                    ImGui::TextDisabled("Fingerprint: %zu", rv::nodes::virtual_struct_layout_fingerprint(*schema));
                    std::string diagnostics;
                    if (!rv::nodes::validate_virtual_struct_schema(*schema, diagnostics))
                        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", diagnostics.c_str());
                } else {
                    ImGui::TextDisabled("No struct selected");
                }
            } else if (state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::shader_module) {
                if (auto* node = state.graph.find_node(state.selection.sidebar_id);
                    node != nullptr &&
                    node->type == rv::nodes::node_type_v<rv::nodes::shader_module_node_tag> &&
                    node->custom_state.storage != nullptr) {
                    auto& shader_state = node->custom_state.as<rv::nodes::shader_module_state>();
                    ImGui::TextUnformatted(node->title.empty() ? "Unnamed Shader Module" : node->title.c_str());
                    ImGui::Separator();
                    if (const graph_function_definition* function = state.graph.find_function(node->function_id); function != nullptr)
                        ImGui::TextDisabled("Function: %s", function->name.c_str());
                    ImGui::TextDisabled(shader_state.last_compile_success ? "Compiled" : "Not compiled");
                    if (!shader_state.diagnostics.empty()) {
                        ImGui::Spacing();
                        ImGui::TextUnformatted("Diagnostics");
                        ImGui::BeginChild("##shader_sidebar_diagnostics", { 0.0f, 110.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
                        ne_render_selectable_text_block("##shader_sidebar_diagnostics_text", shader_state.diagnostics, { -FLT_MIN, -FLT_MIN });
                        ImGui::EndChild();
                    }
                    ImGui::Spacing();
                    ImGui::TextUnformatted("Reflected Inputs");
                    if (shader_state.reflected_inputs.empty()) {
                        ImGui::TextDisabled("No reflected vertex inputs.");
                    } else {
                        for (const auto& input : shader_state.reflected_inputs) {
                            const std::string type_name = ne_pin_type_name(state, input);
                            ImGui::BulletText("%s : %s", input.label.c_str(), type_name.c_str());
                        }
                    }
                } else {
                    ImGui::TextDisabled("No shader module selected");
                }
            } else if (state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::texture_slot) {
                if (auto* slot = state.graph.find_texture_slot(state.selection.sidebar_id); slot != nullptr) {
                    ImGui::TextUnformatted(slot->name.empty() ? "Unnamed Texture" : slot->name.c_str());
                    ImGui::Separator();
                    rv::ui::render_texture_slot_editor(state.graph, *slot);
                } else {
                    ImGui::TextDisabled("No texture selected");
                }
            } else if (state.selection.sidebar_kind == NodeEditorState::sidebar_selection_kind::variable) {
                if (auto* slot = state.graph.find_variable_slot(state.selection.sidebar_id); slot != nullptr) {
                    ImGui::TextUnformatted(slot->name.empty() ? "Unnamed Variable" : slot->name.c_str());
                    ImGui::Separator();
                    rv::ui::render_variable_slot_editor(state.graph, *slot);
                } else {
                    ImGui::TextDisabled("No variable selected");
                }
            } else if (selected_function != nullptr) {
                if (graph_function_definition* function = state.graph.find_function(selected_function->id); function != nullptr) {
                    ImGui::TextUnformatted(function->name.c_str());
                    ImGui::Separator();
                    if (!state.graph.is_builtin_function(function->id))
                        rv::ui::input_text_string("Name", function->name);
                    ImGui::TextDisabled("%s", function->id == state.graph.setup_function_id() ? "Runs once when simulation starts." :
                        function->id == state.graph.render_function_id() ? "Runs every frame and must present to swapchain." :
                        "Custom callable function.");
                    ImGui::Text("Nodes: %zu", state.graph.nodes_in_function(function->id).size());
                    if (!state.graph.is_builtin_function(function->id)) {
                        ImGui::Separator();
                        ImGui::TextUnformatted("Inputs");
                        bool changed = rv::ui::render_function_signature_editor(function->inputs, "Input");
                        ImGui::Separator();
                        ImGui::TextUnformatted("Outputs");
                        changed |= rv::ui::render_function_signature_editor(function->outputs, "Output");
                        if (changed) {
                            rv::nodes::refresh_dynamic_function_graph_nodes(state.graph);
                            state.graph.notify_graph_dirty();
                        }
                    }
                }
            } else {
                ImGui::TextDisabled("No function selected");
            }
        } else {
            ne_render_selected_node_sidebar(state.graph, *registry, state.selection.node_id);
        }
        if (NE_Node* node = state.graph.find_node(state.selection.node_id); node != nullptr) {
            bool any_inline_pin = false;
            bool changed = false;
            for (const auto& pin : node->inputs) {
                if (!ne_pin_supports_inline_editor(pin) || ne_node_has_incoming_link(state.graph, *node, pin.id))
                    continue;
                if (!any_inline_pin) {
                    ImGui::Separator();
                    ImGui::TextUnformatted("Inline Inputs");
                    any_inline_pin = true;
                }
                changed |= ne_render_inline_input_editor(*node, pin);
            }
            if (any_inline_pin)
                ImGui::TextDisabled("Used only when the pin is not connected.");
            if (changed)
                state.graph.notify_graph_dirty();
        }
    }
}

void ne_render_canvas(NodeEditorState& state, const char* id, mars::vector2<float> size) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
    bool vis = ImGui::BeginChild(id, { size.x, size.y }, ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!vis) { ImGui::EndChild(); return; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    mars::vector2<float> org = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y };
    mars::vector2<float> wsz = { ImGui::GetWindowSize().x, ImGui::GetWindowSize().y };
    ImGuiIO& io = ImGui::GetIO();
    mars::vector2<float> mouse = { io.MousePos.x, io.MousePos.y };
    ImFont* ne_font = state.font ? state.font : ImGui::GetFont();
    float bfs = state.font_size;
    const ImU32 exec_link_color = IM_COL32(245, 245, 245, 235);
    ne_prune_selection(state);

    auto pin_screen_pos = [&](const NE_Node& node, bool is_out, int pin_id) -> mars::vector2<float> {
        if (!is_out && node.has_exec_input && node.exec_input.id == pin_id)
            return ne_c2s(org, state.scroll, state.zoom, ne_exec_in_pin_pos(node));
        if (is_out) {
            for (size_t exec_index = 0; exec_index < node.exec_outputs.size(); ++exec_index) {
                if (node.exec_outputs[exec_index].id == pin_id)
                    return ne_c2s(org, state.scroll, state.zoom, ne_exec_out_pin_pos(node, static_cast<int>(exec_index)));
            }
        }

        const auto& pins = is_out ? node.outputs : node.inputs;
        for (int i = 0; i < static_cast<int>(pins.size()); ++i) {
            if (pins[i].id != pin_id)
                continue;
            return ne_c2s(org, state.scroll, state.zoom, is_out ? ne_out_pin_pos(node, i) : ne_in_pin_pos(node, i));
        }

        return {};
    };

    auto try_pin_screen_pos = [&](const NE_Node& node, bool is_out, int pin_id, mars::vector2<float>& out) -> bool {
        if (ne_find_link_pin(node, is_out, pin_id) == nullptr)
            return false;
        out = pin_screen_pos(node, is_out, pin_id);
        return std::isfinite(out.x) && std::isfinite(out.y);
    };

    auto link_color = [&](const NE_Link& lnk) -> ImU32 {
        const NE_Node* from_node = state.graph.find_node(lnk.from_node);
        if (from_node == nullptr)
            return IM_COL32(200, 200, 80, 220);
        if (rv::nodes::find_pin_by_id(from_node->exec_outputs, lnk.from_pin) != nullptr)
            return exec_link_color;
        if (const NE_Pin* from_pin = rv::nodes::find_pin_by_id(from_node->outputs, lnk.from_pin); from_pin != nullptr)
            return ne_to_imgui_color(ne_resolve_pin_color(state, *from_pin));
        if (const NE_Node* to_node = state.graph.find_node(lnk.to_node); to_node != nullptr) {
            if (const NE_Pin* to_pin = rv::nodes::find_pin_by_id(to_node->inputs, lnk.to_pin); to_pin != nullptr)
                return ne_to_imgui_color(ne_resolve_pin_color(state, *to_pin));
        }
        return IM_COL32(200, 200, 80, 220);
    };

    bool hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    bool rclick_pressed = state.mouse.right_click_signal;
    bool rclick_down = state.mouse.right_click_held;
    bool rclick_released = !rclick_down && state.mouse.right_click_was_down;
    state.mouse.right_click_was_down = rclick_down;
    state.mouse.right_click_signal = false;

    if (hov && io.MouseWheel != 0.0f) {
        float old_zoom = state.zoom;
        state.zoom = std::clamp(state.zoom * (1.0f + io.MouseWheel * 0.12f), NE_ZOOM_MIN, NE_ZOOM_MAX);
        mars::vector2<float> mouse_canvas = ne_s2c(org, state.scroll, old_zoom, mouse);
        state.scroll = {
            mouse.x - org.x - mouse_canvas.x * state.zoom,
            mouse.y - org.y - mouse_canvas.y * state.zoom
        };
    }

    if (hov && io.MouseClicked[2])
        state.mouse.panning = true;
    if (!io.MouseDown[2])
        state.mouse.panning = false;
    if (state.mouse.panning) {
        state.scroll.x += io.MouseDelta.x;
        state.scroll.y += io.MouseDelta.y;
    }

    const bool popup_open = state.spawn_menu.open;
    if ((hov || popup_open) && rclick_pressed) {
        state.mouse.context_click_armed = true;
        state.mouse.context_click_origin = mouse;
    }
    if (state.mouse.context_click_armed && rclick_released) {
        state.mouse.context_click_armed = false;
        if ((hov || popup_open) && !state.link.active && state.drag.node_id == -1) {
            state.spawn_menu.screen_pos = mouse;
            state.spawn_menu.canvas_pos = ne_s2c(org, state.scroll, state.zoom, mouse);
            state.spawn_menu.search[0] = '\0';
            state.spawn_menu.focus_search = true;
            state.spawn_menu.request_open = true;
        }
    }
    if (!rclick_down)
        state.mouse.context_click_armed = false;

    {
        dl->AddRectFilled({ org.x, org.y }, { org.x + wsz.x, org.y + wsz.y }, IM_COL32(14, 14, 18, 255));

        float grid_size = NE_GRID * state.zoom;
        float grid_size_major = grid_size * 4.0f;
        float offset_x = std::fmod(state.scroll.x, grid_size);
        float offset_y = std::fmod(state.scroll.y, grid_size);
        float offset_x_major = std::fmod(state.scroll.x, grid_size_major);
        float offset_y_major = std::fmod(state.scroll.y, grid_size_major);
        ImU32 minor_color = IM_COL32(48, 48, 52, 255);
        ImU32 major_color = IM_COL32(68, 68, 75, 255);
        mars::vector2<float> top_left = org;
        mars::vector2<float> bottom_right = { org.x + wsz.x, org.y + wsz.y };

        for (float x = offset_x; x < wsz.x; x += grid_size)
            dl->AddLine({ org.x + x, top_left.y }, { org.x + x, bottom_right.y }, minor_color);
        for (float y = offset_y; y < wsz.y; y += grid_size)
            dl->AddLine({ top_left.x, org.y + y }, { bottom_right.x, org.y + y }, minor_color);
        for (float x = offset_x_major; x < wsz.x; x += grid_size_major)
            dl->AddLine({ org.x + x, top_left.y }, { org.x + x, bottom_right.y }, major_color);
        for (float y = offset_y_major; y < wsz.y; y += grid_size_major)
            dl->AddLine({ top_left.x, org.y + y }, { bottom_right.x, org.y + y }, major_color);
    }

    int hov_node = -1;
    int hov_pin_id = -1;
    bool hov_is_out = false;
    NE_PinKind hov_pin_kind = NE_PinKind::data;
    for (auto& node : state.graph.nodes) {
        if (!ne_node_visible(state, node))
            continue;
        if (node.has_exec_input) {
            mars::vector2<float> sp = ne_c2s(org, state.scroll, state.zoom, ne_exec_in_pin_pos(node));
            if (ne_pin_hit(sp, state.zoom, mouse)) {
                hov_node = node.id;
                hov_pin_id = node.exec_input.id;
                hov_is_out = false;
                hov_pin_kind = NE_PinKind::exec;
            }
        }
        for (size_t exec_index = 0; exec_index < node.exec_outputs.size(); ++exec_index) {
            mars::vector2<float> sp = ne_c2s(org, state.scroll, state.zoom, ne_exec_out_pin_pos(node, static_cast<int>(exec_index)));
            if (ne_pin_hit(sp, state.zoom, mouse)) {
                hov_node = node.id;
                hov_pin_id = node.exec_outputs[exec_index].id;
                hov_is_out = true;
                hov_pin_kind = NE_PinKind::exec;
            }
        }
        for (int i = 0; i < static_cast<int>(node.inputs.size()); ++i) {
            mars::vector2<float> sp = ne_c2s(org, state.scroll, state.zoom, ne_in_pin_pos(node, i));
            if (ne_pin_hit(sp, state.zoom, mouse)) {
                hov_node = node.id;
                hov_pin_id = node.inputs[i].id;
                hov_is_out = false;
                hov_pin_kind = node.inputs[i].kind;
            }
        }
        for (int i = 0; i < static_cast<int>(node.outputs.size()); ++i) {
            mars::vector2<float> sp = ne_c2s(org, state.scroll, state.zoom, ne_out_pin_pos(node, i));
            if (ne_pin_hit(sp, state.zoom, mouse)) {
                hov_node = node.id;
                hov_pin_id = node.outputs[i].id;
                hov_is_out = true;
                hov_pin_kind = node.outputs[i].kind;
            }
        }
    }

    if ((hov || state.spawn_menu.open) && rclick_pressed && hov_node != -1 && hov_pin_id != -1 && hov_pin_kind == NE_PinKind::data) {
        state.pin_menu.request_open = true;
        state.pin_menu.node_id = hov_node;
        state.pin_menu.pin_id = hov_pin_id;
        state.pin_menu.is_output = hov_is_out;
        state.pin_menu.screen_pos = mouse;
        state.mouse.context_click_armed = false;
    }
    if ((hov || popup_open) && rclick_pressed && hov_node != -1 && hov_pin_id != -1 && hov_pin_kind == NE_PinKind::data)
        state.mouse.context_click_armed = false;

    if (hov && io.MouseClicked[0] && hov_node != -1) {
        state.link.active = true;
        state.link.node_id = hov_node;
        state.link.pin_id = hov_pin_id;
        state.link.is_output = hov_is_out;
        state.link.pin_kind = hov_pin_kind;
    }

    if (state.link.active && !io.MouseDown[0]) {
        if (hov_node != -1 && hov_node != state.link.node_id && hov_is_out != state.link.is_output && hov_pin_kind == state.link.pin_kind) {
            NE_Link lnk;
            if (state.link.is_output)
                lnk = { state.link.node_id, state.link.pin_id, hov_node, hov_pin_id };
            else
                lnk = { hov_node, hov_pin_id, state.link.node_id, state.link.pin_id };
            state.graph.add_link(lnk);
        } else if (hov && hov_node == -1 && (state.link.pin_kind == NE_PinKind::data || state.link.pin_kind == NE_PinKind::exec)) {
            state.spawn_menu.screen_pos = mouse;
            state.spawn_menu.canvas_pos = ne_s2c(org, state.scroll, state.zoom, mouse);
            state.spawn_menu.request_open = true;
            state.spawn_menu.focus_search = true;
            state.spawn_menu.search[0] = '\0';
            state.spawn_menu.from_link = true;
            state.spawn_menu.link_node_id = state.link.node_id;
            state.spawn_menu.link_pin_id = state.link.pin_id;
            state.spawn_menu.link_is_output = state.link.is_output;
            state.spawn_menu.link_pin_kind = state.link.pin_kind;
        }
        state.link.active = false;
    }

    if (hov && io.MouseDoubleClicked[0]) {
        for (int li = static_cast<int>(state.graph.links.size()) - 1; li >= 0; --li) {
            auto& lnk = state.graph.links[li];
            if (!ne_link_visible(state, lnk))
                continue;
            mars::vector2<float> a = {};
            mars::vector2<float> b = {};
            const NE_Node* from_node = state.graph.find_node(lnk.from_node);
            const NE_Node* to_node = state.graph.find_node(lnk.to_node);
            if (from_node == nullptr || to_node == nullptr)
                continue;
            if (!try_pin_screen_pos(*from_node, true, lnk.from_pin, a) ||
                !try_pin_screen_pos(*to_node, false, lnk.to_pin, b))
                continue;
            if (ne_bezier_hit(a, b, state.zoom, mouse)) {
                state.graph.remove_link(lnk);
                break;
            }
        }
    }

    if (hov && io.MouseClicked[0] && hov_node == -1 && state.drag.node_id == -1 && !state.link.active) {
        mars::vector2<float> cm = ne_s2c(org, state.scroll, state.zoom, mouse);
        bool hit = false;
        for (auto it = state.graph.nodes.rbegin(); it != state.graph.nodes.rend(); ++it) {
            auto& node = *it;
            if (!ne_node_visible(state, node))
                continue;
            float node_height = ne_node_h(node);
            if (cm.x >= node.pos.x && cm.x <= node.pos.x + NE_NODE_W &&
                cm.y >= node.pos.y && cm.y <= node.pos.y + node_height) {
                hit = true;
                const bool title_hit = cm.y <= node.pos.y + NE_TITLE_H;
                const bool ctrl_held = io.KeyCtrl;
                if (ctrl_held) {
                    ne_toggle_selection(state, node.id);
                } else if (!ne_selection_contains(state, node.id)) {
                    ne_set_single_selection(state, node.id);
                } else {
                    state.selection.node_id = node.id;
                }
                if (title_hit && !ctrl_held) {
                    state.drag.node_id = node.id;
                    state.drag.offset = { cm.x - node.pos.x, cm.y - node.pos.y };
                    state.drag.anchor_canvas = cm;
                    state.drag.group_origins.clear();
                    if (!ne_selection_contains(state, node.id))
                        ne_set_single_selection(state, node.id);
                    for (int selected_id : state.selection.node_ids) {
                        if (const NE_Node* selected_node = state.graph.find_node(selected_id); selected_node != nullptr)
                            state.drag.group_origins.push_back({ selected_id, selected_node->pos });
                    }
                }
                break;
            }
        }
        if (!hit) {
            state.marquee.active = true;
            state.marquee.additive = io.KeyCtrl;
            state.marquee.moved = false;
            state.marquee.origin_screen = mouse;
            state.marquee.current_screen = mouse;
        }
    }

    if (state.drag.node_id != -1) {
        if (io.MouseDown[0]) {
            mars::vector2<float> cm = ne_s2c(org, state.scroll, state.zoom, mouse);
            const mars::vector2<float> delta = { cm.x - state.drag.anchor_canvas.x, cm.y - state.drag.anchor_canvas.y };
            for (const auto& entry : state.drag.group_origins) {
                if (NE_Node* node = state.graph.find_node(entry.first); node != nullptr) {
                    const mars::vector2<float> new_pos = { entry.second.x + delta.x, entry.second.y + delta.y };
                    if (node->pos.x != new_pos.x || node->pos.y != new_pos.y) {
                        node->pos = new_pos;
                        state.graph.notify_node_moved(node->id);
                    }
                }
            }
        } else {
            state.drag.node_id = -1;
            state.drag.group_origins.clear();
        }
    }

    if (state.marquee.active) {
        if (io.MouseDown[0]) {
            state.marquee.current_screen = mouse;
            const mars::vector2<float> delta = {
                state.marquee.current_screen.x - state.marquee.origin_screen.x,
                state.marquee.current_screen.y - state.marquee.origin_screen.y
            };
            state.marquee.moved = state.marquee.moved || std::fabs(delta.x) > 2.0f || std::fabs(delta.y) > 2.0f;
        } else {
            std::vector<int> overlapped;
            if (state.marquee.moved) {
                const mars::vector2<float> min_screen = {
                    std::min(state.marquee.origin_screen.x, state.marquee.current_screen.x),
                    std::min(state.marquee.origin_screen.y, state.marquee.current_screen.y)
                };
                const mars::vector2<float> max_screen = {
                    std::max(state.marquee.origin_screen.x, state.marquee.current_screen.x),
                    std::max(state.marquee.origin_screen.y, state.marquee.current_screen.y)
                };
                const mars::vector2<float> min_canvas = ne_s2c(org, state.scroll, state.zoom, min_screen);
                const mars::vector2<float> max_canvas = ne_s2c(org, state.scroll, state.zoom, max_screen);
                for (const auto& node : state.graph.nodes) {
                    if (!ne_node_visible(state, node))
                        continue;
                    const mars::vector2<float> node_min = node.pos;
                    const mars::vector2<float> node_max = { node.pos.x + NE_NODE_W, node.pos.y + ne_node_h(node) };
                    if (ne_rects_overlap(min_canvas, max_canvas, node_min, node_max))
                        overlapped.push_back(node.id);
                }
            }

            if (state.marquee.moved) {
                if (state.marquee.additive) {
                    for (int node_id : overlapped)
                        ne_toggle_selection(state, node_id);
                } else {
                    ne_clear_selection(state);
                    for (int node_id : overlapped)
                        ne_add_selection(state, node_id);
                    ne_sync_primary_selection(state);
                }
            } else if (!state.marquee.additive) {
                ne_clear_selection(state);
            }

            state.marquee.active = false;
        }
    }

    if (!state.selection.node_ids.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        const std::vector<int> selected = state.selection.node_ids;
        ne_clear_selection(state);
        state.drag.node_id = -1;
        state.drag.group_origins.clear();
        for (int id_to_remove : selected) {
            if (state.graph.is_node_permanent(id_to_remove))
                continue;
            if (state.link.node_id == id_to_remove)
                state.link.active = false;
            state.graph.remove_node(id_to_remove);
        }
    }

    for (auto& lnk : state.graph.links) {
        if (!ne_link_visible(state, lnk))
            continue;
        mars::vector2<float> a = {};
        mars::vector2<float> b = {};
        const NE_Node* from_node = state.graph.find_node(lnk.from_node);
        const NE_Node* to_node = state.graph.find_node(lnk.to_node);
        if (from_node == nullptr || to_node == nullptr)
            continue;
        if (!try_pin_screen_pos(*from_node, true, lnk.from_pin, a) ||
            !try_pin_screen_pos(*to_node, false, lnk.to_pin, b))
            continue;
        ne_bezier(dl, a, b, state.zoom, link_color(lnk));
    }

    if (state.link.active) {
        mars::vector2<float> a = {};
        mars::vector2<float> b = mouse;
        if (const NE_Node* source_node = state.graph.find_node(state.link.node_id); source_node != nullptr)
            a = pin_screen_pos(*source_node, state.link.is_output, state.link.pin_id);
        if (!state.link.is_output)
            std::swap(a, b);
        const ImU32 preview_color = state.link.pin_kind == NE_PinKind::exec ? IM_COL32(245, 245, 245, 180) : IM_COL32(200, 200, 80, 140);
        ne_bezier(dl, a, b, state.zoom, preview_color, 2.0f);
    }

    for (auto& node : state.graph.nodes) {
        if (!ne_node_visible(state, node))
            continue;
        const std::string error_message = ne_resolve_node_error_message(state, node);
        const bool has_error = !error_message.empty();
        float node_height = ne_node_h(node);
        mars::vector2<float> sp = ne_c2s(org, state.scroll, state.zoom, node.pos);
        float sw = NE_NODE_W * state.zoom;
        float sh = node_height * state.zoom;
        float seh = has_error ? 24.0f * state.zoom : 0.0f;
        float sth = NE_TITLE_H * state.zoom;
        float spr = NE_PIN_R * state.zoom;
        float rnd = 6.0f * state.zoom;
        float fz = bfs * state.zoom;

        float pin_text_top_offset;
        {
            ImFontBaked* bk = ne_font->GetFontBaked(fz);
            const ImFontGlyph* gx = bk ? bk->FindGlyph('x') : nullptr;
            if (gx)
                pin_text_top_offset = (gx->Y0 + gx->Y1) * 0.5f;
            else
                pin_text_top_offset = bk ? bk->Ascent * 0.6f : fz * 0.5f;
        }

        const bool drag = node.id == state.drag.node_id;
        const bool is_selected = ne_selection_contains(state, node.id);
        const bool is_primary_selected = node.id == state.selection.node_id;

        if (!drag) {
            dl->AddRectFilled(
                { sp.x + 4, sp.y + 5 },
                { sp.x + sw + 4, sp.y + sh + 5 },
                IM_COL32(0, 0, 0, 70),
                rnd
            );
        }

        dl->AddRectFilled({ sp.x, sp.y }, { sp.x + sw, sp.y + sh }, IM_COL32(36, 36, 40, 245), rnd);
        if (has_error) {
            dl->AddRectFilled({ sp.x, sp.y + sh - rnd }, { sp.x + sw, sp.y + sh }, IM_COL32(36, 36, 40, 245), 0.0f);
            dl->AddRectFilled({ sp.x, sp.y + sh }, { sp.x + sw, sp.y + sh + seh }, IM_COL32(130, 42, 42, 245), rnd);
            dl->AddRectFilled({ sp.x, sp.y + sh }, { sp.x + sw, sp.y + sh + rnd }, IM_COL32(130, 42, 42, 245), 0.0f);
        }

        dl->AddRectFilled({ sp.x, sp.y }, { sp.x + sw, sp.y + sth }, IM_COL32(55, 90, 145, 255), rnd);
        dl->AddRectFilled({ sp.x, sp.y + sth - rnd }, { sp.x + sw, sp.y + sth }, IM_COL32(55, 90, 145, 255), 0.0f);

        ImU32 border = drag ? IM_COL32(220, 220, 60, 255)
            : is_primary_selected ? IM_COL32(255, 255, 255, 220)
            : is_selected ? IM_COL32(170, 205, 255, 220)
            : IM_COL32(85, 85, 92, 200);
        dl->AddRect({ sp.x, sp.y }, { sp.x + sw, sp.y + sh + seh }, border, rnd, 0, 1.5f * state.zoom);

        dl->AddLine({ sp.x, sp.y + sth }, { sp.x + sw, sp.y + sth }, IM_COL32(75, 75, 82, 200), 1.0f);
        if (has_error)
            dl->AddLine({ sp.x, sp.y + sh }, { sp.x + sw, sp.y + sh }, IM_COL32(190, 95, 95, 220), 1.0f);

        {
            auto imgui_tsz = ne_font->CalcTextSizeA(fz, FLT_MAX, 0.0f, node.title.c_str());
            mars::vector2<float> tsz = { imgui_tsz.x, imgui_tsz.y };
            float tx = std::round(sp.x + (sw - tsz.x) * 0.5f);
            float ty = std::round(sp.y + (sth - tsz.y) * 0.5f);
            dl->AddText(ne_font, fz, { tx + 1.0f, ty + 1.0f }, IM_COL32(0, 0, 0, 160), node.title.c_str());
            dl->AddText(ne_font, fz, { tx, ty }, IM_COL32(255, 255, 255, 255), node.title.c_str());
        }

        auto draw_pin_shape = [&](mars::vector2<float> pp, ImU32 pc, bool container, bool texture) {
            if (texture) {
                const mars::vector2<float> top = { pp.x, pp.y - spr };
                const mars::vector2<float> left = { pp.x - spr, pp.y + spr * 0.85f };
                const mars::vector2<float> right = { pp.x + spr, pp.y + spr * 0.85f };
                const mars::vector2<float> top_i = { pp.x, pp.y - spr * 0.45f };
                const mars::vector2<float> left_i = { pp.x - spr * 0.45f, pp.y + spr * 0.35f };
                const mars::vector2<float> right_i = { pp.x + spr * 0.45f, pp.y + spr * 0.35f };
                dl->AddTriangleFilled({ top_i.x, top_i.y }, { left_i.x, left_i.y }, { right_i.x, right_i.y }, IM_COL32(22, 22, 26, 255));
                dl->AddTriangle({ top.x, top.y }, { left.x, left.y }, { right.x, right.y }, pc, 1.5f * state.zoom);
            } else if (container) {
                float inner_radius = spr * 0.55f;
                float outer_radius = spr;
                dl->AddRectFilled({ pp.x - inner_radius, pp.y - inner_radius }, { pp.x + inner_radius, pp.y + inner_radius }, IM_COL32(22, 22, 26, 255));
                dl->AddRect({ pp.x - outer_radius, pp.y - outer_radius }, { pp.x + outer_radius, pp.y + outer_radius }, pc, 0.0f, 0, 1.5f * state.zoom);
            } else {
                dl->AddCircleFilled({ pp.x, pp.y }, spr * 0.55f, IM_COL32(22, 22, 26, 255));
                dl->AddCircle({ pp.x, pp.y }, spr, pc, 0, 1.5f * state.zoom);
            }
        };

        auto draw_exec_pin = [&](mars::vector2<float> pp, ImU32 pc, bool is_hovered) {
            const float outer_radius = spr * 0.95f;
            const float inner_radius = spr * 0.48f;
            const mars::vector2<float> top = { pp.x, pp.y - outer_radius };
            const mars::vector2<float> right = { pp.x + outer_radius, pp.y };
            const mars::vector2<float> bottom = { pp.x, pp.y + outer_radius };
            const mars::vector2<float> left = { pp.x - outer_radius, pp.y };
            const mars::vector2<float> top_i = { pp.x, pp.y - inner_radius };
            const mars::vector2<float> right_i = { pp.x + inner_radius, pp.y };
            const mars::vector2<float> bottom_i = { pp.x, pp.y + inner_radius };
            const mars::vector2<float> left_i = { pp.x - inner_radius, pp.y };

            dl->AddQuadFilled({ top_i.x, top_i.y }, { right_i.x, right_i.y }, { bottom_i.x, bottom_i.y }, { left_i.x, left_i.y }, IM_COL32(22, 22, 26, 255));
            dl->AddQuad({ top.x, top.y }, { right.x, right.y }, { bottom.x, bottom.y }, { left.x, left.y }, pc, 1.6f * state.zoom);
            if (is_hovered) {
                dl->AddQuad({ top.x, top.y }, { right.x, right.y }, { bottom.x, bottom.y }, { left.x, left.y }, IM_COL32(255, 240, 180, 120), 2.4f * state.zoom);
            }
        };

        if (node.has_exec_input) {
            mars::vector2<float> pp = ne_c2s(org, state.scroll, state.zoom, ne_exec_in_pin_pos(node));
            const bool phv = hov_node == node.id && hov_pin_id == node.exec_input.id && !hov_is_out;
            const ImU32 pc = phv ? IM_COL32(255, 230, 120, 255) : exec_link_color;
            draw_exec_pin(pp, pc, phv);
        }

        for (size_t exec_index = 0; exec_index < node.exec_outputs.size(); ++exec_index) {
            const auto& exec_pin = node.exec_outputs[exec_index];
            mars::vector2<float> pp = ne_c2s(org, state.scroll, state.zoom, ne_exec_out_pin_pos(node, static_cast<int>(exec_index)));
            const bool phv = hov_node == node.id && hov_pin_id == exec_pin.id && hov_is_out;
            const ImU32 pc = phv ? IM_COL32(255, 230, 120, 255) : exec_link_color;
            draw_exec_pin(pp, pc, phv);

            auto imgui_tsz = ne_font->CalcTextSizeA(fz, FLT_MAX, 0.0f, exec_pin.label.c_str());
            mars::vector2<float> tsz = { imgui_tsz.x, imgui_tsz.y };
            dl->AddText(ne_font, fz,
                { std::round(pp.x - spr - 5.0f * state.zoom - tsz.x), std::round(pp.y - pin_text_top_offset) },
                IM_COL32(220, 220, 225, 255), exec_pin.label.c_str());
        }

        for (int i = 0; i < static_cast<int>(node.inputs.size()); ++i) {
            const auto& pin = node.inputs[i];
            mars::vector2<float> pp = ne_c2s(org, state.scroll, state.zoom, ne_in_pin_pos(node, i));
            bool phv = hov_node == node.id && hov_pin_id == pin.id && !hov_is_out;
            ImU32 pc = phv ? IM_COL32(255, 255, 80, 255) : ne_to_imgui_color(ne_resolve_pin_color(state, pin));

            draw_pin_shape(pp, pc, pin.is_container, ne_pin_type_is_texture(pin.type_hash));

            const std::string pin_text = rv::nodes::pin_display_label(pin);
            dl->AddText(ne_font, fz,
                { std::round(pp.x + spr + 5.0f * state.zoom), std::round(pp.y - pin_text_top_offset) },
                IM_COL32(210, 210, 215, 255), pin_text.c_str());
        }

        for (int i = 0; i < static_cast<int>(node.outputs.size()); ++i) {
            const auto& pin = node.outputs[i];
            mars::vector2<float> pp = ne_c2s(org, state.scroll, state.zoom, ne_out_pin_pos(node, i));
            bool phv = hov_node == node.id && hov_pin_id == pin.id && hov_is_out;
            ImU32 pc = phv ? IM_COL32(255, 255, 80, 255) : ne_to_imgui_color(ne_resolve_pin_color(state, pin));

            draw_pin_shape(pp, pc, pin.is_container, ne_pin_type_is_texture(pin.type_hash));

            const std::string pin_text = rv::nodes::pin_display_label(pin);
            auto imgui_tsz = ne_font->CalcTextSizeA(fz, FLT_MAX, 0.0f, pin_text.c_str());
            mars::vector2<float> tsz = { imgui_tsz.x, imgui_tsz.y };
            dl->AddText(ne_font, fz,
                { std::round(pp.x - spr - 5.0f * state.zoom - tsz.x), std::round(pp.y - pin_text_top_offset) },
                IM_COL32(210, 210, 215, 255), pin_text.c_str());
        }

        if (has_error) {
            const float text_x = std::round(sp.x + 8.0f * state.zoom);
            const float text_y = std::round(sp.y + sh + (seh - fz) * 0.5f - 1.0f * state.zoom);
            const std::string error_text = "Error: " + error_message;
            dl->PushClipRect({ sp.x + 4.0f * state.zoom, sp.y + sh }, { sp.x + sw - 4.0f * state.zoom, sp.y + sh + seh }, true);
            dl->AddText(ne_font, fz, { text_x, text_y }, IM_COL32(255, 232, 232, 255), error_text.c_str());
            dl->PopClipRect();
        }
    }

    if (state.marquee.active && state.marquee.moved) {
        const ImVec2 min_rect(
            std::min(state.marquee.origin_screen.x, state.marquee.current_screen.x),
            std::min(state.marquee.origin_screen.y, state.marquee.current_screen.y)
        );
        const ImVec2 max_rect(
            std::max(state.marquee.origin_screen.x, state.marquee.current_screen.x),
            std::max(state.marquee.origin_screen.y, state.marquee.current_screen.y)
        );
        dl->AddRectFilled(min_rect, max_rect, IM_COL32(120, 170, 255, 32));
        dl->AddRect(min_rect, max_rect, IM_COL32(160, 210, 255, 200), 0.0f, 0, 1.5f);
    }

    if (hov_node != -1 && hov_pin_kind == NE_PinKind::data && !io.MouseDown[0]) {
        const NE_Node* hovered_node = state.graph.find_node(hov_node);
        const NE_Pin* hovered_pin = nullptr;
        if (hovered_node != nullptr) {
            hovered_pin = hov_is_out
                ? rv::nodes::find_pin_by_id(hovered_node->outputs, hov_pin_id)
                : rv::nodes::find_pin_by_id(hovered_node->inputs, hov_pin_id);
        }
        if (hovered_pin != nullptr) {
            const std::string type_name = ne_pin_type_name(state, *hovered_pin);
            ImGui::BeginTooltip();
            ImGui::Text("Type: %s", type_name.c_str());
            ImGui::EndTooltip();
        }
    }

    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImRect canvas_drop_rect(ImVec2(org.x, org.y), ImVec2(org.x + wsz.x, org.y + wsz.y));
        if (ImGui::BeginDragDropTargetCustom(canvas_drop_rect, window->GetID("##canvas_variable_drop_target"))) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("NE_VARIABLE_SLOT")) {
                if (payload->DataSize == sizeof(int) && payload->IsDelivery()) {
                    state.variable_drop.pending_id = *static_cast<const int*>(payload->Data);
                    state.variable_drop.screen_pos = mouse;
                    state.variable_drop.canvas_pos = ne_s2c(org, state.scroll, state.zoom, mouse);
                    state.variable_drop.request_open = true;
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    if (state.spawn_menu.request_open) {
        ImGui::SetNextWindowPos({ state.spawn_menu.screen_pos.x, state.spawn_menu.screen_pos.y }, ImGuiCond_Always);
        ImGui::OpenPopup("NE_SpawnMenu");
        state.spawn_menu.request_open = false;
    }
    if (state.variable_drop.request_open) {
        ImGui::SetNextWindowPos({ state.variable_drop.screen_pos.x, state.variable_drop.screen_pos.y }, ImGuiCond_Always);
        ImGui::OpenPopup("NE_VariableDropMenu");
        state.variable_drop.request_open = false;
    }
    if (state.pin_menu.request_open) {
        ImGui::SetNextWindowPos({ state.pin_menu.screen_pos.x, state.pin_menu.screen_pos.y }, ImGuiCond_Always);
        ImGui::OpenPopup("NE_PinMenu");
        state.pin_menu.request_open = false;
    }

    if (ImGui::BeginPopup("NE_VariableDropMenu")) {
        const auto* slot = state.graph.find_variable_slot(state.variable_drop.pending_id);
        const std::string variable_name = slot != nullptr ? slot->name : std::string("Variable");
        const std::string get_title = "Get " + variable_name;
        const std::string set_title = "Set " + variable_name;
        if (ImGui::Selectable(get_title.c_str())) {
            rv::nodes::spawn_bound_variable_node<rv::nodes::variable_get_node_tag>(state.graph, state.variable_drop.pending_id, state.variable_drop.canvas_pos);
            state.variable_drop.pending_id = -1;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Selectable(set_title.c_str())) {
            rv::nodes::spawn_bound_variable_node<rv::nodes::variable_set_node_tag>(state.graph, state.variable_drop.pending_id, state.variable_drop.canvas_pos);
            state.variable_drop.pending_id = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("NE_PinMenu")) {
        const NE_Node* menu_node = state.graph.find_node(state.pin_menu.node_id);
        const NE_Pin* menu_pin = nullptr;
        if (menu_node != nullptr) {
            menu_pin = state.pin_menu.is_output
                ? rv::nodes::find_pin_by_id(menu_node->outputs, state.pin_menu.pin_id)
                : rv::nodes::find_pin_by_id(menu_node->inputs, state.pin_menu.pin_id);
        }
        if (menu_pin != nullptr && ne_can_promote_pin_to_variable(*menu_pin)) {
            if (ImGui::MenuItem("Promote to Variable")) {
                ne_promote_pin_to_variable(state, state.pin_menu.node_id, state.pin_menu.pin_id, state.pin_menu.is_output);
                ImGui::CloseCurrentPopup();
            }
        } else {
            ImGui::TextDisabled("Pin cannot be promoted");
        }
        ImGui::EndPopup();
    }

    state.spawn_menu.open = ImGui::IsPopupOpen("NE_SpawnMenu");
    if (ImGui::BeginPopup("NE_SpawnMenu")) {
        if (state.spawn_menu.focus_search) {
            ImGui::SetKeyboardFocusHere();
            state.spawn_menu.focus_search = false;
        }

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##ne_spawn_search", "Search nodes...", state.spawn_menu.search.data(), state.spawn_menu.search.size());
        ImGui::Separator();

        bool any_match = false;
        const NodeRegistry* registry = state.graph.node_registry();
        const NE_Pin* drag_pin = ne_find_spawn_menu_drag_pin(state);
        if (registry != nullptr) {
            for (size_t type : registry->registered_types()) {
                const NodeTypeInfo* info = registry->find(type);
                if (info == nullptr || !ne_contains_case_insensitive(info->meta.title, state.spawn_menu.search.data()))
                    continue;

                const bool show_in_menu = info->meta.show_in_spawn_menu || ne_should_show_hidden_spawn_node(state, *info);
                if (!show_in_menu)
                    continue;
                if (drag_pin != nullptr && ne_find_first_compatible_type_pin(*info, *drag_pin, state.spawn_menu.link_is_output) == nullptr)
                    continue;

                any_match = true;
                if (ImGui::Selectable(info->meta.title.c_str())) {
                    NE_Node* spawned_node = state.graph.spawn_node(info->meta.title, state.spawn_menu.canvas_pos);
                    if (spawned_node != nullptr && drag_pin != nullptr) {
                        if (const NE_Pin* compatible_pin = ne_find_first_compatible_node_pin(*spawned_node, *drag_pin, state.spawn_menu.link_is_output); compatible_pin != nullptr) {
                            const NE_Link link = state.spawn_menu.link_is_output
                                ? NE_Link{ state.spawn_menu.link_node_id, state.spawn_menu.link_pin_id, spawned_node->id, compatible_pin->id }
                                : NE_Link{ spawned_node->id, compatible_pin->id, state.spawn_menu.link_node_id, state.spawn_menu.link_pin_id };
                            state.graph.add_link(link);
                        }
                    }
                    ne_clear_spawn_menu_link_context(state);
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        if (!any_match) {
            if (drag_pin != nullptr)
                ImGui::TextDisabled("No compatible nodes found");
            else
                ImGui::TextDisabled("No nodes found");
        }

        ImGui::EndPopup();
    } else if (!state.spawn_menu.open) {
        ne_clear_spawn_menu_link_context(state);
    }

    ImGui::EndChild();
}

rv::ui::editor_actions ne_render(NodeEditorState& state, const rv::ui::runtime_panel_context& runtime_panel, const char* id, mars::vector2<float> size) {
    rv::ui::editor_actions actions;
    state.request_save = false;
    state.request_load = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
    bool vis = ImGui::BeginChild(id, { size.x, size.y }, ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!vis) {
        ImGui::EndChild();
        return actions;
    }

    constexpr float splitter_thickness = 6.0f;
    constexpr float min_sidebar_width = 280.0f;
    constexpr float min_canvas_width = 320.0f;
    constexpr float min_inspector_height = 140.0f;

    const ImVec2 content_avail = ImGui::GetContentRegionAvail();
    const float max_total_sidebars = std::max(2.0f * min_sidebar_width, content_avail.x - min_canvas_width - splitter_thickness * 2.0f);
    state.left_sidebar_width = std::clamp(state.left_sidebar_width, min_sidebar_width, std::max(min_sidebar_width, max_total_sidebars - state.right_sidebar_width));
    state.right_sidebar_width = std::clamp(state.right_sidebar_width, min_sidebar_width, std::max(min_sidebar_width, max_total_sidebars - state.left_sidebar_width));
    const float max_left_sidebar_width = std::max(min_sidebar_width, content_avail.x - state.right_sidebar_width - splitter_thickness * 2.0f - min_canvas_width);
    const float max_right_sidebar_width = std::max(min_sidebar_width, content_avail.x - state.left_sidebar_width - splitter_thickness * 2.0f - min_canvas_width);
    state.left_sidebar_width = std::clamp(state.left_sidebar_width, min_sidebar_width, max_left_sidebar_width);
    state.right_sidebar_width = std::clamp(state.right_sidebar_width, min_sidebar_width, max_right_sidebar_width);
    const float canvas_width = std::max(min_canvas_width, content_avail.x - state.left_sidebar_width - state.right_sidebar_width - splitter_thickness * 2.0f);

    ImGui::BeginChild("##ne_sidebar_left", { state.left_sidebar_width, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    bool start_requested = false;
    ne_render_types_panel(state);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    ne_draw_splitter_x("##ne_splitter_left_sidebar", splitter_thickness, &state.left_sidebar_width, min_sidebar_width, max_left_sidebar_width);
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::BeginChild("##ne_canvas_host", { canvas_width, 0.0f }, 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ne_render_canvas(state, "##ne_canvas", { ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y });
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    ne_draw_splitter_x("##ne_splitter_right_sidebar", splitter_thickness, &state.right_sidebar_width, min_sidebar_width, max_right_sidebar_width, -1.0f);
    ImGui::SameLine(0.0f, 0.0f);

    ImGui::BeginChild("##ne_sidebar_right", { 0.0f, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);

    const float right_sidebar_avail_y = ImGui::GetContentRegionAvail().y;
    state.right_panel_top_height = std::clamp(state.right_panel_top_height, min_inspector_height, std::max(min_inspector_height, right_sidebar_avail_y - min_inspector_height - splitter_thickness));

    ImGui::BeginChild("##ne_sidebar_runtime", { 0.0f, state.right_panel_top_height }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted("Runtime");
    ImGui::Separator();
    actions.overview_toggle_requested = rv::ui::render_runtime_panel(runtime_panel);
    ImGui::EndChild();

    ne_draw_splitter_y("##ne_splitter_right_panel", splitter_thickness, &state.right_panel_top_height, min_inspector_height, std::max(min_inspector_height, right_sidebar_avail_y - min_inspector_height - splitter_thickness));

    ImGui::BeginChild("##ne_sidebar_types", { 0.0f, 0.0f }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ne_render_selection_editor_panel(state, runtime_panel, start_requested);
    ImGui::EndChild();

    ImGui::EndChild();

    ImGui::EndChild();
    actions.save_requested = state.request_save;
    actions.load_requested = state.request_load;
    actions.runtime_toggle_requested = start_requested;
    return actions;
}

namespace rv::ui {

void clear_selection(editor_state& _state) {
    ne_clear_selection(_state);
}

void render_canvas(editor_state& _state, const char* _id, mars::vector2<float> _size) {
    ne_render_canvas(_state, _id, _size);
}

editor_actions render_editor(editor_state& _state, const runtime_panel_context& _runtime_panel, const char* _id, mars::vector2<float> _size) {
    return ne_render(_state, _runtime_panel, _id, _size);
}

} // namespace rv::ui
