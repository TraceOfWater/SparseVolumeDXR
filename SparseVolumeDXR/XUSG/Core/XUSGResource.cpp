//--------------------------------------------------------------------------------------
// By Stars XU Tianchen
//--------------------------------------------------------------------------------------

#include "DXFrameworkHelper.h"
#include "XUSGResource.h"

#define REMOVE_PACKED_UAV	ResourceFlags(~0x8000)

using namespace std;
using namespace XUSG;

Format MapToPackedFormat(Format &format)
{
	auto formatUAV = DXGI_FORMAT_R32_UINT;

	switch (format)
	{
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
		format = DXGI_FORMAT_R10G10B10A2_TYPELESS;
		break;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
		format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
		break;
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
		break;
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		format = DXGI_FORMAT_B8G8R8X8_TYPELESS;
		break;
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
		format = DXGI_FORMAT_R16G16_TYPELESS;
		break;
	default:
		formatUAV = format;
	}

	return formatUAV;
}

//--------------------------------------------------------------------------------------
// Constant buffer
//--------------------------------------------------------------------------------------

ConstantBuffer::ConstantBuffer() :
	m_device(nullptr),
	m_resource(nullptr),
	m_cbvPool(nullptr),
	m_cbvs(0),
	m_currentCbv(D3D12_DEFAULT),
	m_cbvOffsets(0),
	m_pDataBegin(nullptr)
{
}

ConstantBuffer::~ConstantBuffer()
{
	if (m_resource) Unmap();
}

bool ConstantBuffer::Create(const Device &device, uint32_t byteWidth, uint32_t numCBVs, const uint32_t *offsets,
	const wchar_t *name)
{
	M_RETURN(!device, cerr, "The device is NULL.", false);
	m_device = device;

	// Instanced CBVs
	vector<uint32_t> offsetList;
	if (!offsets)
	{
		auto numBytes = 0u;
		// CB size is required to be D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT-byte aligned.
		const auto cbvSize = ALIGN(byteWidth / numCBVs, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		offsetList.resize(numCBVs);

		for (auto &offset : offsetList)
		{
			offset = numBytes;
			numBytes += cbvSize;
		}

		offsets = offsetList.data();
		byteWidth = cbvSize * numCBVs;
	}

	const auto strideCbv = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	V_RETURN(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(byteWidth),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_resource)), clog, false);
	if (name) m_resource->SetName((wstring(name) + L".Resource").c_str());

	// Allocate descriptor pool
	N_RETURN(allocateDescriptorPool(numCBVs, name), false);

	// Describe and create a constant buffer view.
	D3D12_CONSTANT_BUFFER_VIEW_DESC desc;

	m_cbvs.resize(numCBVs);
	m_cbvOffsets.resize(numCBVs);
	for (auto i = 0u; i < numCBVs; ++i)
	{
		const auto &offset = offsets[i];
		desc.BufferLocation = m_resource->GetGPUVirtualAddress() + offset;
		desc.SizeInBytes = (i + 1 >= numCBVs ? byteWidth : offsets[i + 1]) - offset;

		m_cbvOffsets[i] = offset;

		// Create a constant buffer view
		m_cbvs[i] = m_currentCbv;
		m_device->CreateConstantBufferView(&desc, m_cbvs[i]);
		m_currentCbv.Offset(strideCbv);
	}

	return true;
}

void *ConstantBuffer::Map(uint32_t i)
{
	if (m_pDataBegin == nullptr)
	{
		// Map and initialize the constant buffer. We don't unmap this until the
		// app closes. Keeping things mapped for the lifetime of the resource is okay.
		CD3DX12_RANGE readRange(0, 0);	// We do not intend to read from this resource on the CPU.
		V_RETURN(m_resource->Map(0, &readRange, &m_pDataBegin), cerr, false);
	}

	return &reinterpret_cast<uint8_t*>(m_pDataBegin)[m_cbvOffsets[i]];
}

void ConstantBuffer::Unmap()
{
	if (m_pDataBegin)
	{
		m_resource->Unmap(0, nullptr);
		m_pDataBegin = nullptr;
	}
}

const Resource &ConstantBuffer::GetResource() const
{
	return m_resource;
}

Descriptor ConstantBuffer::GetCBV(uint32_t i) const
{
	return m_cbvs.size() > i ? m_cbvs[i] : Descriptor(D3D12_DEFAULT);
}

bool ConstantBuffer::allocateDescriptorPool(uint32_t numDescriptors, const wchar_t *name)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	V_RETURN(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_cbvPool)), cerr, false);
	if (name) m_cbvPool->SetName((wstring(name) + L".CbvPool").c_str());

	m_currentCbv = m_cbvPool->GetCPUDescriptorHandleForHeapStart();

	return true;
}

//--------------------------------------------------------------------------------------
// Resource base
//--------------------------------------------------------------------------------------

ResourceBase::ResourceBase() :
	m_device(nullptr),
	m_resource(nullptr),
	m_srvUavPool(nullptr),
	m_srvs(0),
	m_currentSrvUav(D3D12_DEFAULT),
	m_states()
{
}

ResourceBase::~ResourceBase()
{
}

void ResourceBase::Barrier(const CommandList &commandList, ResourceState dstState,
	uint32_t subresource)
{
	const auto &state = m_states[subresource == 0xffffffff ? 0 : subresource];
	if (state != dstState || dstState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		commandList.Barrier(1, &Transition(dstState, subresource));
}

const Resource &ResourceBase::GetResource() const
{
	return m_resource;
}

Descriptor ResourceBase::GetSRV(uint32_t i) const
{
	return m_srvs.size() > i ? m_srvs[i] : Descriptor(D3D12_DEFAULT);
}

ResourceBarrier ResourceBase::Transition(ResourceState dstState, uint32_t subresource)
{
	auto &state = m_states[subresource == 0xffffffff ? 0 : subresource];
	const auto srcState = state;
	state = dstState;

	return srcState == dstState && dstState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS ?
		ResourceBarrier::UAV(m_resource.get()) :
		ResourceBarrier::Transition(m_resource.get(), srcState, dstState, subresource);
}

ResourceState ResourceBase::GetResourceState(uint32_t i) const
{
	return m_states[i];
}

void ResourceBase::setDevice(const Device & device)
{
	m_device = device;

	if (m_device)
		m_srvUavStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

bool ResourceBase::allocateDescriptorPool(uint32_t numDescriptors)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	V_RETURN(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvUavPool)), cerr, false);
	if (!m_name.empty()) m_srvUavPool->SetName((m_name + L".SrvUavPool").c_str());

	m_currentSrvUav = m_srvUavPool->GetCPUDescriptorHandleForHeapStart();

	return true;
}

//--------------------------------------------------------------------------------------
// 2D Texture
//--------------------------------------------------------------------------------------

Texture2D::Texture2D() :
	ResourceBase(),
	m_counter(nullptr),
	m_uavs(0),
	m_srvLevels(0)
{
}

Texture2D::~Texture2D()
{
}

