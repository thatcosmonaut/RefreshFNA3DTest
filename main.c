#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <Refresh.h>
#include <Refresh_Image.h>
#include <Refresh_SysRenderer.h>

#include <FNA3D.h>
#include <FNA3D_SysRenderer.h>

#define MOJOSHADER_NO_VERSION_INCLUDE
#define MOJOSHADER_EFFECT_SUPPORT
#include <mojoshader.h>
#include <mojoshader_effects.h>

typedef struct Vertex
{
	float x, y, z;
	float u, v;
} Vertex;

typedef struct RaymarchUniforms
{
	float time, padding;
	float resolutionX, resolutionY;
} RaymarchUniforms;

typedef struct FNAVertex
{
	float x, y;
	float u, v;
	uint32_t color;
} FNAVertex;

int main(int argc, char *argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0)
	{
		fprintf(stderr, "Failed to initialize SDL\n\t%s\n", SDL_GetError());
		return -1;
	}

	const int windowWidth = 1280;
	const int windowHeight = 720;

	SDL_SetHint("FNA3D_FORCE_DRIVER", "Vulkan");

	uint32_t windowFlags = FNA3D_PrepareWindowAttributes();

	SDL_Window *window = SDL_CreateWindow(
		"Refresh Test",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		windowWidth,
		windowHeight,
		windowFlags
	);

	int width, height;
	FNA3D_GetDrawableSize(window, &width, &height);

	FNA3D_PresentationParameters presentationParameters;
	SDL_memset(&presentationParameters, 0, sizeof(presentationParameters));
	presentationParameters.backBufferWidth = width;
	presentationParameters.backBufferHeight = height;
	presentationParameters.deviceWindowHandle = window;

	FNA3D_Device* fnaDevice = FNA3D_CreateDevice(&presentationParameters, 0);

	FNA3D_SysRendererEXT vulkanRenderingContext;
	vulkanRenderingContext.version = 0;
	FNA3D_GetSysRendererEXT(fnaDevice, &vulkanRenderingContext);

	Refresh_Device* device = Refresh_CreateDeviceUsingExternal(
		vulkanRenderingContext.renderer.vulkan.instance,
		vulkanRenderingContext.renderer.vulkan.physicalDevice,
		vulkanRenderingContext.renderer.vulkan.logicalDevice,
		vulkanRenderingContext.renderer.vulkan.queueFamilyIndex,
		1
	);

	bool quit = false;

	double t = 0.0;
	double dt = 0.01;

	uint64_t currentTime = SDL_GetPerformanceCounter();
	double accumulator = 0.0;

	Refresh_Rect renderArea;
	renderArea.x = 0;
	renderArea.y = 0;
	renderArea.w = windowWidth;
	renderArea.h = windowHeight;

	/* Compile shaders */

	SDL_RWops* file = SDL_RWFromFile("passthrough_vert.spv", "rb");
	Sint64 shaderCodeSize = SDL_RWsize(file);
	uint32_t* byteCode = SDL_malloc(sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	Refresh_ShaderModuleCreateInfo passthroughVertexShaderModuleCreateInfo;
	passthroughVertexShaderModuleCreateInfo.byteCode = byteCode;
	passthroughVertexShaderModuleCreateInfo.codeSize = shaderCodeSize;

	Refresh_ShaderModule* passthroughVertexShaderModule = Refresh_CreateShaderModule(device, &passthroughVertexShaderModuleCreateInfo);

	file = SDL_RWFromFile("hexagon_grid.spv", "rb");
	shaderCodeSize = SDL_RWsize(file);
	byteCode = SDL_realloc(byteCode, sizeof(uint32_t) * shaderCodeSize);
	SDL_RWread(file, byteCode, sizeof(uint32_t), shaderCodeSize);
	SDL_RWclose(file);

	Refresh_ShaderModuleCreateInfo raymarchFragmentShaderModuleCreateInfo;
	raymarchFragmentShaderModuleCreateInfo.byteCode = byteCode;
	raymarchFragmentShaderModuleCreateInfo.codeSize = shaderCodeSize;

	Refresh_ShaderModule* raymarchFragmentShaderModule = Refresh_CreateShaderModule(device, &raymarchFragmentShaderModuleCreateInfo);

	SDL_free(byteCode);

	/* Load textures */

	int32_t textureWidth, textureHeight, numChannels;
	uint8_t *woodTexturePixels = Refresh_Image_Load(
		"woodgrain.png",
		&textureWidth,
		&textureHeight,
		&numChannels
	);

	Refresh_Texture *woodTexture = Refresh_CreateTexture2D(
		device,
		REFRESH_COLORFORMAT_R8G8B8A8,
		textureWidth,
		textureHeight,
		1,
		REFRESH_TEXTUREUSAGE_SAMPLER_BIT
	);

	Refresh_TextureSlice setTextureDataSlice;
	setTextureDataSlice.texture = woodTexture;
	setTextureDataSlice.rectangle.x = 0;
	setTextureDataSlice.rectangle.y = 0;
	setTextureDataSlice.rectangle.w = textureWidth;
	setTextureDataSlice.rectangle.h = textureHeight;
	setTextureDataSlice.depth = 0;
	setTextureDataSlice.layer = 0;
	setTextureDataSlice.level = 0;

	Refresh_SetTextureData(
		device,
		&setTextureDataSlice,
		woodTexturePixels,
		textureWidth * textureHeight * 4
	);

	Refresh_Image_Free(woodTexturePixels);

	uint8_t *noiseTexturePixels = Refresh_Image_Load(
		"noise.png",
		&textureWidth,
		&textureHeight,
		&numChannels
	);

	Refresh_Texture *noiseTexture = Refresh_CreateTexture2D(
		device,
		REFRESH_COLORFORMAT_R8G8B8A8,
		textureWidth,
		textureHeight,
		1,
		REFRESH_TEXTUREUSAGE_SAMPLER_BIT
	);

	setTextureDataSlice.texture = noiseTexture;
	setTextureDataSlice.rectangle.w = textureWidth;
	setTextureDataSlice.rectangle.h = textureHeight;

	Refresh_SetTextureData(
		device,
		&setTextureDataSlice,
		noiseTexturePixels,
		textureWidth * textureHeight * 4
	);

	Refresh_Image_Free(noiseTexturePixels);

	/* Define vertex buffer */

	Vertex* vertices = SDL_malloc(sizeof(Vertex) * 3);
	vertices[0].x = -1;
	vertices[0].y = -1;
	vertices[0].z = 0;
	vertices[0].u = 0;
	vertices[0].v = 1;

	vertices[1].x = 3;
	vertices[1].y = -1;
	vertices[1].z = 0;
	vertices[1].u = 1;
	vertices[1].v = 1;

	vertices[2].x = -1;
	vertices[2].y = 3;
	vertices[2].z = 0;
	vertices[2].u = 0;
	vertices[2].v = 0;

	Refresh_Buffer* vertexBuffer = Refresh_CreateBuffer(device, REFRESH_BUFFERUSAGE_VERTEX_BIT, sizeof(Vertex) * 3);
	Refresh_SetBufferData(device, vertexBuffer, 0, vertices, sizeof(Vertex) * 3);

	uint64_t* offsets = SDL_malloc(sizeof(uint64_t));
	offsets[0] = 0;

	/* Uniforms struct */

	RaymarchUniforms raymarchUniforms;
	raymarchUniforms.time = 0;
	raymarchUniforms.padding = 0;
	raymarchUniforms.resolutionX = (float)windowWidth;
	raymarchUniforms.resolutionY = (float)windowHeight;

	/* Define RenderPass */

	Refresh_ColorTargetDescription mainColorTargetDescription;
	mainColorTargetDescription.format = REFRESH_COLORFORMAT_R8G8B8A8;
	mainColorTargetDescription.loadOp = REFRESH_LOADOP_CLEAR;
	mainColorTargetDescription.storeOp = REFRESH_STOREOP_STORE;
	mainColorTargetDescription.multisampleCount = REFRESH_SAMPLECOUNT_1;

	Refresh_DepthStencilTargetDescription mainDepthStencilTargetDescription;
	mainDepthStencilTargetDescription.depthFormat = REFRESH_DEPTHFORMAT_D32_SFLOAT_S8_UINT;
	mainDepthStencilTargetDescription.loadOp = REFRESH_LOADOP_CLEAR;
	mainDepthStencilTargetDescription.storeOp = REFRESH_STOREOP_DONT_CARE;
	mainDepthStencilTargetDescription.stencilLoadOp = REFRESH_LOADOP_DONT_CARE;
	mainDepthStencilTargetDescription.stencilStoreOp = REFRESH_STOREOP_DONT_CARE;

	Refresh_RenderPassCreateInfo mainRenderPassCreateInfo;
	mainRenderPassCreateInfo.colorTargetCount = 1;
	mainRenderPassCreateInfo.colorTargetDescriptions = &mainColorTargetDescription;
	mainRenderPassCreateInfo.depthTargetDescription = &mainDepthStencilTargetDescription;

	Refresh_RenderPass *mainRenderPass = Refresh_CreateRenderPass(device, &mainRenderPassCreateInfo);

	/* Define ColorTarget */

	Refresh_Texture *mainColorTargetTexture = Refresh_CreateTexture2D(
		device,
		REFRESH_COLORFORMAT_R8G8B8A8,
		windowWidth,
		windowHeight,
		1,
		REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT | REFRESH_TEXTUREUSAGE_SAMPLER_BIT
	);

	Refresh_TextureSlice mainColorTargetTextureSlice;
	mainColorTargetTextureSlice.texture = mainColorTargetTexture;
	mainColorTargetTextureSlice.rectangle.x = 0;
	mainColorTargetTextureSlice.rectangle.y = 0;
	mainColorTargetTextureSlice.rectangle.w = windowWidth;
	mainColorTargetTextureSlice.rectangle.h = windowHeight;
	mainColorTargetTextureSlice.depth = 0;
	mainColorTargetTextureSlice.layer = 0;
	mainColorTargetTextureSlice.level = 0;

	Refresh_ColorTarget *mainColorTarget = Refresh_CreateColorTarget(
		device,
		REFRESH_SAMPLECOUNT_1,
		&mainColorTargetTextureSlice
	);

	Refresh_DepthStencilTarget *mainDepthStencilTarget = Refresh_CreateDepthStencilTarget(
		device,
		windowWidth,
		windowHeight,
		REFRESH_DEPTHFORMAT_D32_SFLOAT_S8_UINT
	);

	/* Define Framebuffer */

	Refresh_FramebufferCreateInfo framebufferCreateInfo;
	framebufferCreateInfo.width = 1280;
	framebufferCreateInfo.height = 720;
	framebufferCreateInfo.colorTargetCount = 1;
	framebufferCreateInfo.pColorTargets = &mainColorTarget;
	framebufferCreateInfo.pDepthStencilTarget = mainDepthStencilTarget;
	framebufferCreateInfo.renderPass = mainRenderPass;

	Refresh_Framebuffer *mainFramebuffer = Refresh_CreateFramebuffer(device, &framebufferCreateInfo);

	/* Define pipeline */
	Refresh_ColorTargetBlendState renderTargetBlendState;
	renderTargetBlendState.blendEnable = 0;
	renderTargetBlendState.alphaBlendOp = 0;
	renderTargetBlendState.colorBlendOp = 0;
	renderTargetBlendState.colorWriteMask =
		REFRESH_COLORCOMPONENT_R_BIT |
		REFRESH_COLORCOMPONENT_G_BIT |
		REFRESH_COLORCOMPONENT_B_BIT |
		REFRESH_COLORCOMPONENT_A_BIT;
	renderTargetBlendState.dstAlphaBlendFactor = 0;
	renderTargetBlendState.dstColorBlendFactor = 0;
	renderTargetBlendState.srcAlphaBlendFactor = 0;
	renderTargetBlendState.srcColorBlendFactor = 0;

	Refresh_ColorBlendState colorBlendState;
	colorBlendState.logicOpEnable = 0;
	colorBlendState.logicOp = REFRESH_LOGICOP_NO_OP;
	colorBlendState.blendConstants[0] = 0.0f;
	colorBlendState.blendConstants[1] = 0.0f;
	colorBlendState.blendConstants[2] = 0.0f;
	colorBlendState.blendConstants[3] = 0.0f;
	colorBlendState.blendStateCount = 1;
	colorBlendState.blendStates = &renderTargetBlendState;

	Refresh_DepthStencilState depthStencilState;
	depthStencilState.depthTestEnable = 0;
	depthStencilState.backStencilState.compareMask = 0;
	depthStencilState.backStencilState.compareOp = REFRESH_COMPAREOP_NEVER;
	depthStencilState.backStencilState.depthFailOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.backStencilState.failOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.backStencilState.passOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.backStencilState.reference = 0;
	depthStencilState.backStencilState.writeMask = 0;
	depthStencilState.compareOp = REFRESH_COMPAREOP_NEVER;
	depthStencilState.depthBoundsTestEnable = 0;
	depthStencilState.depthWriteEnable = 0;
	depthStencilState.frontStencilState.compareMask = 0;
	depthStencilState.frontStencilState.compareOp = REFRESH_COMPAREOP_NEVER;
	depthStencilState.frontStencilState.depthFailOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.frontStencilState.failOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.frontStencilState.passOp = REFRESH_STENCILOP_ZERO;
	depthStencilState.frontStencilState.reference = 0;
	depthStencilState.frontStencilState.writeMask = 0;
	depthStencilState.maxDepthBounds = 1.0f;
	depthStencilState.minDepthBounds = 0.0f;
	depthStencilState.stencilTestEnable = 0;

	Refresh_ShaderStageState vertexShaderStageState;
	vertexShaderStageState.shaderModule = passthroughVertexShaderModule;
	vertexShaderStageState.entryPointName = "main";
	vertexShaderStageState.uniformBufferSize = 0;

	Refresh_ShaderStageState fragmentShaderStageState;
	fragmentShaderStageState.shaderModule = raymarchFragmentShaderModule;
	fragmentShaderStageState.entryPointName = "main";
	fragmentShaderStageState.uniformBufferSize = sizeof(RaymarchUniforms);

	Refresh_MultisampleState multisampleState;
	multisampleState.multisampleCount = REFRESH_SAMPLECOUNT_1;
	multisampleState.sampleMask = -1;

	Refresh_GraphicsPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
	pipelineLayoutCreateInfo.vertexSamplerBindingCount = 0;
	pipelineLayoutCreateInfo.fragmentSamplerBindingCount = 2;

	Refresh_RasterizerState rasterizerState;
	rasterizerState.cullMode = REFRESH_CULLMODE_BACK;
	rasterizerState.depthBiasClamp = 0;
	rasterizerState.depthBiasConstantFactor = 0;
	rasterizerState.depthBiasEnable = 0;
	rasterizerState.depthBiasSlopeFactor = 0;
	rasterizerState.depthClampEnable = 0;
	rasterizerState.fillMode = REFRESH_FILLMODE_FILL;
	rasterizerState.frontFace = REFRESH_FRONTFACE_CLOCKWISE;
	rasterizerState.lineWidth = 1.0f;

	Refresh_TopologyState topologyState;
	topologyState.topology = REFRESH_PRIMITIVETYPE_TRIANGLELIST;

	Refresh_VertexBinding vertexBinding;
	vertexBinding.binding = 0;
	vertexBinding.inputRate = REFRESH_VERTEXINPUTRATE_VERTEX;
	vertexBinding.stride = sizeof(Vertex);

	Refresh_VertexAttribute *vertexAttributes = SDL_stack_alloc(Refresh_VertexAttribute, 2);
	vertexAttributes[0].binding = 0;
	vertexAttributes[0].location = 0;
	vertexAttributes[0].format = REFRESH_VERTEXELEMENTFORMAT_VECTOR3;
	vertexAttributes[0].offset = 0;

	vertexAttributes[1].binding = 0;
	vertexAttributes[1].location = 1;
	vertexAttributes[1].format = REFRESH_VERTEXELEMENTFORMAT_VECTOR2;
	vertexAttributes[1].offset = sizeof(float) * 3;

	Refresh_VertexInputState vertexInputState;
	vertexInputState.vertexBindings = &vertexBinding;
	vertexInputState.vertexBindingCount = 1;
	vertexInputState.vertexAttributes = vertexAttributes;
	vertexInputState.vertexAttributeCount = 2;

	Refresh_Viewport viewport;
	viewport.x = 0;
	viewport.y = 0;
	viewport.w = (float)windowWidth;
	viewport.h = (float)windowHeight;
	viewport.minDepth = 0;
	viewport.maxDepth = 1;

	Refresh_ViewportState viewportState;
	viewportState.viewports = &viewport;
	viewportState.viewportCount = 1;
	viewportState.scissors = &renderArea;
	viewportState.scissorCount = 1;

	Refresh_GraphicsPipelineCreateInfo raymarchPipelineCreateInfo;
	raymarchPipelineCreateInfo.colorBlendState = colorBlendState;
	raymarchPipelineCreateInfo.depthStencilState = depthStencilState;
	raymarchPipelineCreateInfo.vertexShaderState = vertexShaderStageState;
	raymarchPipelineCreateInfo.fragmentShaderState = fragmentShaderStageState;
	raymarchPipelineCreateInfo.multisampleState = multisampleState;
	raymarchPipelineCreateInfo.pipelineLayoutCreateInfo = pipelineLayoutCreateInfo;
	raymarchPipelineCreateInfo.rasterizerState = rasterizerState;
	raymarchPipelineCreateInfo.topologyState = topologyState;
	raymarchPipelineCreateInfo.vertexInputState = vertexInputState;
	raymarchPipelineCreateInfo.viewportState = viewportState;
	raymarchPipelineCreateInfo.renderPass = mainRenderPass;

	Refresh_GraphicsPipeline* raymarchPipeline = Refresh_CreateGraphicsPipeline(device, &raymarchPipelineCreateInfo);

	Refresh_Color clearColor;
	clearColor.r = 100;
	clearColor.g = 149;
	clearColor.b = 237;
	clearColor.a = 255;

	Refresh_DepthStencilValue depthStencilClear;
	depthStencilClear.depth = 1.0f;
	depthStencilClear.stencil = 0;

	/* Sampling */

	Refresh_SamplerStateCreateInfo samplerStateCreateInfo;
	samplerStateCreateInfo.addressModeU = REFRESH_SAMPLERADDRESSMODE_REPEAT;
	samplerStateCreateInfo.addressModeV = REFRESH_SAMPLERADDRESSMODE_REPEAT;
	samplerStateCreateInfo.addressModeW = REFRESH_SAMPLERADDRESSMODE_REPEAT;
	samplerStateCreateInfo.anisotropyEnable = 0;
	samplerStateCreateInfo.borderColor = REFRESH_BORDERCOLOR_FLOAT_OPAQUE_BLACK;
	samplerStateCreateInfo.compareEnable = 0;
	samplerStateCreateInfo.compareOp = REFRESH_COMPAREOP_NEVER;
	samplerStateCreateInfo.magFilter = REFRESH_FILTER_LINEAR;
	samplerStateCreateInfo.maxAnisotropy = 0;
	samplerStateCreateInfo.maxLod = 1;
	samplerStateCreateInfo.minFilter = REFRESH_FILTER_LINEAR;
	samplerStateCreateInfo.minLod = 1;
	samplerStateCreateInfo.mipLodBias = 1;
	samplerStateCreateInfo.mipmapMode = REFRESH_SAMPLERMIPMAPMODE_LINEAR;

	Refresh_Sampler *sampler = Refresh_CreateSampler(
		device,
		&samplerStateCreateInfo
	);

	Refresh_Texture* sampleTextures[2];
	sampleTextures[0] = woodTexture;
	sampleTextures[1] = noiseTexture;

	Refresh_Sampler* sampleSamplers[2];
	sampleSamplers[0] = sampler;
	sampleSamplers[1] = sampler;

	Refresh_Rect flip;
	flip.x = 0;
	flip.y = windowHeight;
	flip.w = windowWidth;
	flip.h = -windowHeight;

	uint8_t screenshotKey = 0;
	uint8_t *screenshotPixels = SDL_malloc(sizeof(uint8_t) * windowWidth * windowHeight * 4);
	Refresh_Buffer *screenshotBuffer = Refresh_CreateBuffer(device, 0, windowWidth * windowHeight * 4);

	/* FNA3D states */

	FNA3D_Viewport fnaViewport;
	fnaViewport.x = 0;
	fnaViewport.y = 0;
	fnaViewport.w = width;
	fnaViewport.h = height;
	fnaViewport.minDepth = 0;
	fnaViewport.maxDepth = 1;
	FNA3D_SetViewport(fnaDevice, &fnaViewport);

	FNA3D_BlendState fnaBlendState;
	fnaBlendState.alphaBlendFunction = FNA3D_BLENDFUNCTION_ADD;
	fnaBlendState.alphaDestinationBlend = FNA3D_BLEND_INVERSESOURCEALPHA;
	fnaBlendState.alphaSourceBlend = FNA3D_BLEND_ONE;
	FNA3D_Color blendFactor = { 0xff, 0xff, 0xff, 0xff };
	fnaBlendState.blendFactor = blendFactor;
	fnaBlendState.colorBlendFunction = FNA3D_BLENDFUNCTION_ADD;
	fnaBlendState.colorDestinationBlend = FNA3D_BLEND_INVERSESOURCEALPHA;
	fnaBlendState.colorSourceBlend = FNA3D_BLEND_ONE;
	fnaBlendState.colorWriteEnable = FNA3D_COLORWRITECHANNELS_ALL;
	fnaBlendState.colorWriteEnable1 = FNA3D_COLORWRITECHANNELS_ALL;
	fnaBlendState.colorWriteEnable2 = FNA3D_COLORWRITECHANNELS_ALL;
	fnaBlendState.colorWriteEnable3 = FNA3D_COLORWRITECHANNELS_ALL;
	fnaBlendState.multiSampleMask = -1;
	FNA3D_SetBlendState(fnaDevice, &fnaBlendState);

	FNA3D_DepthStencilState fnaDepthStencilState;
	fnaDepthStencilState.ccwStencilDepthBufferFail = 0;
	fnaDepthStencilState.ccwStencilFail = 0;
	fnaDepthStencilState.ccwStencilFunction = 0;
	fnaDepthStencilState.ccwStencilPass = 0;
	fnaDepthStencilState.referenceStencil = 0;
	fnaDepthStencilState.depthBufferEnable = 0;
	fnaDepthStencilState.depthBufferFunction = 0;
	fnaDepthStencilState.depthBufferWriteEnable = 0;
	fnaDepthStencilState.stencilDepthBufferFail = 0;
	fnaDepthStencilState.stencilEnable = 0;
	fnaDepthStencilState.stencilFail = 0;
	fnaDepthStencilState.stencilFunction = 0;
	fnaDepthStencilState.stencilMask = 0;
	fnaDepthStencilState.stencilPass = 0;
	fnaDepthStencilState.stencilWriteMask = 0;
	fnaDepthStencilState.twoSidedStencilMode = 0;
	FNA3D_SetDepthStencilState(fnaDevice, &fnaDepthStencilState);

	FNA3D_RasterizerState fnaRasterizerState;
	fnaRasterizerState.cullMode = FNA3D_CULLMODE_NONE;
	fnaRasterizerState.fillMode = FNA3D_FILLMODE_SOLID;
	fnaRasterizerState.depthBias = 0;
	fnaRasterizerState.multiSampleAntiAlias = 1;
	fnaRasterizerState.scissorTestEnable = 0;
	fnaRasterizerState.slopeScaleDepthBias = 0;
	FNA3D_ApplyRasterizerState(fnaDevice, &fnaRasterizerState);

	/* load effect */
	FNA3D_Effect* effect = NULL;
	MOJOSHADER_effect* effectData = NULL;

	/* FIXME: use SDL */
	FILE* effectFile = fopen("SpriteEffect.fxb", "rb");
	fseek(effectFile, 0, SEEK_END);
	uint32_t effectCodeLength = ftell(effectFile);
	fseek(effectFile, 0, SEEK_SET);
	uint8_t* effectCode = malloc(effectCodeLength);
	fread(effectCode, 1, effectCodeLength, effectFile);
	fclose(effectFile);
	FNA3D_CreateEffect(fnaDevice, effectCode, effectCodeLength, &effect, &effectData);
	free(effectCode);

	/* create external texture*/

	Refresh_TextureHandlesEXT textureHandles;
	Refresh_GetTextureHandlesEXT(device, mainColorTargetTexture, &textureHandles);

	FNA3D_SysTextureEXT externalTextureCreateInfo;
	externalTextureCreateInfo.rendererType = FNA3D_RENDERER_TYPE_VULKAN_EXT;
	externalTextureCreateInfo.texture.vulkan.image = textureHandles.texture.vulkan.image;
	externalTextureCreateInfo.texture.vulkan.view = textureHandles.texture.vulkan.view;
	externalTextureCreateInfo.version = 0;

	FNA3D_Texture* externalTexture = FNA3D_CreateSysTextureEXT(fnaDevice, &externalTextureCreateInfo);

	/* create FNA vertices */

	FNA3D_VertexElement vertexElements[3];
	vertexElements[0].offset = 0;
	vertexElements[0].usageIndex = 0;
	vertexElements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
	vertexElements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;

	vertexElements[1].offset = sizeof(float) * 2;
	vertexElements[1].usageIndex = 0;
	vertexElements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
	vertexElements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE;

	vertexElements[2].offset = sizeof(float) * 4;
	vertexElements[2].usageIndex = 0;
	vertexElements[2].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_COLOR;
	vertexElements[2].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_COLOR;

	FNA3D_VertexDeclaration vertexDeclaration;
	vertexDeclaration.elementCount = 3;
	vertexDeclaration.vertexStride = sizeof(Vertex);
	vertexDeclaration.elements = vertexElements;

	FNAVertex fnaVertices[6] =
	{
		{ 50, 50, 0, 0, 0xffff0000 },
		{ 150, 50, 1, 0, 0xff0000ff },
		{ 150, 150, 1, 1, 0xff00ffff },
		{ 150, 150, 1, 1, 0xff00ffff },
		{ 50, 150, 0, 1, 0xff00ff00 },
		{ 50, 50, 0, 0, 0xffff0000 },
	};

	// vertex buffer
	FNA3D_Buffer* fnaVertexBuffer = FNA3D_GenVertexBuffer(fnaDevice, 0, FNA3D_BUFFERUSAGE_WRITEONLY, sizeof(FNAVertex) * 6);
	FNA3D_SetVertexBufferData(fnaDevice, fnaVertexBuffer, 0, fnaVertices, sizeof(FNAVertex) * 6, 1, 1, FNA3D_SETDATAOPTIONS_NONE);

	FNA3D_VertexBufferBinding vertexBufferBinding;
	vertexBufferBinding.instanceFrequency = 0;
	vertexBufferBinding.vertexBuffer = fnaVertexBuffer;
	vertexBufferBinding.vertexDeclaration = vertexDeclaration;
	vertexBufferBinding.vertexOffset = 0;

	while (!quit)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_QUIT:
				quit = true;
				break;
			}
		}

		uint64_t newTime = SDL_GetPerformanceCounter();
		double frameTime = (newTime - currentTime) / (double)SDL_GetPerformanceFrequency();

		if (frameTime > 0.25)
			frameTime = 0.25;
		currentTime = newTime;

		accumulator += frameTime;

		bool updateThisLoop = (accumulator >= dt);

		while (accumulator >= dt && !quit)
		{
			// Update here!

			t += dt;
			accumulator -= dt;

			const uint8_t *keyboardState = SDL_GetKeyboardState(NULL);

			if (keyboardState[SDL_SCANCODE_S])
			{
				if (screenshotKey == 1)
				{
					screenshotKey = 2;
				}
				else
				{
					screenshotKey = 1;
				}
			}
			else
			{
				screenshotKey = 0;
			}
		}

		if (updateThisLoop && !quit)
		{
			// Draw here!

			Refresh_CommandBuffer *commandBuffer = Refresh_AcquireCommandBuffer(device, 0);

			Refresh_BeginRenderPass(
				device,
				commandBuffer,
				mainRenderPass,
				mainFramebuffer,
				renderArea,
				&clearColor,
				1,
				&depthStencilClear
			);

			Refresh_BindGraphicsPipeline(
				device,
				commandBuffer,
				raymarchPipeline
			);

			raymarchUniforms.time = (float)t;

			uint32_t fragmentParamOffset = Refresh_PushFragmentShaderParams(device, commandBuffer, &raymarchUniforms, 1);
			Refresh_BindVertexBuffers(device, commandBuffer, 0, 1, &vertexBuffer, offsets);
			Refresh_BindFragmentSamplers(device, commandBuffer, sampleTextures, sampleSamplers);
			Refresh_DrawPrimitives(device, commandBuffer, 0, 1, 0, fragmentParamOffset);

			Refresh_Clear(device, commandBuffer, &renderArea, REFRESH_CLEAROPTIONS_DEPTH | REFRESH_CLEAROPTIONS_STENCIL, NULL, 0, 0.5f, 10);
			Refresh_EndRenderPass(device, commandBuffer);

			if (screenshotKey == 1)
			{
				SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "screenshot!");
				Refresh_CopyTextureToBuffer(device, commandBuffer, &mainColorTargetTextureSlice, screenshotBuffer);
			}

			Refresh_Submit(device, 1, &commandBuffer);

			if (screenshotKey == 1)
			{
				Refresh_Image_SavePNG("screenshot.png", windowWidth, windowHeight, screenshotPixels);
			}

			MOJOSHADER_effectStateChanges stateChanges;
			memset(&stateChanges, 0, sizeof(stateChanges));
			FNA3D_ApplyEffect(fnaDevice, effect, 0, &stateChanges);

			FNA3D_SamplerState samplerState;
			memset(&samplerState, 0, sizeof(samplerState));
			samplerState.addressU = FNA3D_TEXTUREADDRESSMODE_CLAMP;
			samplerState.addressV = FNA3D_TEXTUREADDRESSMODE_CLAMP;
			samplerState.addressW = FNA3D_TEXTUREADDRESSMODE_WRAP;
			samplerState.filter = FNA3D_TEXTUREFILTER_LINEAR;
			samplerState.maxAnisotropy = 4;
			samplerState.maxMipLevel = 0;
			samplerState.mipMapLevelOfDetailBias = 0;
			FNA3D_VerifySampler(fnaDevice, 0, externalTexture, &samplerState);

			for (int i = 0; i < effectData->param_count; i++)
			{
				if (SDL_strcmp("MatrixTransform", effectData->params[i].value.name) == 0)
				{
					// OrthographicOffCenter Matrix - value copied from XNA project
					// todo: Do I need to worry about row-major/column-major?
					float projectionMatrix[16] =
					{
						0.0015625f,
						0,
						0,
						-1,
						0,
						-0.00277777785f,
						0,
						1,
						0,
						0,
						1,
						0,
						0,
						0,
						0,
						1
					};
					SDL_memcpy(effectData->params[i].value.values, projectionMatrix, sizeof(float) * 16);
					break;
				}
			}

			FNA3D_ApplyVertexBufferBindings(fnaDevice, &vertexBufferBinding, 1, 0, 0);
			FNA3D_DrawPrimitives(fnaDevice, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, 2);

			FNA3D_SwapBuffers(fnaDevice, NULL, NULL, window);
		}
	}

	SDL_free(screenshotPixels);

	Refresh_QueueDestroyColorTarget(device, mainColorTarget);
	Refresh_QueueDestroyDepthStencilTarget(device, mainDepthStencilTarget);

	Refresh_QueueDestroyTexture(device, woodTexture);
	Refresh_QueueDestroyTexture(device, noiseTexture);
	Refresh_QueueDestroyTexture(device, mainColorTargetTexture);
	Refresh_QueueDestroySampler(device, sampler);

	Refresh_QueueDestroyBuffer(device, vertexBuffer);
	Refresh_QueueDestroyBuffer(device, screenshotBuffer);

	Refresh_QueueDestroyGraphicsPipeline(device, raymarchPipeline);

	Refresh_QueueDestroyShaderModule(device, passthroughVertexShaderModule);
	Refresh_QueueDestroyShaderModule(device, raymarchFragmentShaderModule);

	Refresh_QueueDestroyFramebuffer(device, mainFramebuffer);
	Refresh_QueueDestroyRenderPass(device, mainRenderPass);

	Refresh_DestroyDevice(device);

	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
