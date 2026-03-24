#include <render_visualizer/raster_node.hpp>

#include <mars/math/matrix4.hpp>
#include <mars/math/vector2.hpp>
#include <mars/math/vector3.hpp>
#include <mars/math/vector4.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <objidl.h>
#include <d3d12shader.h>
#include <wrl/client.h>
using dxc_module_t = HMODULE;
#else
#include <dlfcn.h>
using dxc_module_t = void*;
#include <WinAdapter.h>
#endif

#include <dxcapi.h>

#ifdef _WIN32
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;
#endif

#ifdef _WIN32
using dxc_create_instance_proc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
#else
using dxc_create_instance_proc = HRESULT(*)(REFCLSID, REFIID, LPVOID*);
#endif

#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifndef _WIN32
enum D3D_REGISTER_COMPONENT_TYPE {
	D3D_REGISTER_COMPONENT_UNKNOWN = 0,
	D3D_REGISTER_COMPONENT_UINT32 = 1,
	D3D_REGISTER_COMPONENT_SINT32 = 2,
	D3D_REGISTER_COMPONENT_FLOAT32 = 3,
};

enum D3D_NAME {
	D3D_NAME_UNDEFINED = 0,
};

enum D3D_PRIMITIVE_TOPOLOGY {
	D3D_PRIMITIVE_TOPOLOGY_UNDEFINED = 0,
};

enum D3D_PRIMITIVE {
	D3D_PRIMITIVE_UNDEFINED = 0,
};

enum D3D_TESSELLATOR_OUTPUT_PRIMITIVE {
	D3D_TESSELLATOR_OUTPUT_UNDEFINED = 0,
};

enum D3D_TESSELLATOR_PARTITIONING {
	D3D_TESSELLATOR_PARTITIONING_UNDEFINED = 0,
};

enum D3D_TESSELLATOR_DOMAIN {
	D3D_TESSELLATOR_DOMAIN_UNDEFINED = 0,
};

enum D3D_SHADER_INPUT_TYPE {
	D3D_SIT_CBUFFER = 0,
	D3D_SIT_TBUFFER = 1,
	D3D_SIT_TEXTURE = 2,
	D3D_SIT_SAMPLER = 3,
	D3D_SIT_UAV_RWTYPED = 4,
	D3D_SIT_STRUCTURED = 5,
	D3D_SIT_UAV_RWSTRUCTURED = 6,
	D3D_SIT_BYTEADDRESS = 7,
	D3D_SIT_UAV_RWBYTEADDRESS = 8,
	D3D_SIT_UAV_APPEND_STRUCTURED = 9,
	D3D_SIT_UAV_CONSUME_STRUCTURED = 10,
	D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER = 11,
};

enum D3D_SRV_DIMENSION {
	D3D_SRV_DIMENSION_UNKNOWN = 0,
	D3D_SRV_DIMENSION_BUFFER = 1,
	D3D_SRV_DIMENSION_TEXTURE1D = 2,
	D3D_SRV_DIMENSION_TEXTURE1DARRAY = 3,
	D3D_SRV_DIMENSION_TEXTURE2D = 4,
	D3D_SRV_DIMENSION_TEXTURE2DARRAY = 5,
	D3D_SRV_DIMENSION_TEXTURE2DMS = 6,
	D3D_SRV_DIMENSION_TEXTURE2DMSARRAY = 7,
	D3D_SRV_DIMENSION_TEXTURE3D = 8,
	D3D_SRV_DIMENSION_TEXTURECUBE = 9,
	D3D_SRV_DIMENSION_TEXTURECUBEARRAY = 10,
	D3D_SRV_DIMENSION_BUFFEREX = 11,
};

enum D3D_CBUFFER_TYPE {
	D3D_CT_CBUFFER = 0,
};

enum D3D_SHADER_VARIABLE_CLASS {
	D3D_SVC_SCALAR = 0,
	D3D_SVC_VECTOR = 1,
};