bool Texture2D::Create(const Device &device, uint32_t width, uint32_t height, Format format,
	uint32_t arraySize, ResourceFlags resourceFlags, uint8_t numMips, uint8_t sampleCount,
	PoolType poolType, ResourceState state, bool isCubeMap, const wchar_t *name)
{
	M_RETURN(!device, cerr, "The device is NULL.", false);
	setDevice(device);

	if (name) m_name = name;

	const auto isPacked = (resourceFlags & BIND_PACKED_UAV) == BIND_PACKED_UAV;
	resourceFlags &= REMOVE_PACKED_UAV;

	const auto hasSRV = !(resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	const bool hasUAV = resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	// Map formats
	auto formatResource = format;
	const auto formatUAV = isPacked ? MapToPackedFormat(formatResource) : format;

	// Setup the texture description.
	const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(formatResource, width, height, arraySize,
		numMips, sampleCount, 0, resourceFlags);

	// Determine initial state
	m_states.resize(arraySize * numMips);
	if (state) for (auto &initState : m_states) initState = state;
	else for (auto &initState : m_states)
	{
		initState = hasSRV ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON;
		initState = hasUAV ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : initState;
	}

	V_RETURN(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(poolType),
		D3D12_HEAP_FLAG_NONE, &desc, m_states[0], nullptr, IID_PPV_ARGS(&m_resource)), clog, false);
	if (!m_name.empty()) m_resource->SetName((m_name + L".Resource").c_str());

	// Allocate descriptor pool
	auto numDescriptors = hasSRV ? (max)(numMips, 1ui8) : 0u;
	numDescriptors += hasUAV ? (max)(numMips, 1ui8) : 0;
	numDescriptors += hasSRV && hasUAV && numMips > 1 ? numMips : 0;
	N_RETURN(allocateDescriptorPool(numDescriptors), false);

	// Create SRV
	if (hasSRV) CreateSRVs(arraySize, format, numMips, sampleCount, isCubeMap);

	// Create UAVs
	if (hasUAV) CreateUAVs(arraySize, formatUAV, numMips);

	// Create SRV for each level
	if (hasSRV && hasUAV) CreateSRVLevels(arraySize, numMips, format, sampleCount, isCubeMap);

	return true;
}

bool Texture2D::Upload(const CommandList &commandList, Resource &resourceUpload,
	SubresourceData *pSubresourceData, uint32_t numSubresources, ResourceState dstState)
{
	N_RETURN(pSubresourceData, false);

	const auto uploadBufferSize = GetRequiredIntermediateSize(m_resource.get(), 0, numSubresources);

	// Create the GPU upload buffer.
	V_RETURN(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resourceUpload)), clog, false);
	if (!m_name.empty()) resourceUpload->SetName((m_name + L".UploaderResource").c_str());

	// Copy data to the intermediate upload heap and then schedule a copy 
	// from the upload heap to the Texture2D.
	const auto &curState = m_states[0];
	dstState = dstState ? dstState : curState;
	if (curState != D3D12_RESOURCE_STATE_COPY_DEST) Barrier(commandList, D3D12_RESOURCE_STATE_COPY_DEST);
	M_RETURN(UpdateSubresources(const_cast<CommandList&>(commandList).GetCommandList().get(),
		m_resource.get(), resourceUpload.get(), 0, 0, numSubresources, pSubresourceData) <= 0,
		clog, "Failed to upload the resource.", false);
	Barrier(commandList, dstState);

	return true;
}

bool Texture2D::Upload(const CommandList &commandList, Resource &resourceUpload,
	const uint8_t *pData, uint8_t stride, ResourceState dstState)
{
	const auto desc = m_resource->GetDesc();

	SubresourceData subresourceData;
	subresourceData.pData = pData;
	subresourceData.RowPitch = stride * static_cast<uint32_t>(desc.Width);
	subresourceData.SlicePitch = subresourceData.RowPitch * desc.Height;

	return Upload(commandList, resourceUpload, &subresourceData, 1, dstState);
}

void Texture2D::CreateSRVs(uint32_t arraySize, Format format, uint8_t numMips,
	uint8_t sampleCount, bool isCubeMap)
{
	// Setup the description of the shader resource view.
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = format ? format : m_resource->GetDesc().Format;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	auto mipLevel = 0ui8;
	m_srvs.resize(sampleCount > 1 ? 1 : (max)(numMips, 1ui8));

	for (auto &descriptor : m_srvs)
	{
		if (isCubeMap)
		{
			assert(arraySize % 6 == 0);
			if (arraySize > 6)
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
				desc.TextureCubeArray.MipLevels = numMips - mipLevel;
				desc.TextureCubeArray.MostDetailedMip = mipLevel++;
				desc.TextureCubeArray.NumCubes = arraySize / 6;
			}
			else
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
				desc.TextureCube.MipLevels = numMips - mipLevel;
				desc.TextureCube.MostDetailedMip = mipLevel++;
			}
		}
		else if (arraySize > 1)
		{
			if (sampleCount > 1)
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
				desc.Texture2DMSArray.ArraySize = arraySize;
			}
			else
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				desc.Texture2DArray.ArraySize = arraySize;
				desc.Texture2DArray.MipLevels = numMips - mipLevel;
				desc.Texture2DArray.MostDetailedMip = mipLevel++;
			}
		}
		else
		{
			if (sampleCount > 1)
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			else
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				desc.Texture2D.MipLevels = numMips - mipLevel;
				desc.Texture2D.MostDetailedMip = mipLevel++;
			}
		}

		// Create a shader resource view
		descriptor = m_currentSrvUav;
		m_device->CreateShaderResourceView(m_resource.get(), &desc, descriptor);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

void Texture2D::CreateSRVLevels(uint32_t arraySize, uint8_t numMips, Format format,
	uint8_t sampleCount, bool isCubeMap)
{
	if (numMips > 1)
	{
		// Setup the description of the shader resource view.
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = format ? format : m_resource->GetDesc().Format;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		auto mipLevel = 0ui8;
		m_srvLevels.resize(numMips);

		for (auto &descriptor : m_srvLevels)
		{
			// Setup the description of the shader resource view.
			if (isCubeMap)
			{
				assert(arraySize % 6 == 0);
				if (arraySize > 6)
				{
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
					desc.TextureCubeArray.MostDetailedMip = mipLevel++;
					desc.TextureCubeArray.MipLevels = 1;
					desc.TextureCubeArray.NumCubes = arraySize / 6;
				}
				else
				{
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
					desc.TextureCube.MostDetailedMip = mipLevel++;
					desc.TextureCube.MipLevels = 1;
				}
			}
			else if (arraySize > 1)
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
				desc.Texture2DArray.MostDetailedMip = mipLevel++;
				desc.Texture2DArray.MipLevels = 1;
			}
			else
			{
				desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				desc.Texture2D.MostDetailedMip = mipLevel++;
				desc.Texture2D.MipLevels = 1;
			}

			descriptor = m_currentSrvUav;
			m_device->CreateShaderResourceView(m_resource.get(), &desc, descriptor);
			m_currentSrvUav.Offset(m_srvUavStride);
		}
	}
}

