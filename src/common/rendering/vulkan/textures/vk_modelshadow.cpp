/*
** vk_modelshadow.cpp
**
** Jupiter3D: render targets for the shadow maps of the models and of the level (see hw_modelshadow.cpp)
*/

#include "vk_modelshadow.h"
#include "zvulkan/vulkanbuilders.h"
#include "vulkan/system/vk_renderdevice.h"
#include "vulkan/system/vk_commandbuffer.h"
#include "vulkan/renderer/vk_renderstate.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "hw_renderstate.h"
#include <vector>
#include <string.h>

VkModelShadow::VkModelShadow(VulkanRenderDevice* fb) : fb(fb)
{
}

VkModelShadow::~VkModelShadow()
{
}

void VkModelShadow::CreateDepthImage(VkTextureImage& image, int width, int height, const char* name)
{
	VkFormat format = fb->GetBuffers()->SceneDepthStencilFormat;
	image.Image = ImageBuilder()
		.Size(width, height)
		.Format(format)
		.Usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName(name)
		.Create(fb->device.get());
	image.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	image.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	image.View = ImageViewBuilder()
		.Image(image.Image.get(), format, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
		.DebugName(name)
		.Create(fb->device.get());
	image.DepthOnlyView = ImageViewBuilder()
		.Image(image.Image.get(), format, VK_IMAGE_ASPECT_DEPTH_BIT)
		.DebugName(name)
		.Create(fb->device.get());
}

void VkModelShadow::CreateModelMap(int index)
{
	Map& m = Models[index];
	CreateDepthImage(m.Depth, TileSize * 3, TileSize * 2, "VkModelShadow.ModelDepth");

	m.Color.Image = ImageBuilder()
		.Size(TileSize * 3, TileSize * 2)
		.Format(VK_FORMAT_R8_UNORM)
		.Usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		.DebugName("VkModelShadow.ModelColor")
		.Create(fb->device.get());
	m.Color.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	m.Color.Layout = VK_IMAGE_LAYOUT_UNDEFINED;
	m.Color.View = ImageViewBuilder()
		.Image(m.Color.Image.get(), VK_FORMAT_R8_UNORM)
		.DebugName("VkModelShadow.ModelColor")
		.Create(fb->device.get());
	m.Created = true;
}

VkTextureImage* VkModelShadow::GetDummy()
{
	if (!Dummy.Image)
		CreateDepthImage(Dummy, 1, 1, "VkModelShadow.Dummy");
	return &Dummy;
}

VkTextureImage* VkModelShadow::GetModelTexture(int index)
{
	if (index < 0 || index >= MaxMaps || !Models[index].Created)
		return GetDummy();
	return &Models[index].Depth;
}

VkTextureImage* VkModelShadow::GetWorldTexture(int index)
{
	if (index < 0 || index >= MaxMaps || !Worlds[index].Created)
		return GetDummy();
	return &Worlds[index].Depth;
}

//==========================================================================
//
// Models: the normal render state draws into the depth atlas
//
//==========================================================================

bool VkModelShadow::Begin(int index)
{
	if (index < 0 || index >= MaxMaps || ModelActive)
		return false;
	if (!Models[index].Created)
		CreateModelMap(index);
	Map& m = Models[index];

	auto rs = fb->GetRenderState();
	rs->PushRenderTarget();

	VkImageTransition()
		.AddImage(&m.Color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true)
		.AddImage(&m.Depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true)
		.Execute(fb->GetCommands()->GetDrawCommands());

	rs->SetRenderTarget(&m.Color, m.Depth.View.get(), TileSize * 3, TileSize * 2, VK_FORMAT_R8_UNORM, VK_SAMPLE_COUNT_1_BIT);
	rs->EnableDrawBuffers(1, false);
	rs->SetViewport(0, 0, TileSize * 3, TileSize * 2);
	rs->SetScissor(0, 0, TileSize * 3, TileSize * 2);
	rs->Clear(CT_Depth | CT_Stencil);
	ModelActive = true;
	return true;
}

void VkModelShadow::Face(int face)
{
	face = face < 0 ? 0 : (face > 5 ? 5 : face);
	int x = (face % 3) * TileSize, y = (face / 3) * TileSize;
	auto rs = fb->GetRenderState();
	rs->SetViewport(x, y, TileSize, TileSize);
	rs->SetScissor(x, y, TileSize, TileSize);
}

void VkModelShadow::End()
{
	if (!ModelActive)
		return;
	auto rs = fb->GetRenderState();
	rs->FlushClearPass();
	rs->PopRenderTarget();
	ModelActive = false;
}

//==========================================================================
//
// Level: a small own pipeline (depth only) draws the level mesh
//
//==========================================================================

bool VkModelShadow::CreateWorldPipeline()
{
	if (WorldPipeline)
		return true;
	if (WorldFailed)
		return false;

	const char* vs =
		"#version 450\n"
		"layout(location = 0) in vec3 aPos;\n"
		"layout(push_constant) uniform PushConstants { mat4 uVP; };\n"
		"void main()\n"
		"{\n"
		"	gl_Position = uVP * vec4(aPos, 1.0);\n"
		"	gl_Position.z = (gl_Position.z + gl_Position.w) / 2.0;\n"	// same depth range as main.vp
		"}\n";
	const char* fs =
		"#version 450\n"
		"void main() { }\n";

	WorldVertexShader = ShaderBuilder()
		.Type(ShaderType::Vertex)
		.AddSource("VkModelShadow.vert", vs)
		.DebugName("VkModelShadow.vert")
		.Create("VkModelShadow.vert", fb->device.get());
	WorldFragmentShader = ShaderBuilder()
		.Type(ShaderType::Fragment)
		.AddSource("VkModelShadow.frag", fs)
		.DebugName("VkModelShadow.frag")
		.Create("VkModelShadow.frag", fb->device.get());

	VkFormat format = fb->GetBuffers()->SceneDepthStencilFormat;
	RenderPassBuilder rp;
	rp.AddDepthStencilAttachment(
		format, VK_SAMPLE_COUNT_1_BIT,
		VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	rp.AddSubpass();
	rp.AddSubpassDepthStencilAttachmentRef(0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	rp.AddExternalSubpassDependency(
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
	rp.DebugName("VkModelShadow.WorldRenderPass");
	WorldRenderPass = rp.Create(fb->device.get());

	WorldPipelineLayout = PipelineLayoutBuilder()
		.AddPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, 16 * sizeof(float))
		.DebugName("VkModelShadow.WorldPipelineLayout")
		.Create(fb->device.get());

	GraphicsPipelineBuilder pb;
	pb.AddVertexShader(WorldVertexShader.get());
	pb.AddFragmentShader(WorldFragmentShader.get());
	pb.AddVertexBufferBinding(0, 3 * sizeof(float));
	pb.AddVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0);
	pb.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
	pb.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR);
	// Note: the actual values are ignored since we use dynamic viewport+scissor states
	pb.Viewport(0.0f, 0.0f, 320.0f, 200.0f);
	pb.Scissor(0, 0, 320, 200);
	pb.Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pb.DepthStencilEnable(true, true, false);
	pb.DepthFunc(VK_COMPARE_OP_LESS);
	pb.Cull(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	pb.RasterizationSamples(VK_SAMPLE_COUNT_1_BIT);
	pb.Layout(WorldPipelineLayout.get());
	pb.RenderPass(WorldRenderPass.get());
	pb.DebugName("VkModelShadow.WorldPipeline");
	WorldPipeline = pb.Create(fb->device.get());
	return WorldPipeline != nullptr;
}

void VkModelShadow::CreateWorldMap(int index)
{
	Map& m = Worlds[index];
	CreateDepthImage(m.Depth, TileSize * 3, TileSize * 2, "VkModelShadow.WorldDepth");
	m.WorldFramebuffer = FramebufferBuilder()
		.RenderPass(WorldRenderPass.get())
		.Size(TileSize * 3, TileSize * 2)
		.AddAttachment(m.Depth.View.get())
		.DebugName("VkModelShadow.WorldFramebuffer")
		.Create(fb->device.get());
	m.Created = true;
}

void VkModelShadow::SetWorld(const float* verts, unsigned nverts, const unsigned int* elems, unsigned nelems, const void* key)
{
	if (key == MeshKey && nverts == MeshVertexCount && nelems == MeshIndexCount && (MeshVertices || nelems == 0 || nverts == 0))
		return;
	MeshKey = key;

	auto deletelist = fb->GetCommands()->DrawDeleteList.get();
	deletelist->Add(std::move(MeshVertices));
	deletelist->Add(std::move(MeshIndices));
	MeshGeneration++;
	MeshVertexCount = nverts;
	MeshIndexCount = nelems;
	if (nverts == 0 || nelems == 0)
		return;

	// render space: x = X, y = Z (up), z = Y
	size_t vsize = (size_t)nverts * 3 * sizeof(float);
	size_t isize = (size_t)nelems * sizeof(unsigned int);
	MeshVertices = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)
		.Size(vsize)
		.DebugName("VkModelShadow.MeshVertices")
		.Create(fb->device.get());
	MeshIndices = BufferBuilder()
		.Usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)
		.Size(isize)
		.DebugName("VkModelShadow.MeshIndices")
		.Create(fb->device.get());

	float* vdata = (float*)MeshVertices->Map(0, vsize);
	for (unsigned i = 0; i < nverts; i++)
	{
		vdata[i * 3 + 0] = verts[i * 3 + 0];
		vdata[i * 3 + 1] = verts[i * 3 + 2];
		vdata[i * 3 + 2] = verts[i * 3 + 1];
	}
	MeshVertices->Unmap();

	void* idata = MeshIndices->Map(0, isize);
	memcpy(idata, elems, isize);
	MeshIndices->Unmap();
}

