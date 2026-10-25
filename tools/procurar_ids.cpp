// Terceira tentativa de achar a missao marcada como rota.
//
// As duas primeiras procuraram o NOME da missao e a CHAVE do texto, e as duas
// deram zero. O que nao existia na epoca era o catalogo extraido do jogo, que
// trouxe um identificador novo: o entryId de cada objetivo do diario, alem dos
// ids de conhecimento de pre-requisito e de conclusao.
//
// Aqui varremos a memoria gravavel UMA vez e dizemos quais desses ids o jogo
// mantem por perto. Se o entryId da missao marcada aparecer, ha o que seguir.
//
// So le a memoria do jogo, nunca escreve.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Id {
    std::uint32_t valor;
    std::string tipo;
    std::string rotulo;
};

DWORD AcharJogo()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e{sizeof(PROCESSENTRY32W)};
    DWORD pid = 0;
    if (Process32FirstW(snap, &e)) {
        do {
            if (_wcsicmp(e.szExeFile, L"enshrouded.exe") == 0) { pid = e.th32ProcessID; break; }
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return pid;
}

std::vector<Id> CarregarIds()
{
    std::vector<Id> ids;
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* barra = wcsrchr(exe, L'\\');
    if (barra) *(barra + 1) = L'\0';
    std::wstring caminho = std::wstring(exe) + L"ids_missoes.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, caminho.c_str(), L"r, ccs=UTF-8") != 0 || !f) return ids;
    wchar_t linha[1024];
    while (fgetws(linha, 1024, f)) {
        std::wstring l(linha);
        const size_t t1 = l.find(L'\t');
        if (t1 == std::wstring::npos) continue;
        const size_t t2 = l.find(L'\t', t1 + 1);
        if (t2 == std::wstring::npos) continue;
        std::wstring rot = l.substr(t2 + 1);
        while (!rot.empty() && (rot.back() == L'\n' || rot.back() == L'\r')) rot.pop_back();
        char tipo[32] = {}, rotulo[512] = {};
        WideCharToMultiByte(CP_UTF8, 0, l.substr(t1 + 1, t2 - t1 - 1).c_str(), -1, tipo, 31, nullptr, nullptr);
        WideCharToMultiByte(CP_UTF8, 0, rot.c_str(), -1, rotulo, 511, nullptr, nullptr);
        ids.push_back({static_cast<std::uint32_t>(_wtoi64(l.substr(0, t1).c_str())), tipo, rotulo});
    }
    fclose(f);
    return ids;
}

}  // namespace

int main(int argc, char** argv)
{
    const char* filtro = argc > 1 ? argv[1] : "Gnomo";
    std::printf("\n  PROCURANDO OS IDS DE MISSAO NA MEMORIA DO JOGO\n");
    std::printf("  ==============================================\n");
    std::printf("  (so leitura; destaque para quem casa com \"%s\")\n\n", filtro);

    auto ids = CarregarIds();
    if (ids.empty()) { std::printf("  nao achei ids_missoes.txt ao lado deste programa\n"); return 1; }
    std::printf("  ids carregados: %zu\n", ids.size());

    std::unordered_map<std::uint32_t, std::vector<const Id*>> porValor;
    for (const auto& id : ids) porValor[id.valor].push_back(&id);

    const DWORD pid = AcharJogo();
    if (!pid) { std::printf("  abra o jogo e entre no mundo\n"); return 1; }
    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc) { std::printf("  nao consegui abrir o processo (erro %lu)\n", GetLastError()); return 1; }

    std::unordered_map<std::uint32_t, int> achados;
    std::size_t regioes = 0, bytes = 0;
    MEMORY_BASIC_INFORMATION info{};
    std::uintptr_t endereco = 0;
    std::vector<unsigned char> buffer(1u << 20);

    while (VirtualQueryEx(proc, reinterpret_cast<LPCVOID>(endereco), &info, sizeof(info)) == sizeof(info)) {
        const auto proximo = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        const bool vale = info.State == MEM_COMMIT &&
                          !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                          (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
        if (vale && info.RegionSize < (1ull << 31)) {
            ++regioes;
            const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            for (std::size_t off = 0; off < info.RegionSize; off += buffer.size() - 4) {
                const std::size_t pedir = min(buffer.size(), info.RegionSize - off);
                if (pedir < 4) break;
                SIZE_T lido = 0;
                if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(base + off),
                                       buffer.data(), pedir, &lido) || lido < 4)
                    continue;
                bytes += lido;
                for (std::size_t i = 0; i + 4 <= lido; i += 4) {
                    std::uint32_t v;
                    std::memcpy(&v, buffer.data() + i, 4);
                    if (v && porValor.count(v)) ++achados[v];
                }
            }
        }
        if (proximo <= endereco) break;
        endereco = proximo;
    }
    CloseHandle(proc);
    std::printf("  varridos %zu MB em %zu regioes gravaveis\n\n", bytes / (1024 * 1024), regioes);

    std::map<std::string, int> porTipo, totalTipo;
    for (const auto& id : ids) {
        ++totalTipo[id.tipo];
        if (achados.count(id.valor)) ++porTipo[id.tipo];
    }
    std::printf("  QUANTOS IDS DE CADA TIPO ESTAO NA MEMORIA:\n");
    for (const auto& [tipo, n] : totalTipo)
        std::printf("     %-10s %4d de %4d\n", tipo.c_str(), porTipo[tipo], n);

    std::printf("\n  OS IDS DA MISSAO EM DESTAQUE:\n");
    bool algum = false;
    for (const auto& id : ids) {
        if (id.rotulo.find(filtro) == std::string::npos) continue;
        const auto it = achados.find(id.valor);
        std::printf("     %-9s %-11u %-6s  %s\n", id.tipo.c_str(), id.valor,
                    it == achados.end() ? "nao" : "SIM", id.rotulo.c_str());
        algum = true;
    }
    if (!algum) std::printf("     (nenhum id casa com \"%s\")\n", filtro);

    std::printf("\n  Quantas vezes cada id aparece (os 8 mais repetidos):\n");
    std::vector<std::pair<std::uint32_t, int>> ordenado(achados.begin(), achados.end());
    std::sort(ordenado.begin(), ordenado.end(),
              [](auto& a, auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < ordenado.size() && i < 8; ++i) {
        const auto* id = porValor[ordenado[i].first][0];
        std::printf("     %-9s %-11u x%-4d %s\n", id->tipo.c_str(), ordenado[i].first,
                    ordenado[i].second, id->rotulo.c_str());
    }
    std::printf("\n  (Enter para fechar)\n");
    getchar();
    return 0;
}