enum D3D_SHADER_VARIABLE_TYPE {
	D3D_SVT_VOID = 0,
	D3D_SVT_BOOL = 1,
	D3D_SVT_INT = 2,
	D3D_SVT_FLOAT = 3,
	D3D_SVT_UINT = 19,
};

enum D3D_MIN_PRECISION {
	D3D_MIN_PRECISION_DEFAULT = 0,
};

struct D3D12_SIGNATURE_PARAMETER_DESC {
	LPCSTR SemanticName;
	UINT SemanticIndex;
	UINT Register;
	D3D_NAME SystemValueType;
	D3D_REGISTER_COMPONENT_TYPE ComponentType;
	BYTE Mask;
	BYTE ReadWriteMask;
	UINT Stream;
	D3D_MIN_PRECISION MinPrecision;
};

struct D3D12_SHADER_INPUT_BIND_DESC {
	LPCSTR Name;
	D3D_SHADER_INPUT_TYPE Type;
	UINT BindPoint;
	UINT BindCount;
	UINT uFlags;
	UINT ReturnType;
	UINT Dimension;
	UINT NumSamples;
	UINT Space;
	UINT uID;
};

struct D3D12_SHADER_DESC {
	UINT Version;
	LPCSTR Creator;
	UINT Flags;
	UINT ConstantBuffers;
	UINT BoundResources;
	UINT InputParameters;
	UINT OutputParameters;
	UINT InstructionCount;
	UINT TempRegisterCount;
	UINT TempArrayCount;
	UINT DefCount;
	UINT DclCount;
	UINT TextureNormalInstructions;
	UINT TextureLoadInstructions;
	UINT TextureCompInstructions;
	UINT TextureBiasInstructions;
	UINT TextureGradientInstructions;
	UINT FloatInstructionCount;
	UINT IntInstructionCount;
	UINT UintInstructionCount;
	UINT StaticFlowControlCount;
	UINT DynamicFlowControlCount;
	UINT MacroInstructionCount;
	UINT ArrayInstructionCount;
	UINT CutInstructionCount;
	UINT EmitInstructionCount;
	D3D_PRIMITIVE_TOPOLOGY GSOutputTopology;
	UINT GSMaxOutputVertexCount;
	D3D_PRIMITIVE InputPrimitive;
	UINT PatchConstantParameters;
	UINT cGSInstanceCount;
	UINT cControlPoints;
	D3D_TESSELLATOR_OUTPUT_PRIMITIVE HSOutputPrimitive;
	D3D_TESSELLATOR_PARTITIONING HSPartitioning;
	D3D_TESSELLATOR_DOMAIN TessellatorDomain;
	UINT cBarrierInstructions;
	UINT cInterlockedInstructions;
	UINT cTextureStoreInstructions;
};

struct D3D12_SHADER_BUFFER_DESC {
	LPCSTR Name;
	D3D_CBUFFER_TYPE Type;
	UINT Variables;
	UINT Size;
	UINT uFlags;
};

struct D3D12_SHADER_VARIABLE_DESC {
	LPCSTR Name;
	UINT StartOffset;
	UINT Size;
	UINT uFlags;
	LPVOID DefaultValue;
	UINT StartTexture;
	UINT TextureSize;
	UINT StartSampler;
	UINT SamplerSize;
};

struct D3D12_SHADER_TYPE_DESC {
	D3D_SHADER_VARIABLE_CLASS Class;
	D3D_SHADER_VARIABLE_TYPE Type;
	UINT Rows;
	UINT Columns;
	UINT Elements;
	UINT Members;
	UINT Offset;
	LPCSTR Name;
};

struct ID3D12ShaderReflectionConstantBuffer;
struct ID3D12ShaderReflectionVariable;

