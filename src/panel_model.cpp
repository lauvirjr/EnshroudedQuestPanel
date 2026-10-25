#include "panel_model.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>

namespace questpanel {
namespace {

// cores do painel, no mesmo tom escuro do editor
constexpr int kBackR = 20, kBackG = 26, kBackB = 34;
constexpr int kTextR = 230, kTextG = 237, kTextB = 243;
constexpr std::uint8_t kEdgeR = 79, kEdgeG = 209, kEdgeB = 197;

std::wstring Widen(const std::string& utf8)
{
    if (utf8.empty()) return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), need);
    return out;
}

// O editor marca enfase com **assim**; aqui so removemos os marcadores.
std::wstring StripEmphasis(std::wstring s)
{
    size_t at;
    while ((at = s.find(L"**")) != std::wstring::npos)
        s.erase(at, 2);
    return s;
}

std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t tab = line.find('\t', start);
        if (tab == std::string::npos) { out.push_back(line.substr(start)); break; }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return out;
}

struct DeviceContext {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* pixels = nullptr;
    int width = 0, height = 0;

    bool Create(int w, int h)
    {
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;            // topo-para-baixo
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels) return false;
        oldBitmap = SelectObject(dc, bitmap);
        width = w; height = h;
        std::memset(pixels, 0, static_cast<size_t>(w) * h * 4);
        return true;
    }
    ~DeviceContext()
    {
        if (dc && oldBitmap) SelectObject(dc, oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
};

HFONT MakeFont(int pixelHeight, int weight)
{
    return CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

// O GDI escreveu o texto sobre preto; aqui ele e composto sobre o fundo do
// painel. O brilho de cada pixel vira a cobertura, entao desenhar em cinza no
// GDI ja sai como texto apagado -- e o que destaca a linha escolhida da lista.
void ComporPixels(const DeviceContext& gdi, int width, int height, Raster& out)
{
    out.width = width;
    out.height = height;
    out.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    const auto* bytes = static_cast<const std::uint8_t*>(gdi.pixels);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const size_t at = (static_cast<size_t>(row) * width + col) * 4;
            const int cover = bytes[at];
            const auto mix = [cover](int back, int fore) {
                return static_cast<std::uint8_t>(back + (fore - back) * cover / 255);
            };
            out.pixels[at + 0] = mix(kBackB, kTextB);
            out.pixels[at + 1] = mix(kBackG, kTextG);
            out.pixels[at + 2] = mix(kBackR, kTextR);
            out.pixels[at + 3] = 255;
        }
    }
    // moldura de uma linha, para o painel nao se perder sobre cenario claro
    for (int col = 0; col < width; ++col)
        for (int row : {0, height - 1}) {
            const size_t at = (static_cast<size_t>(row) * width + col) * 4;
            out.pixels[at + 0] = kEdgeB; out.pixels[at + 1] = kEdgeG; out.pixels[at + 2] = kEdgeR;
        }
    for (int row = 0; row < height; ++row)
        for (int col : {0, width - 1}) {
            const size_t at = (static_cast<size_t>(row) * width + col) * 4;
            out.pixels[at + 0] = kEdgeB; out.pixels[at + 1] = kEdgeG; out.pixels[at + 2] = kEdgeR;
        }
}

}  // namespace

std::wstring PanelFilePath()
{
    wchar_t* base = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&base, &len, L"LOCALAPPDATA") != 0 || !base) return {};
    std::wstring path(base);
    free(base);
    path += L"\\EnshroudedQuestPanel\\active.txt";
    return path;
}

bool ReadStamp(const std::wstring& path, FileStamp& out)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    out.writeTime = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32)
                  | data.ftLastWriteTime.dwLowDateTime;
    out.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return true;
}