bool VkModelShadow::RenderWorld(int index, const float* lightPos, float farp, const float* faceVP)
{
	if (index < 0 || index >= MaxMaps)
		return false;
	if (!MeshVertices || !MeshIndices || MeshIndexCount == 0 || !CreateWorldPipeline())
	{
		Cache[index].Valid = false;
		return false;
	}
	WorldCache& wc = Cache[index];
	if (!Worlds[index].Created)
	{
		CreateWorldMap(index);
		wc.Valid = false;
	}
	if (wc.Valid && wc.Generation == MeshGeneration && wc.FarP == farp
		&& wc.Pos[0] == lightPos[0] && wc.Pos[1] == lightPos[1] && wc.Pos[2] == lightPos[2])
		return true;	// the light and the level did not change, the map is still good

	Map& m = Worlds[index];
	auto rs = fb->GetRenderState();
	rs->EndRenderPass();

	auto cmdbuffer = fb->GetCommands()->GetDrawCommands();
	VkImageTransition()
		.AddImage(&m.Depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true)
		.Execute(cmdbuffer);

	RenderPassBegin()
		.RenderPass(WorldRenderPass.get())
		.RenderArea(0, 0, TileSize * 3, TileSize * 2)
		.Framebuffer(m.WorldFramebuffer.get())
		.AddClearDepthStencil(1.0f, 0)
		.Execute(cmdbuffer);

	cmdbuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, WorldPipeline.get());
	VkBuffer vbuf = MeshVertices->buffer;
	VkDeviceSize voffset = 0;
	cmdbuffer->bindVertexBuffers(0, 1, &vbuf, &voffset);
	cmdbuffer->bindIndexBuffer(MeshIndices->buffer, 0, VK_INDEX_TYPE_UINT32);
	for (int face = 0; face < 6; face++)
	{
		VkViewport viewport = {};
		viewport.x = (float)((face % 3) * TileSize);
		viewport.y = (float)((face / 3) * TileSize);
		viewport.width = (float)TileSize;
		viewport.height = (float)TileSize;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor = {};
		scissor.offset.x = (face % 3) * TileSize;
		scissor.offset.y = (face / 3) * TileSize;
		scissor.extent.width = TileSize;
		scissor.extent.height = TileSize;
		cmdbuffer->setViewport(0, 1, &viewport);
		cmdbuffer->setScissor(0, 1, &scissor);
		cmdbuffer->pushConstants(WorldPipelineLayout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, 16 * sizeof(float), faceVP + face * 16);
		cmdbuffer->drawIndexed(MeshIndexCount, 1, 0, 0, 0);
	}
	cmdbuffer->endRenderPass();

	wc.Valid = true;
	wc.Generation = MeshGeneration;
	wc.FarP = farp;
	wc.Pos[0] = lightPos[0]; wc.Pos[1] = lightPos[1]; wc.Pos[2] = lightPos[2];
	return true;
}