struct ID3D12ShaderReflection : public IUnknown {
	virtual HRESULT STDMETHODCALLTYPE GetDesc(D3D12_SHADER_DESC* desc) = 0;
	virtual ID3D12ShaderReflectionConstantBuffer* STDMETHODCALLTYPE GetConstantBufferByIndex(UINT index) = 0;
	virtual ID3D12ShaderReflectionConstantBuffer* STDMETHODCALLTYPE GetConstantBufferByName(LPCSTR name) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetResourceBindingDesc(UINT resource_index, D3D12_SHADER_INPUT_BIND_DESC* desc) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetInputParameterDesc(UINT parameter_index, D3D12_SIGNATURE_PARAMETER_DESC* desc) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetOutputParameterDesc(UINT parameter_index, D3D12_SIGNATURE_PARAMETER_DESC* desc) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPatchConstantParameterDesc(UINT parameter_index, D3D12_SIGNATURE_PARAMETER_DESC* desc) = 0;
	virtual ID3D12ShaderReflectionVariable* STDMETHODCALLTYPE GetVariableByName(LPCSTR name) = 0;
};
#endif

namespace {

struct parsed_cbuffer_resource {
	std::string label;
	size_t type_hash = 0;
	std::string error;
};

bool load_shader_desc(ID3D12ShaderReflection* reflection, D3D12_SHADER_DESC& desc, std::string& error) {
	desc = {};
	if (reflection == nullptr) {
		error = "Shader reflection interface is missing.";
		return false;
	}
	if (FAILED(reflection->GetDesc(&desc))) {
		error = "Failed to query DXC shader reflection metadata.";
		return false;
	}
	return true;
}

bool is_identifier_start(char c) {
	return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool is_identifier_char(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

void skip_spaces(std::string_view text, size_t& index) {
	while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])))
		++index;
}

bool consume_keyword(std::string_view text, size_t& index, std::string_view keyword) {
	skip_spaces(text, index);
	if (index + keyword.size() > text.size())
		return false;
	if (text.substr(index, keyword.size()) != keyword)
		return false;
	if (index + keyword.size() < text.size() && is_identifier_char(text[index + keyword.size()]))
		return false;
	index += keyword.size();
	return true;
}

bool parse_identifier(std::string_view text, size_t& index, std::string& out) {
	skip_spaces(text, index);
	if (index >= text.size() || !is_identifier_start(text[index]))
		return false;
	const size_t start = index++;
	while (index < text.size() && is_identifier_char(text[index]))
		++index;
	out.assign(text.substr(start, index - start));
	return true;
}

std::string strip_hlsl_comments(std::string_view source) {
	std::string result;
	result.reserve(source.size());
	bool in_line_comment = false;
	bool in_block_comment = false;
	for (size_t i = 0; i < source.size(); ++i) {
		const char c = source[i];
		const char next = i + 1 < source.size() ? source[i + 1] : '\0';
		if (in_line_comment) {
			if (c == '\n') {
				in_line_comment = false;
				result.push_back(c);
			}
			continue;
		}
		if (in_block_comment) {
			if (c == '*' && next == '/') {
				in_block_comment = false;
				++i;
			}
			continue;
		}
		if (c == '/' && next == '/') {
			in_line_comment = true;
			++i;
			continue;
		}
		if (c == '/' && next == '*') {
			in_block_comment = true;
			++i;
			continue;
		}
		result.push_back(c);
	}
	return result;
}

size_t parsed_uniform_type_hash(std::string_view type_name) {
	if (type_name == "float")
		return rv::detail::pin_type_hash<float>();
	if (type_name == "float2")
		return rv::detail::pin_type_hash<mars::vector2<float>>();
	if (type_name == "float3")
		return rv::detail::pin_type_hash<mars::vector3<float>>();
	if (type_name == "float4")
		return rv::detail::pin_type_hash<mars::vector4<float>>();
	if (type_name == "float4x4")
		return rv::detail::pin_type_hash<mars::matrix4<float>>();
	if (type_name == "uint")
		return rv::detail::pin_type_hash<unsigned int>();
	if (type_name == "uint2")
		return rv::detail::pin_type_hash<mars::vector2<unsigned int>>();
	if (type_name == "uint3")
		return rv::detail::pin_type_hash<mars::vector3<unsigned int>>();
	if (type_name == "uint4")
		return rv::detail::pin_type_hash<mars::vector4<unsigned int>>();
	return 0;
}

