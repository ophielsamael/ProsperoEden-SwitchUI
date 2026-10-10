    const auto hud = draw_hud ? Eden::GetVulkanHud() : Eden::HudSnapshot{};
    if (hud.width && !hud_pipeline) {
        const VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(hud)};
        hud_layout = device.GetLogical().CreatePipelineLayout(VkPipelineLayoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        });
        auto vertex = BuildShader(device, EDEN_HUD_VERT_SPV);
        auto fragment = BuildShader(device, EDEN_HUD_FRAG_SPV);
        hud_pipeline = CreateWrappedCoverageBlendingPipeline(
            device, render_pass, hud_layout, std::tie(vertex, fragment));
    }
    const VkPipeline hud_graphics_pipeline = *hud_pipeline;
    const VkPipelineLayout hud_pipeline_layout = *hud_layout;
    // The loading screen with its picture (vulkan_loading.inc): a pipeline of its own, whose set 0
    // holds the picture. Without a picture the HUD pipeline draws it, on its dark backdrop.
    const auto loading_set = reinterpret_cast<VkDescriptorSet>(::Eden::Loading::vulkan_picture_set);
    if (hud.width && hud.loading && loading_set != VK_NULL_HANDLE && !loading_pipeline) {
        const auto picture_layout = reinterpret_cast<VkDescriptorSetLayout>(::Eden::Loading::vulkan_picture_layout);
        const VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(hud)};
        loading_layout = device.GetLogical().CreatePipelineLayout(VkPipelineLayoutCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &picture_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        });
        auto vertex = BuildShader(device, EDEN_HUD_VERT_SPV);
        auto fragment = BuildShader(device, EDEN_LOADING_FRAG_SPV);
        loading_pipeline = CreateWrappedCoverageBlendingPipeline(
            device, render_pass, loading_layout, std::tie(vertex, fragment));
    }
    const VkPipeline loading_graphics_pipeline = loading_set != VK_NULL_HANDLE ? *loading_pipeline : VK_NULL_HANDLE;
    const VkPipelineLayout loading_pipeline_layout = *loading_layout;