void Texture2D::CreateUAVs(uint32_t arraySize, Format format, uint8_t numMips)
{
	// Setup the description of the unordered access view.
	D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
	desc.Format = format ? format : m_resource->GetDesc().Format;

	auto mipLevel = 0ui8;
	m_uavs.resize((max)(numMips, 1ui8));
	
	for (auto &descriptor : m_uavs)
	{
		// Setup the description of the unordered access view.
		if (arraySize > 1)
		{
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.ArraySize = arraySize;
			desc.Texture2DArray.MipSlice = mipLevel++;
		}
		else
		{
			desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			desc.Texture2D.MipSlice = mipLevel++;
		}

		// Create an unordered access view
		descriptor = m_currentSrvUav;
		m_device->CreateUnorderedAccessView(m_resource.get(), m_counter.get(), &desc, descriptor);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

Descriptor Texture2D::GetUAV(uint8_t i) const
{
	return m_uavs.size() > i ? m_uavs[i] : Descriptor(D3D12_DEFAULT);
}

Descriptor Texture2D::GetSRVLevel(uint8_t i) const
{
	return m_srvLevels.size() > i ? m_srvLevels[i] : Descriptor(D3D12_DEFAULT);
}

//--------------------------------------------------------------------------------------
// Render target
//--------------------------------------------------------------------------------------

RenderTarget::RenderTarget() :
	Texture2D(),
	m_rtvPool(nullptr),
	m_rtvs(0),
	m_currentRtv(D3D12_DEFAULT)
{
}

RenderTarget::~RenderTarget()
{
}

bool RenderTarget::Create(const Device &device, uint32_t width, uint32_t height, Format format,
	uint32_t arraySize, ResourceFlags resourceFlags, uint8_t numMips, uint8_t sampleCount,
	ResourceState state, const float *pClearColor, bool isCubeMap, const wchar_t *name)
{
	N_RETURN(create(device, width, height, arraySize, format, numMips, sampleCount,
		resourceFlags, state, pClearColor, isCubeMap, name), false);

	numMips = (max)(numMips, 1ui8);
	N_RETURN(allocateRtvPool(numMips * arraySize), false);

	// Setup the description of the render target view.
	D3D12_RENDER_TARGET_VIEW_DESC desc = {};
	desc.Format = format;

	m_rtvs.resize(arraySize);
	for (auto i = 0u; i < arraySize; ++i)
	{
		auto mipLevel = 0ui8;
		m_rtvs[i].resize(numMips);

		for (auto &descriptor : m_rtvs[i])
		{
			// Setup the description of the render target view.
			if (arraySize > 1)
			{
				if (sampleCount > 1)
				{
					desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
					desc.Texture2DMSArray.FirstArraySlice = i;
					desc.Texture2DMSArray.ArraySize = 1;
				}
				else
				{
					desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
					desc.Texture2DArray.FirstArraySlice = i;
					desc.Texture2DArray.ArraySize = 1;
					desc.Texture2DArray.MipSlice = mipLevel++;
				}
			}
			else
			{
				if (sampleCount > 1)
					desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
				else
				{
					desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
					desc.Texture2D.MipSlice = mipLevel++;
				}
			}

			// Create a render target view
			descriptor = m_currentRtv;
			m_device->CreateRenderTargetView(m_resource.get(), &desc, descriptor);
			m_currentRtv.Offset(m_rtvStride);
		}
	}

	return true;
}

bool RenderTarget::CreateArray(const Device &device, uint32_t width, uint32_t height,
	uint32_t arraySize, Format format, ResourceFlags resourceFlags, uint8_t numMips,
	uint8_t sampleCount, ResourceState state, const float *pClearColor, bool isCubeMap,
	const wchar_t *name)
{
	N_RETURN(create(device, width, height, arraySize, format, numMips, sampleCount,
		resourceFlags, state, pClearColor, isCubeMap, name), false);

	numMips = (max)(numMips, 1ui8);
	N_RETURN(allocateRtvPool(numMips), false);

	// Setup the description of the render target view.
	D3D12_RENDER_TARGET_VIEW_DESC desc = {};
	desc.Format = format;

	m_rtvs.resize(1);
	m_rtvs[0].resize(numMips);

	auto mipLevel = 0ui8;
	for (auto &descriptor : m_rtvs[0])
	{
		// Setup the description of the render target view.
		if (sampleCount > 1)
		{
			desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
			desc.Texture2DMSArray.ArraySize = arraySize;
		}
		else
		{
			desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.ArraySize = arraySize;
			desc.Texture2DArray.MipSlice = mipLevel++;
		}

		// Create a render target view
		descriptor = m_currentRtv;
		m_device->CreateRenderTargetView(m_resource.get(), &desc, descriptor);
		m_currentRtv.Offset(m_rtvStride);
	}

	return true;
}

bool RenderTarget::CreateFromSwapChain(const Device &device, const SwapChain &swapChain, uint32_t bufferIdx)
{
	M_RETURN(!device, cerr, "The device is NULL.", false);
	setDevice(device);

	m_name = L"SwapChain[" + to_wstring(bufferIdx) + L"]";

	m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// Determine initial state
	m_states.resize(1);
	m_states[0] = D3D12_RESOURCE_STATE_PRESENT;

	// Get resource
	V_RETURN(swapChain->GetBuffer(bufferIdx, IID_PPV_ARGS(&m_resource)), cerr, false);

	// Allocate descriptor pool
	N_RETURN(allocateRtvPool(1), false);

	// Create RTV
	m_rtvs.resize(1);
	m_rtvs[0].resize(1);
	m_rtvs[0][0] = m_currentRtv;
	m_device->CreateRenderTargetView(m_resource.get(), nullptr, m_rtvs[0][0]);
	m_currentRtv.Offset(m_rtvStride);

	return true;
}

void RenderTarget::Populate(const CommandList &commandList, const PipelineLayout &pipelineLayout,
	const Pipeline &pipeline, const DescriptorTable &srcSrvTable, const DescriptorTable &samplerTable,
	uint32_t srcSlot, uint32_t samplerSlot, uint8_t mipLevel, int32_t slice)
{
	// Set render target
	const auto rtvTable = make_shared<Descriptor>(GetRTV(slice, mipLevel));
	Barrier(commandList, D3D12_RESOURCE_STATE_RENDER_TARGET, slice * GetNumMips(slice) + mipLevel);
	commandList.OMSetRenderTargets(1, rtvTable, nullptr);

	// Set pipeline layout and descriptor tables
	commandList.SetGraphicsPipelineLayout(pipelineLayout);
	commandList.SetGraphicsDescriptorTable(srcSlot, srcSrvTable);
	commandList.SetGraphicsDescriptorTable(samplerSlot, samplerTable);

	// Set pipeline
	commandList.SetPipelineState(pipeline);

	// Set viewport
	const auto desc = m_resource->GetDesc();
	const auto width = static_cast<uint32_t>(desc.Width >> mipLevel);
	const auto height = static_cast<uint32_t>(desc.Height >> mipLevel);
	const Viewport viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
	const RectRange rect(0, 0, width, height);
	commandList.RSSetViewports(1, &viewport);
	commandList.RSSetScissorRects(1, &rect);

	// Draw quad
	commandList.IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList.Draw(3, 1, 0, 0);
}

Descriptor RenderTarget::GetRTV(uint32_t slice, uint8_t mipLevel) const
{
	return m_rtvs.size() > slice && m_rtvs[slice].size() > mipLevel ?
		m_rtvs[slice][mipLevel] : Descriptor(D3D12_DEFAULT);
}

uint32_t RenderTarget::GetArraySize() const
{
	return static_cast<uint32_t>(m_rtvs.size());
}

uint8_t RenderTarget::GetNumMips(uint32_t slice) const
{
	return m_rtvs.size() > slice ? static_cast<uint8_t>(m_rtvs[slice].size()) : 0;
}

bool RenderTarget::create(const Device &device, uint32_t width, uint32_t height,
	uint32_t arraySize, Format format, uint8_t numMips, uint8_t sampleCount,
	ResourceFlags resourceFlags, ResourceState state, const float *pClearColor,
	bool isCubeMap, const wchar_t *name)
{
	M_RETURN(!device, cerr, "The device is NULL.", false);
	setDevice(device);

	if (name) m_name = name;

	m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	const auto isPacked = (resourceFlags & BIND_PACKED_UAV) == BIND_PACKED_UAV;;
	resourceFlags &= REMOVE_PACKED_UAV;

	const auto hasSRV = !(resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	const bool hasUAV = resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	// Map formats
	auto formatResource = format;
	const auto formatUAV = isPacked ? MapToPackedFormat(formatResource) : format;

	// Setup the texture description.
	const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(formatResource, width, height, arraySize,
		numMips, sampleCount, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | resourceFlags);

	// Determine initial state
	m_states.resize(arraySize * numMips);
	for (auto &initState : m_states) initState = state ? state : D3D12_RESOURCE_STATE_RENDER_TARGET;

	// Optimized clear value
	D3D12_CLEAR_VALUE clearValue = { format };
	if (pClearColor) memcpy(clearValue.Color, pClearColor, sizeof(clearValue.Color));

	// Create the render target texture.
	V_RETURN(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE, &desc, m_states[0], &clearValue, IID_PPV_ARGS(&m_resource)), clog, false);
	if (!m_name.empty()) m_resource->SetName((m_name + L".Resource").c_str());
	
	// Allocate descriptor pool
	auto numDescriptors = hasSRV ? 1 + (numMips > 1 ? numMips : 0) : 0u;
	numDescriptors += hasUAV ? numMips : 0;
	numDescriptors += hasSRV && numMips > 1 ? numMips : 0;	// Sub SRVs
	N_RETURN(allocateDescriptorPool(numDescriptors), false);

	// Create SRV
	if (hasSRV)
	{
		CreateSRVs(arraySize, format, numMips, sampleCount, isCubeMap);
		CreateSRVLevels(arraySize, numMips, format, sampleCount, isCubeMap);
	}

	// Create UAV
	if (hasUAV) CreateUAVs(arraySize, formatUAV, numMips);

	return true;
}

bool RenderTarget::allocateRtvPool(uint32_t numDescriptors)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	V_RETURN(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvPool)), cerr, false);
	if (!m_name.empty()) m_rtvPool->SetName((m_name + L".RtvPool").c_str());
	
	m_currentRtv = m_rtvPool->GetCPUDescriptorHandleForHeapStart();

	return true;
}