parsed_cbuffer_resource parse_cbuffer_body(std::string_view body) {
	parsed_cbuffer_resource result;
	size_t index = 0;
	size_t supported_value_count = 0;
	size_t declaration_count = 0;
	while (index < body.size()) {
		while (index < body.size() && (std::isspace(static_cast<unsigned char>(body[index])) || body[index] == ';'))
			++index;
		if (index >= body.size())
			break;

		std::string type_name;
		std::string variable_name;
		if (!parse_identifier(body, index, type_name) || !parse_identifier(body, index, variable_name)) {
			result.error = "only simple variable declarations are supported";
			return result;
		}
		while (index < body.size() && body[index] != ';')
			++index;

		++declaration_count;
		if (!variable_name.empty() && variable_name.front() == '_')
			continue;

		const size_t type_hash = parsed_uniform_type_hash(type_name);
		if (type_hash == 0) {
			result.error = "only float/uint scalar, vector, and float4x4 matrix values are supported";
			return result;
		}
		++supported_value_count;
		if (supported_value_count > 1) {
			result.error = "only one non-padding value is supported";
			return result;
		}
		result.label = std::move(variable_name);
		result.type_hash = type_hash;
	}

	if (supported_value_count == 0)
		result.error = declaration_count == 0 ? "the constant buffer is empty" : "no supported non-padding value was found";
	return result;
}

std::unordered_map<std::string, parsed_cbuffer_resource> parse_cbuffer_resources(std::string_view source) {
	std::unordered_map<std::string, parsed_cbuffer_resource> result;
	const std::string stripped = strip_hlsl_comments(source);
	std::string_view text = stripped;
	size_t index = 0;
	while (index < text.size()) {
		if (!consume_keyword(text, index, "cbuffer")) {
			++index;
			continue;
		}

		std::string cbuffer_name;
		if (!parse_identifier(text, index, cbuffer_name))
			continue;

		const size_t open_brace = text.find('{', index);
		if (open_brace == std::string_view::npos)
			break;
		size_t cursor = open_brace + 1;
		int depth = 1;
		while (cursor < text.size() && depth > 0) {
			if (text[cursor] == '{')
				++depth;
			else if (text[cursor] == '}')
				--depth;
			++cursor;
		}
		if (depth != 0)
			break;

		result[cbuffer_name] = parse_cbuffer_body(text.substr(open_brace + 1, cursor - open_brace - 2));
		index = cursor;
	}
	return result;
}

bool is_supported_system_input_semantic(const D3D12_SIGNATURE_PARAMETER_DESC& desc) {
	if (desc.SemanticName == nullptr)
		return false;

	const std::string_view semantic_name = desc.SemanticName;
	return semantic_name == "SV_VertexID" || semantic_name == "SV_InstanceID";
}

std::filesystem::path find_dxc_module() {
#ifdef _WIN32
	if (std::filesystem::exists("dxcompiler.dll"))
		return "dxcompiler.dll";
	if (const char* sdk = std::getenv("VULKAN_SDK")) {
		const std::filesystem::path candidate = std::filesystem::path(sdk) / "Bin" / "dxcompiler.dll";
		if (std::filesystem::exists(candidate))
			return candidate;
	}
#else
	if (const char* sdk = std::getenv("VULKAN_SDK")) {
		const std::filesystem::path candidate = std::filesystem::path(sdk) / "lib" / "libdxcompiler.so";
		if (std::filesystem::exists(candidate))
			return candidate;
	}
	for (const char* candidate : {"/usr/lib/libdxcompiler.so", "/usr/local/lib/libdxcompiler.so", "libdxcompiler.so"}) {
		if (std::filesystem::exists(candidate) || std::string_view(candidate) == "libdxcompiler.so")
			return candidate;
	}
#endif
	return {};
}

