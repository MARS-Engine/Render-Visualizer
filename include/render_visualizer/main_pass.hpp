#pragma once

#include <mars/graphics/backend/render_pass.hpp>
#include <mars/graphics/backend/texture.hpp>
#include <mars/graphics/object/schema.hpp>
#include <mars/meta.hpp>

#include <cstdint>

namespace rv {

struct[[= mars::prop::rp_uses_swapchain()]]
	  [[= mars::prop::rp_clear_color(0.03f, 0.03f, 0.05f, 1.0f)]]
	  [[= mars::prop::rp_clear_depth(1.0f)]]
	  [[= mars::prop::rp_present(true)]]
	main_pass_desc {};

struct present_push_constants {
	std::uint32_t source_texture_index = 0;
};

} // namespace rv