//--------------------------------------------------------------------------------------
// Depth stencil
//--------------------------------------------------------------------------------------

DepthStencil::DepthStencil() :
	Texture2D(),
	m_dsvPool(nullptr),
	m_dsvs(0),
	m_readOnlyDsvs(0),
	m_stencilSrv(D3D12_DEFAULT),
	m_currentDsv(D3D12_DEFAULT)
{
}

DepthStencil::~DepthStencil()
{
}

bool DepthStencil::Create(const Device &device, uint32_t width, uint32_t height, Format format,
	ResourceFlags resourceFlags, uint32_t arraySize, uint8_t numMips, uint8_t sampleCount,
	ResourceState state, float clearDepth, uint8_t clearStencil, bool isCubeMap,
	const wchar_t *name)
{
	bool hasSRV;
	Format formatStencil;
	N_RETURN(create(device, width, height, arraySize, numMips, sampleCount, format, resourceFlags,
		state, clearDepth, clearStencil, hasSRV, formatStencil, isCubeMap, name), false);

	numMips = (max)(numMips, 1ui8);
	N_RETURN(allocateDsvPool(hasSRV ? numMips * arraySize * 2 : numMips * arraySize), false);

	// Setup the description of the depth stencil view.
	D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
	desc.Format = format;

	m_dsvs.resize(arraySize);
	m_readOnlyDsvs.resize(arraySize);

	for (auto i = 0u; i < arraySize; ++i)
	{
		auto mipLevel = 0ui8;
		m_dsvs[i].resize(numMips);
		m_readOnlyDsvs[i].resize(numMips);

		for (auto j = 0ui8; j < numMips; ++j)
		{
			auto &dsv = m_dsvs[i][j];
			auto &readOnlyDsv = m_readOnlyDsvs[i][j];

			// Setup the description of the depth stencil view.
			if (arraySize > 1)
			{
				if (sampleCount > 1)
				{
					desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
					desc.Texture2DMSArray.FirstArraySlice = i;
					desc.Texture2DMSArray.ArraySize = 1;
				}
				else
				{
					desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
					desc.Texture2DArray.FirstArraySlice = i;
					desc.Texture2DArray.ArraySize = 1;
					desc.Texture2DArray.MipSlice = j;
				}
			}
			else
			{
				if (sampleCount > 1)
					desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
				else
				{
					desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
					desc.Texture2D.MipSlice = j;
				}
			}

			// Create a depth stencil view
			dsv = m_currentDsv;
			m_device->CreateDepthStencilView(m_resource.get(), &desc, dsv);
			m_currentDsv.Offset(m_dsvStride);

			// Read-only depth stencil
			if (hasSRV)
			{
				// Setup the description of the depth stencil view.
				desc.Flags = formatStencil ? D3D12_DSV_FLAG_READ_ONLY_DEPTH |
					D3D12_DSV_FLAG_READ_ONLY_STENCIL : D3D12_DSV_FLAG_READ_ONLY_DEPTH;

				// Create a depth stencil view
				readOnlyDsv = m_currentDsv;
				m_device->CreateDepthStencilView(m_resource.get(), &desc, readOnlyDsv);
				m_currentDsv.Offset(m_dsvStride);
			}
			else readOnlyDsv = dsv;
		}
	}

	return true;
}