namespace {

// Le o arquivo inteiro; FILE_SHARE_WRITE porque o editor troca o arquivo por
// baixo enquanto o jogo roda.
bool LerTexto(const std::wstring& path, std::string& texto)
{
    if (path.empty()) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    texto.clear();
    char buffer[8192];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        texto.append(buffer, read);
    CloseHandle(file);
    return !texto.empty();
}

// Quebra o texto em missoes: cada linha MISSAO comeca uma nova.
void MontarQuests(const std::string& texto, std::vector<Quest>& out)
{
    out.clear();
    size_t start = 0;
    bool viuVersao = false;
    while (start <= texto.size()) {
        size_t end = texto.find('\n', start);
        if (end == std::string::npos) end = texto.size();
        std::string line = texto.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) { if (start > texto.size()) break; continue; }
        if (!viuVersao) {
            if (line[0] != 'V') return;          // arquivo de outro formato
            viuVersao = true;
            continue;
        }
        const auto parts = SplitTabs(line);
        const std::string& tag = parts[0];
        if (tag == "MISSAO" && parts.size() >= 2) {
            Quest q;
            q.name = StripEmphasis(Widen(parts[1]));
            out.push_back(std::move(q));
        } else if (out.empty()) {
            continue;                            // linha solta antes da primeira missao
        } else if (tag == "CONCLUI" && parts.size() >= 2) {
            const std::string& lista = parts[1];
            std::size_t ini = 0;
            while (ini <= lista.size()) {
                const std::size_t virgula = lista.find(',', ini);
                const std::string pedaco = lista.substr(
                    ini, virgula == std::string::npos ? std::string::npos : virgula - ini);
                if (!pedaco.empty())
                    out.back().completionIds.push_back(
                        static_cast<std::uint32_t>(std::strtoul(pedaco.c_str(), nullptr, 10)));
                if (virgula == std::string::npos) break;
                ini = virgula + 1;
            }
        } else if (tag == "PREMIO" && parts.size() >= 2) {
            out.back().reward = StripEmphasis(Widen(parts[1]));
        } else if (tag == "OBJ" && parts.size() >= 3) {
            Objective obj;
            obj.number = Widen(parts[1]);
            obj.title = StripEmphasis(Widen(parts[2]));
            if (parts.size() >= 4) obj.location = StripEmphasis(Widen(parts[3]));
            out.back().objectives.push_back(std::move(obj));
        } else if (tag == "PASSO" && parts.size() >= 2 && !out.back().objectives.empty()) {
            out.back().objectives.back().steps.push_back(StripEmphasis(Widen(parts[1])));
        }
        if (start > texto.size()) break;
    }
}

}  // namespace

std::wstring CatalogFilePath()
{
    // Duas moradas, nesta ordem: o que o editor gravou (atualizado, e com as
    // missoes concluidas ja de fora) e, faltando esse, o que veio junto com o
    // mod. A segunda e o que permite instalar o painel e jogar sem nunca abrir
    // o editor.
    wchar_t* base = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&base, &len, L"LOCALAPPDATA") == 0 && base) {
        std::wstring path = std::wstring(base) + L"\\EnshroudedQuestPanel\\catalogo.txt";
        free(base);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
    }
    wchar_t aqui[MAX_PATH] = {};
    HMODULE modulo = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&CatalogFilePath), &modulo);
    if (modulo && GetModuleFileNameW(modulo, aqui, MAX_PATH)) {
        wchar_t* barra = wcsrchr(aqui, L'\\');
        if (barra) {
            *(barra + 1) = L'\0';
            return std::wstring(aqui) + L"catalogo.txt";
        }
    }
    return {};
}

bool ParseCatalogFile(const std::wstring& path, std::vector<Quest>& out)
{
    out.clear();
    std::string texto;
    if (!LerTexto(path, texto)) return false;
    MontarQuests(texto, out);
    // uma missao sem objetivo nao tem passo a passo para mostrar
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const Quest& q) { return q.objectives.empty(); }),
              out.end());
    return !out.empty();
}

bool ParsePanelFile(const std::wstring& path, Quest& out)
{
    out = Quest{};
    if (path.empty()) return false;
    // FILE_SHARE_WRITE: o editor troca o arquivo por os.replace enquanto o jogo roda
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::string text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(file, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        text.append(buffer, read);
    CloseHandle(file);
    if (text.empty()) return false;

    size_t start = 0;
    bool sawVersion = false;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) { if (start > text.size()) break; continue; }

        if (!sawVersion) {
            if (line[0] != 'V') return false;        // arquivo de outro formato
            sawVersion = true;
            continue;
        }
        const auto parts = SplitTabs(line);
        const std::string& tag = parts[0];
        if (tag == "MISSAO" && parts.size() >= 2) {
            out.name = StripEmphasis(Widen(parts[1]));
        } else if (tag == "PREMIO" && parts.size() >= 2) {
            out.reward = StripEmphasis(Widen(parts[1]));
        } else if (tag == "OBJ" && parts.size() >= 3) {
            Objective obj;
            obj.number = Widen(parts[1]);
            obj.title = StripEmphasis(Widen(parts[2]));
            if (parts.size() >= 4) obj.location = StripEmphasis(Widen(parts[3]));
            out.objectives.push_back(std::move(obj));
        } else if (tag == "PASSO" && parts.size() >= 2 && !out.objectives.empty()) {
            out.objectives.back().steps.push_back(StripEmphasis(Widen(parts[1])));
        }
        if (start > text.size()) break;
    }
    return !out.name.empty();
}

