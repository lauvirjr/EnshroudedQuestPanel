// Desenha o seletor de missoes num BMP, sem abrir o jogo.
//   selector_preview.exe [indice] [saida.bmp] [largura]
#include "../src/panel_model.h"

#include <cstdio>
#include <cstdlib>

using namespace questpanel;

int main(int argc, char** argv)
{
    const std::size_t indice = argc > 1 ? static_cast<std::size_t>(std::atoi(argv[1])) : 0;
    const char* saida = argc > 2 ? argv[2] : "seletor.bmp";
    const int largura = argc > 3 ? std::atoi(argv[3]) : 620;

    std::vector<Quest> catalogo;
    const std::wstring caminho = CatalogFilePath();
    std::wprintf(L"catalogo: %s\n", caminho.c_str());
    if (!ParseCatalogFile(caminho, catalogo)) {
        std::printf("FALHA: nao consegui ler o catalogo\n");
        return 1;
    }
    std::printf("missoes no catalogo: %zu\n", catalogo.size());
    if (indice >= catalogo.size()) { std::printf("indice fora da lista\n"); return 1; }
    std::wprintf(L"em destaque: %s\n", catalogo[indice].name.c_str());
    std::printf("objetivos da destacada: %zu\n", catalogo[indice].objectives.size());

    Raster r;
    RasterizeSelector(catalogo, indice, largura, 96, r);
    std::printf("raster: %dx%d\n", r.width, r.height);
    if (r.empty()) { std::printf("FALHA: nada desenhado\n"); return 1; }

    FILE* f = nullptr;
    if (fopen_s(&f, saida, "wb") == 0 && f) {
        BITMAPFILEHEADER fh{};
        BITMAPINFOHEADER ih{};
        ih.biSize = sizeof(ih); ih.biWidth = r.width; ih.biHeight = -r.height;
        ih.biPlanes = 1; ih.biBitCount = 32;
        ih.biSizeImage = static_cast<DWORD>(r.pixels.size());
        fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih);
        fh.bfSize = fh.bfOffBits + ih.biSizeImage;
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&ih, sizeof(ih), 1, f);
        fwrite(r.pixels.data(), 1, r.pixels.size(), f);
        fclose(f);
        std::printf("OK: %s\n", saida);
    }
    return 0;
}