bool DepthStencil::CreateArray(const Device &device, uint32_t width, uint32_t height, uint32_t arraySize,
	Format format, ResourceFlags resourceFlags, uint8_t numMips, uint8_t sampleCount, ResourceState state,
	float clearDepth, uint8_t clearStencil, bool isCubeMap, const wchar_t *name)
{
	bool hasSRV;
	Format formatStencil;
	N_RETURN(create(device, width, height, arraySize, numMips, sampleCount, format, resourceFlags,
		state, clearDepth, clearStencil, hasSRV, formatStencil, isCubeMap, name), false);

	numMips = (max)(numMips, 1ui8);
	N_RETURN(allocateDsvPool(hasSRV ? numMips * 2 : numMips), false);

	// Setup the description of the depth stencil view.
	D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
	desc.Format = format;

	m_dsvs.resize(1);
	m_dsvs[0].resize(numMips);
	m_readOnlyDsvs.resize(1);
	m_readOnlyDsvs[0].resize(numMips);

	for (auto i = 0ui8; i < numMips; ++i)
	{
		auto &dsv = m_dsvs[0][i];
		auto &readOnlyDsv = m_readOnlyDsvs[0][i];

		// Setup the description of the depth stencil view.
		if (arraySize > 1)
		{
			if (sampleCount > 1)
			{
				desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
				desc.Texture2DMSArray.ArraySize = arraySize;
			}
			else
			{
				desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
				desc.Texture2DArray.ArraySize = arraySize;
				desc.Texture2DArray.MipSlice = i;
			}
		}
		else
		{
			if (sampleCount > 1)
				desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
			else
			{
				desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				desc.Texture2D.MipSlice = i;
			}
		}

		// Create a depth stencil view
		dsv = m_currentDsv;
		m_device->CreateDepthStencilView(m_resource.get(), &desc, dsv);
		m_currentDsv.Offset(m_dsvStride);

		// Read-only depth stencil
		if (hasSRV)
		{
			// Setup the description of the depth stencil view.
			desc.Flags = formatStencil ? D3D12_DSV_FLAG_READ_ONLY_DEPTH |
				D3D12_DSV_FLAG_READ_ONLY_STENCIL : D3D12_DSV_FLAG_READ_ONLY_DEPTH;

			// Create a depth stencil view
			readOnlyDsv = m_currentDsv;
			m_device->CreateDepthStencilView(m_resource.get(), &desc, readOnlyDsv);
			m_currentDsv.Offset(m_dsvStride);
		}
		else readOnlyDsv = dsv;
	}

	return true;
}

Descriptor DepthStencil::GetDSV(uint32_t slice, uint8_t mipLevel) const
{
	return m_dsvs.size() > slice && m_dsvs[slice].size() > mipLevel ?
		m_dsvs[slice][mipLevel] : Descriptor(D3D12_DEFAULT);
}

Descriptor DepthStencil::GetReadOnlyDSV(uint32_t slice, uint8_t mipLevel) const
{
	return m_readOnlyDsvs.size() > slice && m_readOnlyDsvs[slice].size() > mipLevel ?
		m_readOnlyDsvs[slice][mipLevel] : Descriptor(D3D12_DEFAULT);
}

const Descriptor &DepthStencil::GetStencilSRV() const
{
	return m_stencilSrv;
}

Format DepthStencil::GetDSVFormat() const
{
	return m_dsvFormat;
}

uint32_t DepthStencil::GetArraySize() const
{
	return static_cast<uint32_t>(m_dsvs.size());
}

uint8_t DepthStencil::GetNumMips() const
{
	return static_cast<uint8_t>(m_dsvs.size());
}

bool DepthStencil::create(const Device &device, uint32_t width, uint32_t height, uint32_t arraySize,
	uint8_t numMips, uint8_t sampleCount, Format &format, ResourceFlags resourceFlags, ResourceState state,
	float clearDepth, uint8_t clearStencil, bool &hasSRV, Format &formatStencil, bool isCubeMap,
	const wchar_t *name)
{
	M_RETURN(!device, cerr, "The device is NULL.", false);
	setDevice(device);

	if (name) m_name = name;

	m_dsvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	hasSRV = !(resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);

	// Map formats
	auto formatResource = format;
	auto formatDepth = DXGI_FORMAT_UNKNOWN;
	formatStencil = DXGI_FORMAT_UNKNOWN;

	if (hasSRV)
	{
		switch (format)
		{
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
			formatResource = DXGI_FORMAT_R24G8_TYPELESS;
			formatDepth = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			formatStencil = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
			break;
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			formatResource = DXGI_FORMAT_R32G8X24_TYPELESS;
			formatDepth = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
			formatStencil = DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
			break;
		case DXGI_FORMAT_D16_UNORM:
			formatResource = DXGI_FORMAT_R16_TYPELESS;
			formatDepth = DXGI_FORMAT_R16_UNORM;
			break;
		case DXGI_FORMAT_D32_FLOAT:
			formatResource = DXGI_FORMAT_R32_TYPELESS;
			formatDepth = DXGI_FORMAT_R32_FLOAT;
		default:
			format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			formatResource = DXGI_FORMAT_R24G8_TYPELESS;
			formatDepth = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		}
	}

	m_dsvFormat = format;

	// Setup the render depth stencil description.
	{
		const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(formatResource, width, height, arraySize,
			numMips, sampleCount, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | resourceFlags);

		// Determine initial state
		m_states.resize(arraySize * numMips);
		for (auto &initState : m_states) initState = state ? state : D3D12_RESOURCE_STATE_DEPTH_WRITE;

		// Optimized clear value
		D3D12_CLEAR_VALUE clearValue = { format };
		clearValue.DepthStencil.Depth = clearDepth;
		clearValue.DepthStencil.Stencil = clearStencil;

		// Create the depth stencil texture.
		V_RETURN(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE, &desc, m_states[0], &clearValue, IID_PPV_ARGS(&m_resource)), clog, false);
		if (!m_name.empty()) m_resource->SetName((m_name + L".Resource").c_str());
	}

	// Allocate descriptor pool
	auto numDescriptors = hasSRV ? 1 + (numMips > 1 ? numMips : 0) : 0u;
	numDescriptors += formatStencil ? 1 : 0;				// Stencil SRV
	numDescriptors += hasSRV && numMips > 1 ? numMips : 0;	// Sub SRVs
	N_RETURN(allocateDescriptorPool(numDescriptors), false);

	if (hasSRV)
	{
		// Create SRV
		if (hasSRV) CreateSRVs(arraySize, formatDepth, numMips, sampleCount, isCubeMap);

		// Has stencil
		if (formatStencil)
		{
			// Setup the description of the shader resource view.
			D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
			desc.Format = formatStencil;
			desc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(1, 4, 4, 4);

			if (isCubeMap)
			{
				assert(arraySize % 6 == 0);
				if (arraySize > 6)
				{
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
					desc.TextureCubeArray.MipLevels = numMips;
					desc.TextureCubeArray.NumCubes = arraySize / 6;
				}
				else
				{
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
					desc.TextureCube.MipLevels = numMips;
				}
			}
			else if (arraySize > 1)
			{
				if (sampleCount > 1)
				{
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
					desc.Texture2DMSArray.ArraySize = arraySize;
				}
				else
				{
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
					desc.Texture2DArray.ArraySize = arraySize;
					desc.Texture2DArray.MipLevels = numMips;
				}
			}
			else
			{
				if (sampleCount > 1)
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
				else
				{
					desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					desc.Texture2D.MipLevels = numMips;
				}
			}

			// Create a shader resource view
			m_stencilSrv = m_currentSrvUav;
			m_device->CreateShaderResourceView(m_resource.get(), &desc, m_stencilSrv);
			m_currentSrvUav.Offset(m_srvUavStride);
		}
	}

	return true;
}

bool DepthStencil::allocateDsvPool(uint32_t numDescriptors)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = numDescriptors;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	V_RETURN(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_dsvPool)), cerr, false);
	if (!m_name.empty()) m_dsvPool->SetName((m_name + L".DsvPool").c_str());

	m_currentDsv = m_dsvPool->GetCPUDescriptorHandleForHeapStart();

	return true;
}

