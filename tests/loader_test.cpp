// Sobe o loader Vulkan de verdade, cria instance + device e uma swapchain
// headless-ish (via janela escondida) para exercitar a camada sem abrir o jogo.
// Carrega vulkan-1.dll em tempo de execucao: nao exige o SDK instalado.
#define VK_NO_PROTOTYPES
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <cstdio>
#include <vector>

#define LOAD_I(name) auto name = reinterpret_cast<PFN_##name>(gipa(instance, #name))

int main()
{
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) { std::printf("FALHA: vulkan-1.dll nao carregou\n"); return 1; }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(lib, "vkGetInstanceProcAddr"));
    if (!gipa) { std::printf("FALHA: sem vkGetInstanceProcAddr\n"); return 1; }

    VkInstance instance = VK_NULL_HANDLE;
    {
        auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
            gipa(nullptr, "vkEnumerateInstanceLayerProperties"));
        std::uint32_t count = 0;
        enumerate(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        enumerate(&count, layers.data());
        std::printf("camadas visiveis ao loader: %u\n", count);
        bool mine = false;
        for (const auto& l : layers) {
            const bool ours = std::strstr(l.layerName, "quest_panel") != nullptr;
            if (ours) mine = true;
            std::printf("   %s%s\n", l.layerName, ours ? "   <== a nossa" : "");
        }
        std::printf("%s\n", mine ? "camada ENCONTRADA pelo loader"
                                 : "AVISO: a camada nao aparece (implicitas nem sempre listam)");

        auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "questpanel-loader-test";
        app.apiVersion = VK_API_VERSION_1_1;
        const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = 2;
        info.ppEnabledExtensionNames = exts;
        const VkResult r = createInstance(&info, nullptr, &instance);
        if (r != VK_SUCCESS) { std::printf("FALHA: vkCreateInstance = %d\n", r); return 1; }
        std::printf("vkCreateInstance OK\n");
    }

    LOAD_I(vkEnumeratePhysicalDevices);
    LOAD_I(vkGetPhysicalDeviceProperties);
    LOAD_I(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_I(vkCreateDevice);
    LOAD_I(vkGetDeviceProcAddr);
    LOAD_I(vkDestroyInstance);
    LOAD_I(vkCreateWin32SurfaceKHR);
    LOAD_I(vkGetPhysicalDeviceSurfaceSupportKHR);
    LOAD_I(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD_I(vkGetPhysicalDeviceSurfaceFormatsKHR);
    LOAD_I(vkDestroySurfaceKHR);

    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (!deviceCount) { std::printf("FALHA: nenhuma GPU Vulkan\n"); return 1; }
    std::vector<VkPhysicalDevice> physicals(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicals.data());
    VkPhysicalDevice physical = physicals[0];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical, &props);
    std::printf("GPU: %s\n", props.deviceName);

    // janela escondida so para ter uma surface real
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "questpanel_test";
    RegisterClassA(&wc);
    HWND window = CreateWindowA("questpanel_test", "t", WS_OVERLAPPEDWINDOW,
                                0, 0, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);
    VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surfaceInfo.hinstance = wc.hInstance;
    surfaceInfo.hwnd = window;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vkCreateWin32SurfaceKHR(instance, &surfaceInfo, nullptr, &surface) != VK_SUCCESS) {
        std::printf("FALHA: surface\n"); return 1;
    }

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());
    std::uint32_t family = UINT32_MAX;
    for (std::uint32_t i = 0; i < familyCount; ++i) {
        VkBool32 canPresent = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &canPresent);
        if (canPresent && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    }
    if (family == UINT32_MAX) { std::printf("FALHA: sem fila de apresentacao\n"); return 1; }

    const float priority = 1.f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    const char* deviceExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExts;
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) {
        std::printf("FALHA: vkCreateDevice\n"); return 1;
    }
    std::printf("vkCreateDevice OK\n");

    auto gdpa = vkGetDeviceProcAddr;
    auto createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(gdpa(device, "vkCreateSwapchainKHR"));
    auto destroySwapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(gdpa(device, "vkDestroySwapchainKHR"));
    auto getImages = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(gdpa(device, "vkGetSwapchainImagesKHR"));
    auto getQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(gdpa(device, "vkGetDeviceQueue"));
    auto present = reinterpret_cast<PFN_vkQueuePresentKHR>(gdpa(device, "vkQueuePresentKHR"));
    auto destroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(gdpa(device, "vkDestroyDevice"));
    std::printf("ponteiros: CreateSwapchain=%p QueuePresent=%p GetDeviceQueue=%p\n",
                (void*)createSwapchain, (void*)present, (void*)getQueue);

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data());

    VkSwapchainCreateInfoKHR chain{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    chain.surface = surface;
    chain.minImageCount = caps.minImageCount < 2 ? 2 : caps.minImageCount;
    chain.imageFormat = formats[0].format;
    chain.imageColorSpace = formats[0].colorSpace;
    chain.imageExtent = {640, 360};
    chain.imageArrayLayers = 1;
    // de proposito SEM TRANSFER_DST: a camada tem que acrescentar sozinha
    chain.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    chain.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    chain.preTransform = caps.currentTransform;
    chain.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    chain.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    chain.clipped = VK_TRUE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    const VkResult sr = createSwapchain(device, &chain, nullptr, &swapchain);
    if (sr != VK_SUCCESS) { std::printf("FALHA: vkCreateSwapchainKHR = %d\n", sr); return 1; }
    std::uint32_t imageCount = 0;
    getImages(device, swapchain, &imageCount, nullptr);
    std::printf("vkCreateSwapchainKHR OK, %u imagens (formato %d)\n", imageCount, chain.imageFormat);

    VkQueue queue = VK_NULL_HANDLE;
    getQueue(device, family, 0, &queue);
    std::printf("fila obtida: %p\n", (void*)queue);

    // ---- apresenta alguns quadros de verdade, para a camada desenhar ----
    // O jogo sempre entrega a imagem em PRESENT_SRC_KHR; aqui fazemos a mesma
    // transicao, senao a barreira da camada partiria de um layout que nao existe.
    std::vector<VkImage> images(imageCount);
    getImages(device, swapchain, &imageCount, images.data());

    auto createPool = reinterpret_cast<PFN_vkCreateCommandPool>(gdpa(device, "vkCreateCommandPool"));
    auto allocCmd = reinterpret_cast<PFN_vkAllocateCommandBuffers>(gdpa(device, "vkAllocateCommandBuffers"));
    auto beginCmd = reinterpret_cast<PFN_vkBeginCommandBuffer>(gdpa(device, "vkBeginCommandBuffer"));
    auto endCmd = reinterpret_cast<PFN_vkEndCommandBuffer>(gdpa(device, "vkEndCommandBuffer"));
    auto cmdBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(gdpa(device, "vkCmdPipelineBarrier"));
    auto submit = reinterpret_cast<PFN_vkQueueSubmit>(gdpa(device, "vkQueueSubmit"));
    auto waitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(gdpa(device, "vkQueueWaitIdle"));
    auto acquire = reinterpret_cast<PFN_vkAcquireNextImageKHR>(gdpa(device, "vkAcquireNextImageKHR"));
    auto createFence = reinterpret_cast<PFN_vkCreateFence>(gdpa(device, "vkCreateFence"));
    auto waitFences = reinterpret_cast<PFN_vkWaitForFences>(gdpa(device, "vkWaitForFences"));
    auto resetFences = reinterpret_cast<PFN_vkResetFences>(gdpa(device, "vkResetFences"));

    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family;
    createPool(device, &poolInfo, nullptr, &pool);
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.commandPool = pool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    allocCmd(device, &cmdInfo, &cmd);
    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    createFence(device, &fenceInfo, nullptr, &fence);

    ShowWindow(window, SW_SHOW);
    int presented = 0;
    std::uint32_t lastIndex = 0;
    for (int frame = 0; frame < 12; ++frame) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        std::uint32_t index = 0;
        if (acquire(device, swapchain, UINT64_MAX, VK_NULL_HANDLE, fence, &index) != VK_SUCCESS) break;
        waitFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        resetFences(device, 1, &fence);

        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginCmd(cmd, &begin);
        VkImageMemoryBarrier toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toPresent.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = images[index];
        toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        cmdBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   0, 0, nullptr, 0, nullptr, 1, &toPresent);
        endCmd(cmd);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        submit(queue, 1, &si, VK_NULL_HANDLE);
        waitIdle(queue);

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &index;
        const VkResult pr = present(queue, &pi);     // <= a camada desenha aqui
        if (pr != VK_SUCCESS) { std::printf("FALHA: present = %d no quadro %d\n", pr, frame); break; }
        ++presented;
        lastIndex = index;
        waitIdle(queue);
        Sleep(16);
    }
    std::printf("quadros apresentados pela camada: %d\n", presented);

    // Le a imagem da swapchain de volta pela GPU. BitBlt nao serve: o conteudo
    // do Vulkan nunca passa pelo GDI da janela.
    if (presented > 0) {
        auto createBuffer = reinterpret_cast<PFN_vkCreateBuffer>(gdpa(device, "vkCreateBuffer"));
        auto bufferReq = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(gdpa(device, "vkGetBufferMemoryRequirements"));
        auto allocMem = reinterpret_cast<PFN_vkAllocateMemory>(gdpa(device, "vkAllocateMemory"));
        auto bindBuffer = reinterpret_cast<PFN_vkBindBufferMemory>(gdpa(device, "vkBindBufferMemory"));
        auto mapMem = reinterpret_cast<PFN_vkMapMemory>(gdpa(device, "vkMapMemory"));
        auto copyToBuffer = reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(gdpa(device, "vkCmdCopyImageToBuffer"));
        auto memProps = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            gipa(instance, "vkGetPhysicalDeviceMemoryProperties"));

        const std::uint32_t w = chain.imageExtent.width, h = chain.imageExtent.height;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;
        VkBuffer readback = VK_NULL_HANDLE;
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = bytes;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        createBuffer(device, &bci, nullptr, &readback);
        VkMemoryRequirements req{};
        bufferReq(device, readback, &req);
        VkPhysicalDeviceMemoryProperties mp{};
        memProps(physical, &mp);
        std::uint32_t typeIndex = UINT32_MAX;
        for (std::uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((req.memoryTypeBits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { typeIndex = i; break; }
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = typeIndex;
        allocMem(device, &mai, nullptr, &mem);
        bindBuffer(device, readback, mem, 0);

        // a ultima imagem apresentada ainda guarda o que a camada escreveu
        const std::uint32_t last = lastIndex;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginCmd(cmd, &begin);
        VkImageMemoryBarrier toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toSrc.srcAccessMask = 0;
        toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSrc.image = images[last];
        toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        cmdBarrier(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   0, 0, nullptr, 0, nullptr, 1, &toSrc);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, 1};
        copyToBuffer(cmd, images[last], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &region);
        endCmd(cmd);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        submit(queue, 1, &si, VK_NULL_HANDLE);
        waitIdle(queue);

        void* mapped = nullptr;
        mapMem(device, mem, 0, VK_WHOLE_SIZE, 0, &mapped);
        const auto* px = static_cast<const std::uint8_t*>(mapped);
        // a camada desenha a partir de (24,24): procuramos a borda ciano
        const auto at = [&](int x, int y) { return px + (static_cast<size_t>(y) * w + x) * 4; };
        const std::uint8_t* edge = at(24, 24);
        const std::uint8_t* inside = at(60, 60);
        std::printf("pixel da borda  (24,24): B=%u G=%u R=%u  (esperado ~197/209/79)\n",
                    edge[0], edge[1], edge[2]);
        std::printf("pixel do fundo  (60,60): B=%u G=%u R=%u  (esperado ~34/26/20)\n",
                    inside[0], inside[1], inside[2]);
        const bool edgeOk = edge[0] > 150 && edge[1] > 150 && edge[2] < 140;
        const bool insideOk = inside[0] < 90 && inside[1] < 90 && inside[2] < 90;
        std::printf("%s\n", (edgeOk && insideOk)
            ? "PAINEL DESENHADO: a camada escreveu na imagem da swapchain"
            : "FALHA: a imagem nao tem o painel");

        FILE* f = nullptr;
        if (fopen_s(&f, "captura.bmp", "wb") == 0 && f) {
            BITMAPFILEHEADER fh{};
            BITMAPINFOHEADER ih{};
            ih.biSize = sizeof(ih); ih.biWidth = static_cast<LONG>(w);
            ih.biHeight = -static_cast<LONG>(h); ih.biPlanes = 1; ih.biBitCount = 32;
            ih.biSizeImage = static_cast<DWORD>(bytes);
            fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih);
            fh.bfSize = fh.bfOffBits + ih.biSizeImage;
            fwrite(&fh, sizeof(fh), 1, f);
            fwrite(&ih, sizeof(ih), 1, f);
            fwrite(px, 1, static_cast<size_t>(bytes), f);
            fclose(f);
            std::printf("captura.bmp gravado (%ux%u)\n", w, h);
        }
        if (!edgeOk || !insideOk) return 1;
    }

    destroySwapchain(device, swapchain, nullptr);
    destroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    DestroyWindow(window);
    std::printf("\nOK: o loader subiu a cadeia inteira com a camada e desmontou sem erro\n");
    return 0;
}
