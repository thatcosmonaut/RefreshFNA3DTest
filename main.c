#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <Refresh.h>
#include <Refresh_Image.h>

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

int main(int argc, char *argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0)
	{
		fprintf(stderr, "Failed to initialize SDL\n\t%s\n", SDL_GetError());
		return -1;
	}

	const int windowWidth = 1280;
	const int windowHeight = 720;

	SDL_Window *window = SDL_CreateWindow(
		"Refresh Test",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		windowWidth,
		windowHeight,
		SDL_WINDOW_VULKAN
	);

	Refresh_PresentationParameters presentationParameters;
	presentationParameters.deviceWindowHandle = window;
	presentationParameters.presentMode = REFRESH_PRESENTMODE_IMMEDIATE;

	Refresh_Device *device = Refresh_CreateDevice(&presentationParameters, 1);

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
		REFRESH_TEXTUREUSAGE_COLOR_TARGET_BIT
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
	depthStencilClear.stencil = 0.0f;

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

			Refresh_QueuePresent(device, commandBuffer, &mainColorTargetTextureSlice, &flip, REFRESH_FILTER_NEAREST);
			Refresh_Submit(device, 1, &commandBuffer);

			if (screenshotKey == 1)
			{
				Refresh_Image_SavePNG("screenshot.png", windowWidth, windowHeight, screenshotPixels);
			}
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