//--------------------------------------------------------------------------------------
// 3D Texture
//--------------------------------------------------------------------------------------

Texture3D::Texture3D() :
	ResourceBase(),
	m_counter(nullptr),
	m_uavs(0),
	m_srvLevels(0)
{
}

Texture3D::~Texture3D()
{
}

bool Texture3D::Create(const Device &device, uint32_t width, uint32_t height,
	uint32_t depth, Format format, ResourceFlags resourceFlags, uint8_t numMips,
	PoolType poolType, ResourceState state, const wchar_t *name)
{
	M_RETURN(!device, cerr, "The device is NULL.", false);
	setDevice(device);

	if (name) m_name = name;

	const auto isPacked = (resourceFlags & BIND_PACKED_UAV) == BIND_PACKED_UAV;
	resourceFlags &= REMOVE_PACKED_UAV;

	const auto hasSRV = !(resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	const bool hasUAV = resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	// Map formats
	auto formatResource = format;
	const auto formatUAV = isPacked ? MapToPackedFormat(formatResource) : format;

	// Setup the texture description.
	const auto desc = CD3DX12_RESOURCE_DESC::Tex3D(formatResource, width, height, depth,
		numMips, resourceFlags);

	// Determine initial state
	m_states.resize(numMips);
	if (state) for (auto &initState : m_states) initState = state;
	else for (auto &initState : m_states)
	{
		initState = hasSRV ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON;
		initState = hasUAV ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : initState;
	}
	
	V_RETURN(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(poolType),
		D3D12_HEAP_FLAG_NONE, &desc, m_states[0], nullptr, IID_PPV_ARGS(&m_resource)), clog, false);
	if (!m_name.empty()) m_resource->SetName((m_name + L".Resource").c_str());

	// Allocate descriptor pool
	auto numDescriptors = hasSRV ? (max)(numMips, 1ui8) : 0u;
	numDescriptors += hasUAV ? (max)(numMips, 1ui8) : 0;
	numDescriptors += hasSRV && hasUAV && numMips > 1 ? numMips : 0;
	N_RETURN(allocateDescriptorPool(numDescriptors), false);

	// Create SRV
	if (hasSRV) CreateSRVs(format, numMips);

	// Create UAV
	if (hasUAV) CreateUAVs(formatUAV, numMips);

	// Create SRV for each level
	if (hasSRV && hasUAV) CreateSRVLevels(numMips, format);

	return true;
}