dxc_module_t load_dxc_module() {
	static dxc_module_t module = []() -> dxc_module_t {
		const std::filesystem::path candidate = find_dxc_module();
		if (candidate.empty())
			return nullptr;
#ifdef _WIN32
		if (candidate == "dxcompiler.dll")
			return LoadLibraryW(L"dxcompiler.dll");
		return LoadLibraryW(candidate.c_str());
#else
		return dlopen(candidate.c_str(), RTLD_LAZY);
#endif
	}();
	return module;
}

HRESULT dxc_create_instance(REFCLSID clsid, REFIID iid, void** out_object) {
	if (dxc_module_t module = load_dxc_module()) {
		const auto proc = reinterpret_cast<dxc_create_instance_proc>(
#ifdef _WIN32
			GetProcAddress(module, "DxcCreateInstance")
#else
			dlsym(module, "DxcCreateInstance")
#endif
		);
		if (proc != nullptr)
			return proc(clsid, iid, out_object);
	}
	return E_FAIL;
}

std::string extract_messages(IDxcResult* result) {
	ComPtr<IDxcBlobUtf8> errors;
	if (FAILED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) || !errors)
		return {};
	if (errors->GetStringLength() == 0)
		return {};
	return std::string(errors->GetStringPointer(), errors->GetStringLength());
}

bool append_signature_pin(std::vector<NE_Pin>& pins, const D3D12_SIGNATURE_PARAMETER_DESC& desc, bool inputs, std::ostringstream& warnings) {
	if (inputs && is_supported_system_input_semantic(desc))
		return true;

	NE_Pin pin;
	pin.label = std::string(desc.SemanticName ? desc.SemanticName : "VALUE") + std::to_string(desc.SemanticIndex);

	if (!inputs) {
		pin.type_hash = rv::detail::pin_type_hash<mars::vector3<unsigned char>>();
		pins.push_back(std::move(pin));
		return true;
	}

	pin.is_container = true;
	const int component_count =
		(desc.Mask & 0x1 ? 1 : 0) +
		(desc.Mask & 0x2 ? 1 : 0) +
		(desc.Mask & 0x4 ? 1 : 0) +
		(desc.Mask & 0x8 ? 1 : 0);
	if (component_count < 1 || component_count > 4) {
		warnings << "Unsupported semantic " << pin.label << ": invalid component mask.\n";
		return false;
	}

	switch (desc.ComponentType) {
	case D3D_REGISTER_COMPONENT_FLOAT32:
		switch (component_count) {
		case 1: pin.type_hash = rv::detail::pin_type_hash<float>(); break;
		case 2: pin.type_hash = rv::detail::pin_type_hash<mars::vector2<float>>(); break;
		case 3: pin.type_hash = rv::detail::pin_type_hash<mars::vector3<float>>(); break;
		case 4: pin.type_hash = rv::detail::pin_type_hash<mars::vector4<float>>(); break;
		}
		break;
	case D3D_REGISTER_COMPONENT_UINT32:
		switch (component_count) {
		case 1: pin.type_hash = rv::detail::pin_type_hash<unsigned int>(); break;
		case 2: pin.type_hash = rv::detail::pin_type_hash<mars::vector2<unsigned int>>(); break;
		case 3: pin.type_hash = rv::detail::pin_type_hash<mars::vector3<unsigned int>>(); break;
		case 4: pin.type_hash = rv::detail::pin_type_hash<mars::vector4<unsigned int>>(); break;
		}
		break;
	default:
		warnings << "Unsupported semantic " << pin.label << ": only float and uint signatures are supported.\n";
		return false;
	}

	pins.push_back(std::move(pin));
	return true;
}

bool is_supported_sampled_texture_dimension(UINT dimension) {
	switch (static_cast<D3D_SRV_DIMENSION>(dimension)) {
	case D3D_SRV_DIMENSION_TEXTURE2D:
	case D3D_SRV_DIMENSION_TEXTURE2DARRAY:
	case D3D_SRV_DIMENSION_TEXTURECUBE:
		return true;
	default:
		return false;
	}
}

