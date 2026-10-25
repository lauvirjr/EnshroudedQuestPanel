// Leitura do arquivo que o editor escreve e rasterizacao do painel em faixas.
// Nada aqui depende de Vulkan, entao da para testar fora do jogo (ver tests/).
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace questpanel {

struct Objective {
    std::wstring number;
    std::wstring title;
    std::wstring location;
    std::vector<std::wstring> steps;
};

struct Quest {
    std::wstring name;
    std::wstring reward;
    std::vector<Objective> objectives;
    // ids de conhecimento que marcam o fim de cada objetivo; a camada procura
    // esses numeros na memoria do jogo para saber se a missao ja terminou
    std::vector<std::uint32_t> completionIds;
    bool empty() const { return name.empty(); }
};

// O painel pronto, em BGRA opaco. A camada sobe isto para uma VkImage e copia
// para a imagem da swapchain com um unico vkCmdBlitImage por quadro -- por isso
// o texto nao vira milhares de retangulos, como faz o mod do minimapa.
struct Raster {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;      // width*height*4, BGRA
    bool empty() const { return pixels.empty(); }
    void clear() { width = height = 0; pixels.clear(); }
};

// Le o active.txt (UTF-8, campos por TAB). Devolve false se o arquivo nao existe
// ou nao mudou desde a ultima leitura; stamp guarda o estado entre chamadas.
struct FileStamp {
    std::uint64_t writeTime = 0;
    std::uint64_t size = 0;
    bool operator==(const FileStamp& o) const { return writeTime == o.writeTime && size == o.size; }
};

std::wstring PanelFilePath();
std::wstring CatalogFilePath();
bool ReadStamp(const std::wstring& path, FileStamp& out);
bool ParsePanelFile(const std::wstring& path, Quest& out);

// O catalogo traz todas as missoes num arquivo so, para dar de escolher dentro
// do jogo: se fosse preciso sair para trocar de missao, mais valia procurar na
// internet -- que e o que o painel existe para evitar.
bool ParseCatalogFile(const std::wstring& path, std::vector<Quest>& out);

// Desenha a lista de missoes com uma em destaque (o seletor do jogo).
void RasterizeSelector(const std::vector<Quest>& quests, std::size_t selected,
                       int maxWidth, int dpi, Raster& out);

// Converte a missao em faixas prontas para desenhar, respeitando maxWidth.
// O resultado e cacheado pelo chamador: rasterizar e caro, desenhar nao.
// comCabecalho=false deixa so os objetivos (tecla F7 no jogo)
void RasterizeQuest(const Quest& quest, int maxWidth, int dpi, Raster& out,
                    bool comCabecalho = true);

// Exposto para teste: quebra o texto em linhas que cabem em maxWidth.
std::vector<std::wstring> WrapText(HDC dc, const std::wstring& text, int maxWidth);

// O painel so deve aparecer dentro de um mundo, nunca no menu. Em vez de
// procurar padroes de bytes no executavel -- o que faz o mod do minimapa
// quebrar a cada atualizacao --, lemos o enshrouded.log, que registra
// "[Session] started transition from 'X' to 'Y'" a cada troca. O editor de
// save usa exatamente essas marcas.
std::wstring GameLogPath();          // enshrouded.log ao lado do executavel
bool PlayerInWorld(const std::wstring& logPath, bool previous);

}  // namespace questpanel
