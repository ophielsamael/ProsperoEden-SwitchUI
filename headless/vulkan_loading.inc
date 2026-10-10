// Runs on the existing GPU worker, before the first guest Composite. Reuse its
// swapchain and composition pass without guest captures or FPS notifications.
void RendererVulkan::PresentLoading() {
    if (!std::exchange(loading_picture_tried, true)) {
        // The picture behind the loading screen (hud.h, Loading::ReadPicture): uploaded once and
        // drawn by the loading pipeline (vulkan_hud_prepare.inc), whose set 0 holds it. Whatever
        // fails here leaves the screen as it was, with its own dark backdrop.
        const std::vector<uint8_t> pixels = ::Eden::Loading::ReadPicture();
        if (!pixels.empty()) {
            try {
                constexpr VkExtent2D extent{::Eden::Loading::kPictureWidth, ::Eden::Loading::kPictureHeight};
                constexpr VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
                loading_image = CreateWrappedImage(memory_allocator, extent, format);
                UploadImage(device, memory_allocator, scheduler, loading_image, extent, format, pixels);
                loading_view = CreateWrappedImageView(device, loading_image, format);
                loading_sampler = CreateBilinearSampler(device);
                loading_pool = CreateWrappedDescriptorPool(device, 1, 1);
                loading_set_layout = CreateWrappedDescriptorSetLayout(
                    device, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER}, VK_SHADER_STAGE_FRAGMENT_BIT);
                const std::vector layouts(1, *loading_set_layout);
                loading_sets = CreateWrappedDescriptorSets(loading_pool, layouts);
                std::vector<VkDescriptorImageInfo> images;
                images.reserve(1);
                const VkWriteDescriptorSet write =
                    CreateWriteDescriptorSet(images, *loading_sampler, *loading_view, loading_sets[0], 0);
                device.GetLogical().UpdateDescriptorSets(std::array{write}, {});
                ::Eden::Loading::vulkan_picture_layout = reinterpret_cast<uint64_t>(*loading_set_layout);
                ::Eden::Loading::vulkan_picture_set = reinterpret_cast<uint64_t>(loading_sets[0]);
            } catch (const std::exception& error) {
                ::Eden::Report("loading screen",
                               (std::string("The loading picture could not be made: ") + error.what()).c_str());
            }
        }
        std::printf("EDEN_LOADING picture=%d\n", ::Eden::Loading::vulkan_picture_set != 0);
    }
    Frame* frame = present_manager.GetRenderFrame();
    scheduler.RequestOutsideRenderPassOperationContext();
    blit_swapchain.DrawToFrame(device, rasterizer, frame, {},
                              render_window.GetFramebufferLayout(), swapchain.GetImageCount(),
                              swapchain.GetImageViewFormat(), true);
    scheduler.Flush(*frame->render_ready);
    present_manager.Present(frame);
}