void Texture3D::CreateSRVs(Format format, uint8_t numMips)
{
	// Setup the description of the shader resource view.
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = format ? format : m_resource->GetDesc().Format;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;

	auto mipLevel = 0ui8;
	m_srvs.resize((max)(numMips, 1ui8));

	for (auto &descriptor : m_srvs)
	{
		// Setup the description of the shader resource view.
		desc.Texture3D.MipLevels = numMips - mipLevel;
		desc.Texture3D.MostDetailedMip = mipLevel++;

		// Create a shader resource view
		descriptor = m_currentSrvUav;
		m_device->CreateShaderResourceView(m_resource.get(), &desc, descriptor);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

void Texture3D::CreateSRVLevels(uint8_t numMips, Format format)
{
	if (numMips > 1)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
		desc.Format = format ? format : m_resource->GetDesc().Format;
		desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;

		auto mipLevel = 0ui8;
		m_srvLevels.resize(numMips);

		for (auto &descriptor : m_srvLevels)
		{
			// Setup the description of the shader resource view.
			desc.Texture3D.MostDetailedMip = mipLevel++;
			desc.Texture3D.MipLevels = 1;

			// Create a shader resource view
			descriptor = m_currentSrvUav;
			m_device->CreateShaderResourceView(m_resource.get(), &desc, descriptor);
			m_currentSrvUav.Offset(m_srvUavStride);
		}
	}
}

void Texture3D::CreateUAVs(Format format, uint8_t numMips)
{
	const auto txDesc = m_resource->GetDesc();

	// Setup the description of the unordered access view.
	D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
	desc.Format = format ? format : txDesc.Format;
	desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;

	auto mipLevel = 0ui8;
	m_uavs.resize(numMips);

	for (auto &descriptor : m_uavs)
	{
		// Setup the description of the unordered access view.
		desc.Texture3D.WSize = txDesc.DepthOrArraySize >> mipLevel;
		desc.Texture3D.MipSlice = mipLevel++;

		// Create an unordered access view
		descriptor = m_currentSrvUav;
		m_device->CreateUnorderedAccessView(m_resource.get(), m_counter.get(), &desc, descriptor);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

Descriptor Texture3D::GetUAV(uint8_t i) const
{
	return m_uavs.size() > i ? m_uavs[i] : Descriptor(D3D12_DEFAULT);
}

Descriptor Texture3D::GetSRVLevel(uint8_t i) const
{
	return m_srvLevels.size() > i ? m_srvLevels[i] : Descriptor(D3D12_DEFAULT);
}

//--------------------------------------------------------------------------------------
// Raw buffer
//--------------------------------------------------------------------------------------

RawBuffer::RawBuffer() :
	ResourceBase(),
	m_counter(nullptr),
	m_uavs(0),
	m_srvOffsets(0),
	m_pDataBegin(nullptr)
{
}

RawBuffer::~RawBuffer()
{
	if (m_resource) Unmap();
}

bool RawBuffer::Create(const Device &device, uint32_t byteWidth, ResourceFlags resourceFlags,
	PoolType poolType, ResourceState state, uint32_t numSRVs, const uint32_t *firstSRVElements,
	uint32_t numUAVs, const uint32_t *firstUAVElements, const wchar_t *name)
{
	const auto hasSRV = !(resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	const bool hasUAV = resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	numSRVs = hasSRV ? numSRVs : 0;
	numUAVs = hasUAV ? numUAVs : 0;

	// Create buffer
	N_RETURN(create(device, byteWidth, resourceFlags, poolType, state, numSRVs, numUAVs, name), false);

	// Create SRV
	if (numSRVs > 0) CreateSRVs(byteWidth, firstSRVElements, numSRVs);

	// Create UAV
	if (numUAVs > 0) CreateUAVs(byteWidth, firstUAVElements, numUAVs);

	return true;
}

bool RawBuffer::Upload(const CommandList &commandList, Resource &resourceUpload,
	const void *pData, ResourceState dstState)
{
	const auto desc = m_resource->GetDesc();
	const auto uploadBufferSize = GetRequiredIntermediateSize(m_resource.get(), 0, 1);

	// Create the GPU upload buffer.
	V_RETURN(m_device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resourceUpload)), clog, false);
	if (!m_name.empty()) resourceUpload->SetName((m_name + L".UploaderResource").c_str());

	// Copy data to the intermediate upload heap and then schedule a copy 
	// from the upload heap to the buffer.
	D3D12_SUBRESOURCE_DATA subresourceData;
	subresourceData.pData = pData;
	subresourceData.RowPitch = static_cast<uint32_t>(uploadBufferSize);
	subresourceData.SlicePitch = subresourceData.RowPitch;

	const auto &curState = m_states[0];
	dstState = dstState ? dstState : curState;
	if (curState != D3D12_RESOURCE_STATE_COPY_DEST) Barrier(commandList, D3D12_RESOURCE_STATE_COPY_DEST);
	M_RETURN(UpdateSubresources(const_cast<CommandList&>(commandList).GetCommandList().get(),
		m_resource.get(), resourceUpload.get(), 0, 0, 1, &subresourceData) <= 0, clog,
		"Failed to upload the resource.", false);
	Barrier(commandList, dstState);

	return true;
}

void RawBuffer::CreateSRVs(uint32_t byteWidth, const uint32_t *firstElements,
	uint32_t numDescriptors)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = DXGI_FORMAT_R32_TYPELESS;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

	const uint32_t stride = sizeof(uint32_t);
	const auto numElements = byteWidth / stride;

	m_srvOffsets.resize(numDescriptors);
	m_srvs.resize(numDescriptors);
	for (auto i = 0u; i < numDescriptors; ++i)
	{
		const auto firstElement = firstElements ? firstElements[i] : 0;
		desc.Buffer.FirstElement = firstElement;
		desc.Buffer.NumElements = (!firstElements || i + 1 >= numDescriptors ?
			numElements : firstElements[i + 1]) - firstElement;
		
		m_srvOffsets[i] = stride * firstElement;

		// Create a shader resource view
		m_srvs[i] = m_currentSrvUav;
		m_device->CreateShaderResourceView(m_resource.get(), &desc, m_srvs[i]);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

void RawBuffer::CreateUAVs(uint32_t byteWidth, const uint32_t *firstElements,
	uint32_t numDescriptors)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
	desc.Format = DXGI_FORMAT_R32_TYPELESS;
	desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;

	const uint32_t numElements = byteWidth / sizeof(uint32_t);

	m_uavs.resize(numDescriptors);
	for (auto i = 0u; i < numDescriptors; ++i)
	{
		const auto firstElement = firstElements ? firstElements[i] : 0;
		desc.Buffer.FirstElement = firstElement;
		desc.Buffer.NumElements = (!firstElements || i + 1 >= numDescriptors ?
			numElements : firstElements[i + 1]) - firstElement;

		// Create an unordered access view
		m_uavs[i] = m_currentSrvUav;
		m_device->CreateUnorderedAccessView(m_resource.get(), m_counter.get(), &desc, m_uavs[i]);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

Descriptor RawBuffer::GetUAV(uint32_t i) const
{
	return m_uavs.size() > i ? m_uavs[i] : Descriptor(D3D12_DEFAULT);
}

void *RawBuffer::Map(uint32_t i)
{
	if (m_pDataBegin == nullptr)
	{
		// Map and initialize the buffer.
		CD3DX12_RANGE readRange(0, 0);	// We do not intend to read from this resource on the CPU.
		V_RETURN(m_resource->Map(0, &readRange, &m_pDataBegin), cerr, false);
	}

	return &reinterpret_cast<uint8_t*>(m_pDataBegin)[m_srvOffsets[i]];
}

void RawBuffer::Unmap()
{
	if (m_pDataBegin)
	{
		m_resource->Unmap(0, nullptr);
		m_pDataBegin = nullptr;
	}
}

bool RawBuffer::create(const Device &device, uint32_t byteWidth, ResourceFlags resourceFlags,
	PoolType poolType, ResourceState state, uint32_t numSRVs, uint32_t numUAVs,
	const wchar_t *name)
{
	M_RETURN(!device, cerr, "The device is NULL.", false);
	setDevice(device);

	if (name) m_name = name;

	// Setup the buffer description.
	const auto desc = CD3DX12_RESOURCE_DESC::Buffer(byteWidth, resourceFlags);

	// Determine initial state
	m_states.resize(1);
	auto &initState = m_states[0];
	if (state) initState = state;
	else
	{
		initState = numSRVs > 0 ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON;
		initState = numUAVs > 0 ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : initState;
	}

	V_RETURN(m_device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(poolType),
		D3D12_HEAP_FLAG_NONE, &desc, initState, nullptr, IID_PPV_ARGS(&m_resource)), clog, false);
	if (!m_name.empty()) m_resource->SetName((m_name + L".Resource").c_str());

	// Allocate descriptor pool
	const auto numDescriptors = numSRVs + numUAVs;
	N_RETURN(allocateDescriptorPool(numDescriptors), false);

	return true;
}

//--------------------------------------------------------------------------------------
// Structured buffer
//--------------------------------------------------------------------------------------

StructuredBuffer::StructuredBuffer() :
	RawBuffer()
{
}

StructuredBuffer::~StructuredBuffer()
{
}

bool StructuredBuffer::Create(const Device &device, uint32_t numElements, uint32_t stride,
	ResourceFlags resourceFlags, PoolType poolType, ResourceState state, uint32_t numSRVs,
	const uint32_t *firstSRVElements, uint32_t numUAVs, const uint32_t *firstUAVElements,
	const wchar_t *name)
{
	const auto hasSRV = !(resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	const bool hasUAV = resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	numSRVs = hasSRV ? numSRVs : 0;
	numUAVs = hasUAV ? numUAVs : 0;

	// Create buffer
	N_RETURN(create(device, stride * numElements, resourceFlags,
		poolType, state, numSRVs, numUAVs, name), false);

	// Create SRV
	if (numSRVs > 0) CreateSRVs(numElements, stride, firstSRVElements, numSRVs);

	// Create UAV
	if (numUAVs > 0) CreateUAVs(numElements, stride, firstUAVElements, numUAVs);

	return true;
}

void StructuredBuffer::CreateSRVs(uint32_t numElements, uint32_t stride,
	const uint32_t *firstElements, uint32_t numDescriptors)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = m_resource->GetDesc().Format;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	desc.Buffer.StructureByteStride = stride;

	m_srvOffsets.resize(numDescriptors);
	m_srvs.resize(numDescriptors);
	for (auto i = 0u; i < numDescriptors; ++i)
	{
		const auto firstElement = firstElements ? firstElements[i] : 0;
		desc.Buffer.FirstElement = firstElement;
		desc.Buffer.NumElements = (!firstElements || i + 1 >= numDescriptors ?
			numElements : firstElements[i + 1]) - firstElement;

		m_srvOffsets[i] = stride * firstElement;

		// Create a shader resource view
		m_srvs[i] = m_currentSrvUav;
		m_device->CreateShaderResourceView(m_resource.get(), &desc, m_srvs[i]);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

void StructuredBuffer::CreateUAVs(uint32_t numElements, uint32_t stride,
	const uint32_t *firstElements, uint32_t numDescriptors)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
	desc.Format = m_resource->GetDesc().Format;
	desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	desc.Buffer.StructureByteStride = stride;

	m_uavs.resize(numDescriptors);
	for (auto i = 0u; i < numDescriptors; ++i)
	{
		const auto firstElement = firstElements ? firstElements[i] : 0;
		desc.Buffer.FirstElement = firstElement;
		desc.Buffer.NumElements = (!firstElements || i + 1 >= numDescriptors ?
			numElements : firstElements[i + 1]) - firstElement;

		// Create an unordered access view
		m_uavs[i] = m_currentSrvUav;
		m_device->CreateUnorderedAccessView(m_resource.get(), m_counter.get(), &desc, m_uavs[i]);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

//--------------------------------------------------------------------------------------
// Typed buffer
//--------------------------------------------------------------------------------------

TypedBuffer::TypedBuffer() :
	RawBuffer()
{
}

TypedBuffer::~TypedBuffer()
{
}

bool TypedBuffer::Create(const Device &device, uint32_t numElements, uint32_t stride,
	Format format, ResourceFlags resourceFlags, PoolType poolType, ResourceState state,
	uint32_t numSRVs, const uint32_t *firstSRVElements,
	uint32_t numUAVs, const uint32_t *firstUAVElements,
	const wchar_t *name)
{
	M_RETURN(!device, cerr, "The device is NULL.", false);
	setDevice(device);

	const auto isPacked = (resourceFlags & BIND_PACKED_UAV) == BIND_PACKED_UAV;
	resourceFlags &= REMOVE_PACKED_UAV;

	const auto hasSRV = !(resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	const bool hasUAV = resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	numSRVs = hasSRV ? numSRVs : 0;
	numUAVs = hasUAV ? numUAVs : 0;

	// Map formats
	auto formatResource = format;
	const auto formatUAV = isPacked ? MapToPackedFormat(formatResource) : format;

	// Create buffer
	N_RETURN(create(device, stride * numElements, resourceFlags,
		poolType, state, numSRVs, numUAVs, name), false);

	// Create SRV
	if (numSRVs > 0) CreateSRVs(numElements, format, stride, firstSRVElements, numSRVs);

	// Create UAV
	if (numUAVs > 0) CreateUAVs(numElements, formatUAV, stride, firstUAVElements, numUAVs);

	return true;
}

void TypedBuffer::CreateSRVs(uint32_t numElements, Format format, uint32_t stride,
	const uint32_t *firstElements, uint32_t numDescriptors)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
	desc.Format = format ? format : m_resource->GetDesc().Format;
	desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

	m_srvOffsets.resize(numDescriptors);
	m_srvs.resize(numDescriptors);
	for (auto i = 0u; i < numDescriptors; ++i)
	{
		const auto firstElement = firstElements ? firstElements[i] : 0;
		desc.Buffer.FirstElement = firstElement;
		desc.Buffer.NumElements = (!firstElements || i + 1 >= numDescriptors ?
			numElements : firstElements[i + 1]) - firstElement;

		m_srvOffsets[i] = stride * firstElement;

		// Create a shader resource view
		m_srvs[i] = m_currentSrvUav;
		m_device->CreateShaderResourceView(m_resource.get(), &desc, m_srvs[i]);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

void TypedBuffer::CreateUAVs(uint32_t numElements, Format format, uint32_t stride,
	const uint32_t *firstElements, uint32_t numDescriptors)
{
	D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
	desc.Format = format ? format : m_resource->GetDesc().Format;
	desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

	m_uavs.resize(numDescriptors);
	for (auto i = 0u; i < numDescriptors; ++i)
	{
		const auto firstElement = firstElements ? firstElements[i] : 0;
		desc.Buffer.FirstElement = firstElement;
		desc.Buffer.NumElements = (!firstElements || i + 1 >= numDescriptors ?
			numElements : firstElements[i + 1]) - firstElement;

		// Create an unordered access view
		m_uavs[i] = m_currentSrvUav;
		m_device->CreateUnorderedAccessView(m_resource.get(), m_counter.get(), &desc, m_uavs[i]);
		m_currentSrvUav.Offset(m_srvUavStride);
	}
}

//--------------------------------------------------------------------------------------
// Vertex buffer
//--------------------------------------------------------------------------------------

VertexBuffer::VertexBuffer() :
	StructuredBuffer(),
	m_vbvs(0)
{
}

VertexBuffer::~VertexBuffer()
{
}

bool VertexBuffer::Create(const Device &device, uint32_t numVertices, uint32_t stride,
	ResourceFlags resourceFlags, PoolType poolType, ResourceState state,
	uint32_t numVBVs, const uint32_t *firstVertices,
	uint32_t numSRVs, const uint32_t *firstSRVElements,
	uint32_t numUAVs, const uint32_t *firstUAVElements,
	const wchar_t *name)
{
	const auto hasSRV = !(resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	const bool hasUAV = resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	// Determine initial state
	if (state == 0)
	{
		state = hasSRV ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE :
			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		state = hasUAV ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : state;
	}

	N_RETURN(StructuredBuffer::Create(device, numVertices, stride, resourceFlags, poolType,
		state, numSRVs, firstSRVElements, numUAVs, firstUAVElements, name), false);

	// Create vertex buffer view
	m_vbvs.resize(numVBVs);
	for (auto i = 0u; i < numVBVs; ++i)
	{
		const auto firstVertex = firstVertices ? firstVertices[i] : 0;
		m_vbvs[i].BufferLocation = m_resource->GetGPUVirtualAddress() + stride * firstVertex;
		m_vbvs[i].StrideInBytes = stride;
		m_vbvs[i].SizeInBytes = stride * ((!firstVertices || i + 1 >= numVBVs ?
			numVertices : firstVertices[i + 1]) - firstVertex);
	}

	return true;
}

VertexBufferView VertexBuffer::GetVBV(uint32_t i) const
{
	return m_vbvs.size() > i ? m_vbvs[i] : VertexBufferView();
}

//--------------------------------------------------------------------------------------
// Index buffer
//--------------------------------------------------------------------------------------

IndexBuffer::IndexBuffer() :
	TypedBuffer(),
	m_ibvs(0)
{
}

IndexBuffer::~IndexBuffer()
{
}

bool IndexBuffer::Create(const Device &device, uint32_t byteWidth, Format format,
	ResourceFlags resourceFlags, PoolType poolType, ResourceState state,
	uint32_t numIBVs, const uint32_t *offsets,
	uint32_t numSRVs, const uint32_t *firstSRVElements,
	uint32_t numUAVs, const uint32_t *firstUAVElements,
	const wchar_t *name)
{
	setDevice(device);

	const auto hasSRV = !(resourceFlags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
	const bool hasUAV = resourceFlags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	assert(format == DXGI_FORMAT_R32_UINT || format == DXGI_FORMAT_R16_UINT);
	const uint32_t stride = format == DXGI_FORMAT_R32_UINT ? sizeof(uint32_t) : sizeof(uint16_t);

	// Determine initial state
	state = state ? state : (hasSRV ? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE :
		D3D12_RESOURCE_STATE_INDEX_BUFFER);

	N_RETURN(TypedBuffer::Create(device, byteWidth / stride, stride, format, resourceFlags,
		poolType, state, numSRVs, firstSRVElements, numUAVs, firstUAVElements, name), false);

	// Create index buffer view
	m_ibvs.resize(numIBVs);
	for (auto i = 0u; i < numIBVs; ++i)
	{
		const auto offset = offsets ? offsets[i] : 0;
		m_ibvs[i].BufferLocation = m_resource->GetGPUVirtualAddress() + offset;
		m_ibvs[i].SizeInBytes = (!offsets || i + 1 >= numIBVs ?
			byteWidth : offsets[i + 1]) - offset;
		m_ibvs[i].Format = format;
	}

	return true;
}

IndexBufferView IndexBuffer::GetIBV(uint32_t i) const
{
	return m_ibvs.size() > i ? m_ibvs[i] : IndexBufferView();
}