mars_pipeline_stage merge_reflected_stage_visibility(mars_pipeline_stage lhs, mars_pipeline_stage rhs) {
	return lhs | rhs;
}

void append_or_merge_reflected_resource(
	std::vector<rv::raster::reflected_shader_resource>& resources,
	rv::raster::reflected_shader_resource resource,
	std::ostringstream& warnings
) {
	auto existing = std::find_if(resources.begin(), resources.end(), [&](const rv::raster::reflected_shader_resource& current) {
		return current.binding == resource.binding &&
			current.label == resource.label &&
			current.kind == resource.kind;
	});
	if (existing == resources.end()) {
		resources.push_back(std::move(resource));
		return;
	}
	if (existing->kind != resource.kind) {
		warnings << "Reflected resource '" << resource.label << "' changed kinds across shader stages and was kept from the first stage only.\n";
		return;
	}
	if (existing->type_hash != resource.type_hash) {
		warnings << "Reflected resource '" << resource.label << "' has conflicting types across shader stages and was kept from the first stage only.\n";
		return;
	}
	existing->stage = merge_reflected_stage_visibility(existing->stage, resource.stage);
}

bool reflect_shader_resources(
	ID3D12ShaderReflection* reflection,
	std::string_view source,
	UINT resource_count,
	mars_pipeline_stage stage,
	std::vector<rv::raster::reflected_shader_resource>& resources,
	std::ostringstream& warnings
) {
	const auto parsed_cbuffers = parse_cbuffer_resources(source);
	for (UINT index = 0; index < resource_count; ++index) {
		D3D12_SHADER_INPUT_BIND_DESC bind_desc = {};
		if (FAILED(reflection->GetResourceBindingDesc(index, &bind_desc))) {
			warnings << "Failed to inspect reflected shader resource " << index << ".\n";
			return false;
		}
		if (bind_desc.Type == D3D_SIT_SAMPLER)
			continue;
		if (bind_desc.Type == D3D_SIT_TEXTURE) {
			if (!is_supported_sampled_texture_dimension(bind_desc.Dimension)) {
				warnings << "Unsupported reflected texture resource '" << (bind_desc.Name != nullptr ? bind_desc.Name : "<unnamed>")
				         << "': only Texture2D, Texture2DArray and TextureCube are supported.\n";
				return false;
			}

			rv::raster::reflected_shader_resource resource;
			resource.label = bind_desc.Name != nullptr ? bind_desc.Name : "texture";
			resource.binding = bind_desc.BindPoint;
			resource.stage = stage;
			resource.kind = graph_shader_resource_kind::sampled_texture;
			append_or_merge_reflected_resource(resources, std::move(resource), warnings);
			continue;
		}
		if (bind_desc.Type != D3D_SIT_CBUFFER) {
			warnings << "Unsupported reflected shader resource '" << (bind_desc.Name != nullptr ? bind_desc.Name : "<unnamed>")
			         << "'. Supported resources are single-value constant buffers and sampled textures.\n";
			return false;
		}
		const std::string buffer_name = bind_desc.Name != nullptr
			? std::string(bind_desc.Name)
			: ("cbuffer_" + std::to_string(bind_desc.BindPoint));
		const auto parsed_it = parsed_cbuffers.find(buffer_name);
		if (parsed_it == parsed_cbuffers.end()) {
			warnings << "Reflected constant buffer '" << buffer_name
			         << "' could not be matched back to a source cbuffer declaration.\n";
			return false;
		}
		if (parsed_it->second.type_hash == 0) {
			warnings << "Unsupported reflected constant buffer '" << buffer_name
			         << "': " << parsed_it->second.error << ".\n";
			return false;
		}

		rv::raster::reflected_shader_resource resource;
		resource.label = parsed_it->second.label;
		resource.binding = bind_desc.BindPoint;
		resource.stage = stage;
		resource.kind = graph_shader_resource_kind::uniform_value;
		resource.type_hash = parsed_it->second.type_hash;
		append_or_merge_reflected_resource(resources, std::move(resource), warnings);
	}
	return true;
}

