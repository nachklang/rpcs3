#pragma once
#if defined (DX12_SUPPORT)

#include <d3d12.h>
#include <wrl/client.h>
#include "../Common/ProgramStateCache.h"
#include "D3D12VertexProgramDecompiler.h"
#include "D3D12FragmentProgramDecompiler.h"
#include "Utilities/File.h"



struct D3D12PipelineProperties
{
	D3D12_PRIMITIVE_TOPOLOGY_TYPE Topology;
	DXGI_FORMAT DepthStencilFormat;
	std::vector<D3D12_INPUT_ELEMENT_DESC> IASet;
	D3D12_BLEND_DESC Blend;
	unsigned numMRT : 3;
	bool depthEnabled : 1;

	bool operator==(const D3D12PipelineProperties &in) const
	{
		// TODO: blend and IASet equality
		return Topology == in.Topology && DepthStencilFormat == in.DepthStencilFormat && numMRT == in.numMRT && depthEnabled == in.depthEnabled;
	}
};

/** Storage for a shader
*   Embeds the D3DBlob
*/
struct Shader
{
public:
	enum class SHADER_TYPE
	{
		SHADER_TYPE_VERTEX,
		SHADER_TYPE_FRAGMENT
	};

	Shader() : bytecode(nullptr) {}
	~Shader() {}

	u32 Id;
	Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
	std::vector<size_t> FragmentConstantOffsetCache;

	/**
	* Decompile a fragment shader located in the PS3's Memory.  This function operates synchronously.
	* @param prog RSXShaderProgram specifying the location and size of the shader in memory
	*/
	//	void Decompile(RSXFragmentProgram& prog)

	/** Compile the decompiled fragment shader into a format we can use with OpenGL. */
	void Compile(const std::string &code, enum class SHADER_TYPE st);
};

struct D3D12Traits
{
	typedef Shader VertexProgramData;
	typedef Shader FragmentProgramData;
	typedef ID3D12PipelineState PipelineData;
	typedef D3D12PipelineProperties PipelineProperties;
	typedef std::pair<ID3D12Device *, ID3D12RootSignature *> ExtraData;

	static
	void RecompileFragmentProgram(RSXFragmentProgram *RSXFP, FragmentProgramData& fragmentProgramData, size_t ID)
	{
		D3D12FragmentDecompiler FS(RSXFP->addr, RSXFP->size, RSXFP->offset);
		const std::string &shader = FS.Decompile();
		fragmentProgramData.Compile(shader, Shader::SHADER_TYPE::SHADER_TYPE_FRAGMENT);

		for (const ParamType& PT : FS.m_parr.params[PF_PARAM_UNIFORM])
		{
			if (PT.type == "sampler2D") continue;
			for (const ParamItem PI : PT.items)
			{
				size_t offset = atoi(PI.name.c_str() + 2);
				fragmentProgramData.FragmentConstantOffsetCache.push_back(offset);
			}
		}

		// TODO: This shouldn't use current dir
		std::string filename = "./FragmentProgram" + std::to_string(ID) + ".hlsl";
		fs::file(filename, o_write | o_create | o_trunc).write(shader.c_str(), shader.size());
		fragmentProgramData.id = (u32)ID;
	}

	static
	void RecompileVertexProgram(RSXVertexProgram *RSXVP, VertexProgramData& vertexProgramData, size_t ID)
	{
		D3D12VertexProgramDecompiler VS(RSXVP->data);
		std::string shaderCode = VS.Decompile();
		vertexProgramData.Compile(shaderCode, Shader::SHADER_TYPE::SHADER_TYPE_VERTEX);

		// TODO: This shouldn't use current dir
		std::string filename = "./VertexProgram" + std::to_string(ID) + ".hlsl";
		fs::file(filename, o_write | o_create | o_trunc).write(shaderCode.c_str(), shaderCode.size());
		vertexProgramData.id = (u32)ID;
	}

	static
	PipelineData *BuildProgram(VertexProgramData &vertexProgramData, FragmentProgramData &fragmentProgramData, const PipelineProperties &pipelineProperties, const ExtraData& extraData)
	{
		ID3D12PipelineState *result;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicPipelineStateDesc = {};

		if (vertexProgramData.bytecode == nullptr)
			return nullptr;
		graphicPipelineStateDesc.VS.BytecodeLength = vertexProgramData.bytecode->GetBufferSize();
		graphicPipelineStateDesc.VS.pShaderBytecode = vertexProgramData.bytecode->GetBufferPointer();

		if (fragmentProgramData.bytecode == nullptr)
			return nullptr;
		graphicPipelineStateDesc.PS.BytecodeLength = fragmentProgramData.bytecode->GetBufferSize();
		graphicPipelineStateDesc.PS.pShaderBytecode = fragmentProgramData.bytecode->GetBufferPointer();

		graphicPipelineStateDesc.pRootSignature = extraData.second;

		// Sensible default value
		static D3D12_RASTERIZER_DESC CD3D12_RASTERIZER_DESC =
		{
			D3D12_FILL_MODE_SOLID,
			D3D12_CULL_MODE_NONE,
			FALSE,
			D3D12_DEFAULT_DEPTH_BIAS,
			D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
			D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
			TRUE,
			FALSE,
			FALSE,
			0,
			D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
		};

		static D3D12_DEPTH_STENCIL_DESC CD3D12_DEPTH_STENCIL_DESC =
		{
			TRUE,
			D3D12_DEPTH_WRITE_MASK_ALL,
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			FALSE,
			D3D12_DEFAULT_STENCIL_READ_MASK,
			D3D12_DEFAULT_STENCIL_WRITE_MASK,
		};

		graphicPipelineStateDesc.BlendState = pipelineProperties.Blend;
		graphicPipelineStateDesc.DepthStencilState = CD3D12_DEPTH_STENCIL_DESC;
		graphicPipelineStateDesc.RasterizerState = CD3D12_RASTERIZER_DESC;
		graphicPipelineStateDesc.PrimitiveTopologyType = pipelineProperties.Topology;

		graphicPipelineStateDesc.DepthStencilState.DepthEnable = pipelineProperties.depthEnabled;

		graphicPipelineStateDesc.NumRenderTargets = pipelineProperties.numMRT;
		for (unsigned i = 0; i < pipelineProperties.numMRT; i++)
			graphicPipelineStateDesc.RTVFormats[i] = DXGI_FORMAT_R8G8B8A8_UNORM;
		graphicPipelineStateDesc.DSVFormat = pipelineProperties.DepthStencilFormat;

		graphicPipelineStateDesc.InputLayout.pInputElementDescs = pipelineProperties.IASet.data();
		graphicPipelineStateDesc.InputLayout.NumElements = (UINT)pipelineProperties.IASet.size();
		graphicPipelineStateDesc.SampleDesc.Count = 1;
		graphicPipelineStateDesc.SampleMask = UINT_MAX;
		graphicPipelineStateDesc.NodeMask = 1;

		extraData.first->CreateGraphicsPipelineState(&graphicPipelineStateDesc, IID_PPV_ARGS(&result));
		return result;
	}

	static
	void DeleteProgram(PipelineData *ptr)
	{
		ptr->Release();
	}
};

class PipelineStateObjectCache : public ProgramStateCache<D3D12Traits>
{
};



#endif