#include "../src/panel_model.h"
#include <cstdio>
using namespace questpanel;
int main(int argc, char** argv) {
    Quest q;
    if (!ParsePanelFile(PanelFilePath(), q)) { std::printf("sem painel fixado\n"); return 1; }
    Raster r;
    RasterizeQuest(q, 460, 96, r, argc > 1 && argv[1][0] == 'c' ? false : true);
    std::printf("%dx%d\n", r.width, r.height);
    FILE* f = nullptr;
    const char* saida = argc > 2 ? argv[2] : "f7.bmp";
    if (fopen_s(&f, saida, "wb") == 0 && f) {
        BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
        ih.biSize = sizeof(ih); ih.biWidth = r.width; ih.biHeight = -r.height;
        ih.biPlanes = 1; ih.biBitCount = 32; ih.biSizeImage = (DWORD)r.pixels.size();
        fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih);
        fh.bfSize = fh.bfOffBits + ih.biSizeImage;
        fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
        fwrite(r.pixels.data(), 1, r.pixels.size(), f); fclose(f);
    }
    return 0;
}