bool reflect_signature_pins(ID3D12ShaderReflection* reflection, bool inputs, UINT parameter_count, std::vector<NE_Pin>& pins, std::ostringstream& warnings) {
	pins.clear();
	pins.reserve(parameter_count);
	for (UINT index = 0; index < parameter_count; ++index) {
		D3D12_SIGNATURE_PARAMETER_DESC desc = {};
		const HRESULT hr = inputs
			? reflection->GetInputParameterDesc(index, &desc)
			: reflection->GetOutputParameterDesc(index, &desc);
		if (FAILED(hr)) {
			warnings << "Failed to inspect " << (inputs ? "input" : "output") << " signature parameter " << index << ".\n";
			return false;
		}
		if (!append_signature_pin(pins, desc, inputs, warnings))
			return false;
	}
	return true;
}

HRESULT compile_reflection_stage(
	IDxcCompiler3* compiler,
	IDxcUtils* utils,
	IDxcIncludeHandler* include_handler,
	std::string_view source,
	const wchar_t* entry,
	const wchar_t* profile,
	ComPtr<ID3D12ShaderReflection>& out_reflection,
	std::string& out_messages
) {
	DxcBuffer buffer = {};
	buffer.Ptr = source.data();
	buffer.Size = source.size();
	buffer.Encoding = DXC_CP_UTF8;

	std::vector<LPCWSTR> args = {
		L"-E", entry,
		L"-T", profile,
		L"-HV", L"2021",
	};

	ComPtr<IDxcResult> result;
	HRESULT hr = compiler->Compile(&buffer, args.data(), static_cast<UINT32>(args.size()), include_handler, IID_PPV_ARGS(&result));
	if (FAILED(hr) || !result) {
		out_messages = "Failed to invoke DXC.";
		return FAILED(hr) ? hr : E_FAIL;
	}

	out_messages = extract_messages(result.Get());

	HRESULT status = S_OK;
	result->GetStatus(&status);
	if (FAILED(status))
		return status;

	ComPtr<IDxcBlob> reflection_blob;
	hr = result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflection_blob), nullptr);
	if (FAILED(hr) || !reflection_blob) {
		out_messages += "\nFailed to retrieve DXC reflection output.";
		return FAILED(hr) ? hr : E_FAIL;
	}

	DxcBuffer reflection_buffer = {};
	reflection_buffer.Ptr = reflection_blob->GetBufferPointer();
	reflection_buffer.Size = reflection_blob->GetBufferSize();
	reflection_buffer.Encoding = 0;

	hr = utils->CreateReflection(&reflection_buffer, IID_PPV_ARGS(&out_reflection));
	if (FAILED(hr) || !out_reflection) {
		out_messages += "\nFailed to create shader reflection interface.";
		return FAILED(hr) ? hr : E_FAIL;
	}

	return S_OK;
}

} // namespace

namespace rv::raster {

std::string default_source() {
	return R"(cbuffer TintData : register(b0) {
    float4 tint;
};

cbuffer OffsetData : register(b1) {
    float2 uv_offset;
    float2 _uv_offset_padding;
};

cbuffer InstancePositionData : register(b2) {
    float3 instance_position;
    float _instance_position_padding;
};

cbuffer AlbedoData : register(b3) {
    uint albedo_texture_index;
    float3 _albedo_padding;
};

SamplerState linearSampler : register(s0);

struct VSInput {
    float3 position : POSITION0;
    float2 uv : TEXCOORD0;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    float3 world = input.position * 0.4 + instance_position.xyz;
    const float z_near = 0.1;
    const float z_far = 100.0;
    const float focal = 1.7320508;
    float z = max(world.z, z_near + 0.001);
    output.position = float4(
        world.x * focal,
        world.y * focal,
        z * (z_far / (z_far - z_near)) - (z_near * z_far / (z_far - z_near)),
        z
    );
    output.uv = input.uv;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target0 {
    float2 uv = frac(input.uv + uv_offset.xy);
    Texture2D<float4> albedoTexture = ResourceDescriptorHeap[albedo_texture_index];
    return albedoTexture.Sample(linearSampler, uv) * tint;
}
)";
}

