#include <render_visualizer/ui/widgets.hpp>

#include <filesystem>
#include <mutex>

namespace rv::ui {

namespace {

struct file_dialog_state {
	std::mutex mutex;
	bool pending = false;
	std::optional<std::string> selection;
	std::string default_location;
	SDL_Window* parent = nullptr;
};

file_dialog_state& dialog_state() {
	static file_dialog_state state;
	return state;
}

int resize_std_string_input(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		auto* str = static_cast<std::string*>(data->UserData);
		str->resize(static_cast<size_t>(data->BufTextLen));
		data->Buf = str->data();
	}
	return 0;
}

void SDLCALL on_open_file_dialog(void*, const char* const* filelist, int) {
	std::lock_guard lock(dialog_state().mutex);
	dialog_state().pending = false;
	dialog_state().selection.reset();

	if (filelist != nullptr && filelist[0] != nullptr)
		dialog_state().selection = filelist[0];
}

} // namespace

void set_file_dialog_parent(SDL_Window* _window) {
	std::lock_guard lock(dialog_state().mutex);
	dialog_state().parent = _window;
}

bool input_text_multiline_string(const char* _label, std::string& _value, const ImVec2& _size) {
	return ImGui::InputTextMultiline(
		_label,
		_value.data(),
		_value.size() + 1u,
		_size,
		ImGuiInputTextFlags_CallbackResize,
		resize_std_string_input,
		&_value
	);
}

bool input_text_string(const char* _label, std::string& _value) {
	return ImGui::InputText(
		_label,
		_value.data(),
		_value.size() + 1u,
		ImGuiInputTextFlags_CallbackResize,
		resize_std_string_input,
		&_value
	);
}

void open_file_picker(std::string_view _current_path) {
	{
		std::lock_guard lock(dialog_state().mutex);
		if (dialog_state().pending)
			return;

		dialog_state().pending = true;
		dialog_state().selection.reset();
		dialog_state().default_location.clear();
		if (!_current_path.empty()) {
			std::filesystem::path dialog_path(_current_path);
			if (!std::filesystem::is_directory(dialog_path))
				dialog_path = dialog_path.parent_path();
			if (!dialog_path.empty())
				dialog_state().default_location = dialog_path.string();
		}
	}

	static constexpr SDL_DialogFileFilter filters[] = {
		{ "Image files", "png;jpg;jpeg;tga;bmp;hdr" },
		{ "glTF files", "gltf;glb" },
		{ "All files", "*" },
	};

	const char* default_location = nullptr;
	std::string default_location_copy;
	SDL_Window* parent = nullptr;
	{
		std::lock_guard lock(dialog_state().mutex);
		if (!dialog_state().default_location.empty()) {
			default_location_copy = dialog_state().default_location;
			default_location = default_location_copy.c_str();
		}
		parent = dialog_state().parent;
	}

#if defined(__linux__)
	SDL_SetHint(SDL_HINT_FILE_DIALOG_DRIVER, "zenity");
#else
	SDL_ResetHint(SDL_HINT_FILE_DIALOG_DRIVER);
#endif

	SDL_ShowOpenFileDialog(
		on_open_file_dialog,
		nullptr,
		parent,
		filters,
		static_cast<int>(std::size(filters)),
		default_location,
		false
	);
}

std::optional<std::string> consume_selected_file_path() {
	std::lock_guard lock(dialog_state().mutex);
	if (dialog_state().pending || !dialog_state().selection.has_value())
		return std::nullopt;

	std::optional<std::string> result = std::move(dialog_state().selection);
	dialog_state().selection.reset();
	return result;
}

bool is_file_picker_open() {
	std::lock_guard lock(dialog_state().mutex);
	return dialog_state().pending;
}

} // namespace rv::ui
