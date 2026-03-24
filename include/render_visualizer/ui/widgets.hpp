#pragma once

#include <imgui.h>
#include <SDL3/SDL.h>

#include <mars/math/vector2.hpp>
#include <mars/math/vector3.hpp>
#include <mars/math/vector4.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace rv::ui {

void set_file_dialog_parent(SDL_Window* _window);
bool input_text_multiline_string(const char* _label, std::string& _value, const ImVec2& _size);
bool input_text_string(const char* _label, std::string& _value);
void open_file_picker(std::string_view _current_path);
std::optional<std::string> consume_selected_file_path();
bool is_file_picker_open();

template <typename T>
inline bool render_typed_value_editor(const char* _label, T& _value) {
	if constexpr (std::is_same_v<T, float>) {
		return ImGui::InputFloat(_label, &_value);
	} else if constexpr (std::is_same_v<T, mars::vector2<float>>) {
		return ImGui::InputFloat2(_label, &_value.x);
	} else if constexpr (std::is_same_v<T, mars::vector3<float>>) {
		return ImGui::InputFloat3(_label, &_value.x);
	} else if constexpr (std::is_same_v<T, mars::vector4<float>>) {
		return ImGui::InputFloat4(_label, &_value.x);
	} else if constexpr (std::is_same_v<T, unsigned int>) {
		return ImGui::InputScalar(_label, ImGuiDataType_U32, &_value);
	} else if constexpr (std::is_same_v<T, mars::vector2<unsigned int>>) {
		return ImGui::InputScalarN(_label, ImGuiDataType_U32, &_value.x, 2);
	} else if constexpr (std::is_same_v<T, mars::vector3<unsigned int>>) {
		return ImGui::InputScalarN(_label, ImGuiDataType_U32, &_value.x, 3);
	} else if constexpr (std::is_same_v<T, mars::vector4<unsigned int>>) {
		return ImGui::InputScalarN(_label, ImGuiDataType_U32, &_value.x, 4);
	} else if constexpr (std::is_same_v<T, std::string>) {
		return input_text_string(_label, _value);
	} else if constexpr (std::is_same_v<T, bool>) {
		return ImGui::Checkbox(_label, &_value);
	} else {
		return false;
	}
}

} // namespace rv::ui

namespace rv::nodes::ui {

using rv::ui::consume_selected_file_path;
using rv::ui::input_text_multiline_string;
using rv::ui::input_text_string;
using rv::ui::is_file_picker_open;
using rv::ui::open_file_picker;
using rv::ui::render_typed_value_editor;
using rv::ui::set_file_dialog_parent;

} // namespace rv::nodes::ui