compile_result compile_source(std::string_view source) {
	compile_result result;

	ComPtr<IDxcUtils> utils;
	ComPtr<IDxcCompiler3> compiler;
	ComPtr<IDxcIncludeHandler> include_handler;

	if (FAILED(dxc_create_instance(CLSID_DxcUtils, __uuidof(IDxcUtils), reinterpret_cast<void**>(utils.GetAddressOf()))) || !utils ||
		FAILED(dxc_create_instance(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), reinterpret_cast<void**>(compiler.GetAddressOf()))) || !compiler ||
		FAILED(utils->CreateDefaultIncludeHandler(include_handler.GetAddressOf())) || !include_handler) {
		result.diagnostics = "DXC is not available. Ensure dxcompiler is installed or accessible through VULKAN_SDK.";
		return result;
	}

	ComPtr<ID3D12ShaderReflection> vertex_reflection;
	ComPtr<ID3D12ShaderReflection> pixel_reflection;
	std::string vertex_messages;
	std::string pixel_messages;

	const HRESULT vs_hr = compile_reflection_stage(compiler.Get(), utils.Get(), include_handler.Get(), source, L"VSMain", L"vs_6_6", vertex_reflection, vertex_messages);
	if (FAILED(vs_hr)) {
		result.diagnostics = "Vertex shader compilation failed.\n" + vertex_messages;
		return result;
	}

	const HRESULT ps_hr = compile_reflection_stage(compiler.Get(), utils.Get(), include_handler.Get(), source, L"PSMain", L"ps_6_6", pixel_reflection, pixel_messages);
	if (FAILED(ps_hr)) {
		result.diagnostics = "Pixel shader compilation failed.\n" + pixel_messages;
		return result;
	}

	D3D12_SHADER_DESC vertex_desc = {};
	D3D12_SHADER_DESC pixel_desc = {};
	std::string reflection_error;
	if (!load_shader_desc(vertex_reflection.Get(), vertex_desc, reflection_error)) {
		result.diagnostics = reflection_error;
		return result;
	}
	if (!load_shader_desc(pixel_reflection.Get(), pixel_desc, reflection_error)) {
		result.diagnostics = reflection_error;
		return result;
	}

	std::ostringstream warnings;
	if (!reflect_signature_pins(vertex_reflection.Get(), true, vertex_desc.InputParameters, result.input_pins, warnings) ||
		!reflect_signature_pins(pixel_reflection.Get(), false, pixel_desc.OutputParameters, result.output_pins, warnings) ||
		!reflect_shader_resources(vertex_reflection.Get(), source, vertex_desc.BoundResources, MARS_PIPELINE_STAGE_VERTEX, result.resources, warnings) ||
		!reflect_shader_resources(pixel_reflection.Get(), source, pixel_desc.BoundResources, MARS_PIPELINE_STAGE_FRAGMENT, result.resources, warnings)) {
		std::ostringstream diagnostics;
		if (!vertex_messages.empty())
			diagnostics << "VSMain:\n" << vertex_messages << '\n';
		if (!pixel_messages.empty())
			diagnostics << "PSMain:\n" << pixel_messages << '\n';
		diagnostics << warnings.str();
		result.diagnostics = diagnostics.str();
		return result;
	}

	std::ostringstream diagnostics;
	if (!vertex_messages.empty())
		diagnostics << "VSMain:\n" << vertex_messages << '\n';
	if (!pixel_messages.empty())
		diagnostics << "PSMain:\n" << pixel_messages << '\n';
	const std::string warning_text = warnings.str();
	if (!warning_text.empty())
		diagnostics << warning_text;
	if (diagnostics.str().empty())
		diagnostics << "Compile successful.";

	result.success = true;
	result.diagnostics = diagnostics.str();
	return result;
}

} // namespace rv::raster
