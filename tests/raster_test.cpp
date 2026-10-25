// Le o active.txt, rasteriza o painel e grava um BMP para conferir a olho.
// Nao depende de Vulkan nem do jogo:  raster_test.exe [saida.bmp] [largura]
#include "../src/panel_model.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace questpanel;

namespace {

bool WriteBmp(const char* path, int width, int height, const std::vector<std::uint8_t>& rgba)
{
    const int rowBytes = width * 3;
    const int padding = (4 - (rowBytes % 4)) % 4;
    const int imageBytes = (rowBytes + padding) * height;

    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + imageBytes;
    ih.biSize = sizeof(ih);
    ih.biWidth = width;
    ih.biHeight = -height;                 // topo-para-baixo
    ih.biPlanes = 1;
    ih.biBitCount = 24;

    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return false;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    const std::uint8_t zeros[3] = {0, 0, 0};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* px = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            const std::uint8_t bgr[3] = {px[2], px[1], px[0]};
            fwrite(bgr, 1, 3, f);
        }
        if (padding) fwrite(zeros, 1, padding, f);
    }
    fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "painel.bmp";
    const int width = argc > 2 ? std::atoi(argv[2]) : 420;

    const std::wstring path = PanelFilePath();
    std::wprintf(L"arquivo: %s\n", path.c_str());

    Quest quest;
    if (!ParsePanelFile(path, quest)) {
        std::printf("FALHA: nao consegui ler o painel (fixe uma missao no editor primeiro)\n");
        return 1;
    }
    std::wprintf(L"missao: %s\n", quest.name.c_str());
    std::wprintf(L"premio: %s\n", quest.reward.c_str());
    std::printf("objetivos: %zu\n", quest.objectives.size());
    for (const auto& o : quest.objectives) {
        std::wprintf(L"  [%s] %s", o.number.c_str(), o.title.c_str());
        if (!o.location.empty()) std::wprintf(L"  (local: %s)", o.location.c_str());
        std::printf("  -> %zu passos\n", o.steps.size());
    }

    Raster raster;
    RasterizeQuest(quest, width, 96, raster);
    std::printf("raster: %dx%d, %zu KB de pixels\n", raster.width, raster.height,
                raster.pixels.size() / 1024);
    if (raster.empty()) { std::printf("FALHA: nenhum pixel gerado\n"); return 1; }

    // Os pixels sao exatamente os que a camada sobe para a GPU.
    if (!WriteBmp(out, raster.width, raster.height, raster.pixels)) {
        std::printf("FALHA: nao consegui gravar %s\n", out);
        return 1;
    }
    std::printf("OK: %s gravado\n", out);
    return 0;
}