std::vector<std::wstring> WrapText(HDC dc, const std::wstring& text, int maxWidth)
{
    std::vector<std::wstring> lines;
    if (text.empty() || maxWidth <= 0) return lines;
    std::wstring current;
    size_t at = 0;
    while (at < text.size()) {
        size_t space = text.find(L' ', at);
        if (space == std::wstring::npos) space = text.size();
        const std::wstring word = text.substr(at, space - at);
        at = space + 1;
        if (word.empty()) continue;
        std::wstring candidate = current.empty() ? word : current + L' ' + word;
        SIZE size{};
        GetTextExtentPoint32W(dc, candidate.c_str(), static_cast<int>(candidate.size()), &size);
        if (size.cx <= maxWidth || current.empty()) {
            current.swap(candidate);
        } else {
            lines.push_back(current);
            current = word;
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

std::wstring GameLogPath()
{
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return {};
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return {};
    *slash = L'\0';
    return std::wstring(path) + L"\\enshrouded.log";
}

bool PlayerInWorld(const std::wstring& logPath, bool previous)
{
    if (logPath.empty()) return previous;
    HANDLE file = CreateFileW(logPath.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return previous;   // sem log, mantem como estava

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) { CloseHandle(file); return previous; }
    constexpr LONGLONG kTail = 512 * 1024;               // o log cresce; so o fim importa
    LARGE_INTEGER from{};
    from.QuadPart = size.QuadPart > kTail ? size.QuadPart - kTail : 0;
    SetFilePointerEx(file, from, nullptr, FILE_BEGIN);

    std::string text;
    text.resize(static_cast<size_t>(size.QuadPart - from.QuadPart));
    DWORD read = 0, total = 0;
    while (total < text.size() &&
           ReadFile(file, text.data() + total, static_cast<DWORD>(text.size() - total), &read, nullptr) &&
           read > 0)
        total += read;
    CloseHandle(file);
    text.resize(total);

    // ultima transicao de sessao: "[Session] started transition from 'A' to 'B'"
    static const std::string marker = "[Session] started transition from '";
    const size_t at = text.rfind(marker);
    if (at == std::string::npos) return false;           // nenhuma: ainda no menu
    const size_t to = text.find("' to '", at);
    if (to == std::string::npos) return previous;
    const size_t begin = to + 6;
    const size_t end = text.find('\'', begin);
    if (end == std::string::npos) return previous;
    return text.compare(begin, end - begin, "Lobby") != 0;
}

void RasterizeSelector(const std::vector<Quest>& quests, std::size_t selected,
                       int maxWidth, int dpi, Raster& out)
{
    out.clear();
    if (quests.empty() || maxWidth <= 32) return;
    dpi = (std::max)(96, (std::min)(dpi, 384));
    const auto scale = [dpi](int v) { return MulDiv(v, dpi, 96); };

    constexpr int kVisiveis = 14;
    const int padding = scale(12);
    const int linha = scale(19);
    const int altura = padding * 2 + linha * (kVisiveis + 2);

    DeviceContext gdi;
    if (!gdi.Create(maxWidth, altura)) return;
    SetBkMode(gdi.dc, TRANSPARENT);

    std::unique_ptr<std::remove_pointer_t<HFONT>, decltype(&::DeleteObject)>
        titulo(MakeFont(scale(15), FW_SEMIBOLD), &::DeleteObject),
        normal(MakeFont(scale(13), FW_NORMAL), &::DeleteObject),
        forte(MakeFont(scale(13), FW_SEMIBOLD), &::DeleteObject);
    if (!titulo || !normal || !forte) return;

    // janela deslizante com a selecao no meio sempre que possivel
    const int total = static_cast<int>(quests.size());
    int primeiro = static_cast<int>(selected) - kVisiveis / 2;
    primeiro = (std::max)(0, (std::min)(primeiro, total - kVisiveis));
    if (total < kVisiveis) primeiro = 0;

    int y = padding;
    {
        auto velha = SelectObject(gdi.dc, titulo.get());
        SetTextColor(gdi.dc, RGB(255, 255, 255));
        wchar_t cabecalho[128];
        swprintf_s(cabecalho, L"Escolha a missao  (%d de %d)",
                   static_cast<int>(selected) + 1, total);
        RECT box{padding, y, maxWidth - padding, y + linha};
        DrawTextW(gdi.dc, cabecalho, -1, &box, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(gdi.dc, velha);
        y += linha + scale(6);
    }

    for (int i = primeiro; i < total && i < primeiro + kVisiveis; ++i) {
        const bool atual = static_cast<std::size_t>(i) == selected;
        auto velha = SelectObject(gdi.dc, atual ? forte.get() : normal.get());
        // o destaque sai mais claro; o resto fica apagado
        SetTextColor(gdi.dc, atual ? RGB(255, 255, 255) : RGB(150, 150, 150));
        std::wstring texto = (atual ? L"> " : L"   ") + quests[i].name;
        RECT box{padding, y, maxWidth - padding, y + linha};
        DrawTextW(gdi.dc, texto.c_str(), static_cast<int>(texto.size()), &box,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(gdi.dc, velha);
        y += linha;
    }
    {
        auto velha = SelectObject(gdi.dc, normal.get());
        SetTextColor(gdi.dc, RGB(120, 190, 180));
        RECT box{padding, y + scale(4), maxWidth - padding, y + scale(4) + linha};
        DrawTextW(gdi.dc, L"setas: mover    Enter ou F6: escolher    Esc: cancelar    F5: esconder", -1, &box,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(gdi.dc, velha);
        y += linha + scale(4);
    }
    GdiFlush();
    ComporPixels(gdi, maxWidth, (std::min)(y + padding, altura), out);
}

void RasterizeQuest(const Quest& quest, int maxWidth, int dpi, Raster& out, bool comCabecalho)
{
    out.clear();
    if (quest.empty() || maxWidth <= 32) return;
    dpi = (std::max)(96, (std::min)(dpi, 384));
    const auto scale = [dpi](int v) { return MulDiv(v, dpi, 96); };

    const int padding = scale(12);
    const int titleSize = scale(17), headSize = scale(14), bodySize = scale(13);
    const int maxHeight = scale(900);
    const int textWidth = maxWidth - padding * 2;

    DeviceContext gdi;
    if (!gdi.Create(maxWidth, maxHeight)) return;
    SetBkMode(gdi.dc, TRANSPARENT);
    SetTextColor(gdi.dc, RGB(255, 255, 255));

    std::unique_ptr<std::remove_pointer_t<HFONT>, decltype(&::DeleteObject)>
        titleFont(MakeFont(titleSize, FW_SEMIBOLD), &::DeleteObject),
        headFont(MakeFont(headSize, FW_SEMIBOLD), &::DeleteObject),
        bodyFont(MakeFont(bodySize, FW_NORMAL), &::DeleteObject);
    if (!titleFont || !headFont || !bodyFont) return;

    int y = padding;
    const auto drawLine = [&](HFONT font, const std::wstring& text, int indent, int lineGap) {
        if (text.empty()) return;
        auto old = SelectObject(gdi.dc, font);
        TEXTMETRICW tm{};
        GetTextMetricsW(gdi.dc, &tm);
        for (const auto& line : WrapText(gdi.dc, text, textWidth - indent)) {
            if (y + tm.tmHeight > maxHeight - padding) break;
            RECT box{padding + indent, y, maxWidth - padding, y + tm.tmHeight};
            DrawTextW(gdi.dc, line.c_str(), static_cast<int>(line.size()), &box,
                      DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            y += tm.tmHeight + lineGap;
        }
        SelectObject(gdi.dc, old);
    };

    if (comCabecalho) {
        drawLine(titleFont.get(), quest.name, 0, scale(3));
        if (!quest.reward.empty())
            drawLine(bodyFont.get(), L"Recompensa: " + quest.reward, 0, scale(3));
        y += scale(6);
    }

    for (const auto& obj : quest.objectives) {
        std::wstring head = obj.number + L". " + obj.title;
        if (!obj.location.empty()) head += L"  [" + obj.location + L"]";
        drawLine(headFont.get(), head, 0, scale(2));
        for (const auto& step : obj.steps)
            drawLine(bodyFont.get(), step, scale(14), scale(2));
        y += scale(7);
    }
    GdiFlush();

    const int usedHeight = (std::min)(y + padding - scale(7), maxHeight);
    ComporPixels(gdi, maxWidth, (std::max)(usedHeight, padding * 2), out);
}

}  // namespace questpanel
