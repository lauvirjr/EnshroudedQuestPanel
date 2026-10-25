// Experimento: descobrir se da para saber, de dentro do processo do jogo, qual
// missao esta marcada como rota.
//
// A primeira tentativa procurou o NOME da missao na memoria e nao achou nada,
// embora o scanner encontrasse outros textos do jogo. A conclusao foi que o
// Enshrouded nao movimenta o texto: ele movimenta a CHAVE da tabela de idioma
// (a mesma chave vale para todos os idiomas). Entao agora procuramos o numero.
//
// Um u32 qualquer aparece muitas vezes por acaso, por isso vai em dois tempos:
//   1. com a missao A marcada, guardamos todo endereco que contem a chave de A;
//   2. com a missao B marcada, relemos esses enderecos e ficamos com os que
//      passaram a valer a chave de B.
// O que sobrevive aos dois e um campo que acompanha a missao marcada.
//
// Isto e so diagnostico: le a memoria do jogo, nunca escreve.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

FILE* g_relatorio = nullptr;

void Diz(const wchar_t* formato, ...)
{
    wchar_t linha[2048];
    va_list args;
    va_start(args, formato);
    _vsnwprintf_s(linha, 2047, _TRUNCATE, formato, args);
    va_end(args);
    std::wprintf(L"%s\n", linha);
    if (g_relatorio) {
        std::fwprintf(g_relatorio, L"%s\n", linha);
        std::fflush(g_relatorio);
    }
}

struct Regiao { std::uintptr_t base; std::size_t tamanho; };

DWORD AcharJogo()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entrada{sizeof(PROCESSENTRY32W)};
    DWORD pid = 0;
    if (Process32FirstW(snap, &entrada)) {
        do {
            if (_wcsicmp(entrada.szExeFile, L"enshrouded.exe") == 0) {
                pid = entrada.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entrada));
    }
    CloseHandle(snap);
    return pid;
}

std::vector<Regiao> Regioes(HANDLE proc)
{
    std::vector<Regiao> out;
    MEMORY_BASIC_INFORMATION info{};
    std::uintptr_t endereco = 0;
    while (VirtualQueryEx(proc, reinterpret_cast<LPCVOID>(endereco), &info, sizeof(info)) == sizeof(info)) {
        const auto proximo = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        const bool gravavel = info.State == MEM_COMMIT &&
                              !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                              (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
        // so memoria que o jogo escreve: a missao marcada muda em tempo de jogo
        if (gravavel && info.RegionSize < (1ull << 31))
            out.push_back({reinterpret_cast<std::uintptr_t>(info.BaseAddress), info.RegionSize});
        if (proximo <= endereco) break;
        endereco = proximo;
    }
    return out;
}

bool Ler(HANDLE proc, std::uintptr_t endereco, void* destino, std::size_t tamanho)
{
    SIZE_T lido = 0;
    return ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(endereco), destino, tamanho, &lido) &&
           lido == tamanho;
}

// Todos os enderecos (alinhados em 4) que contem este u32.
std::vector<std::uintptr_t> ProcurarU32(HANDLE proc, const std::vector<Regiao>& regioes,
                                        std::uint32_t valor, std::size_t limite)
{
    constexpr std::size_t kBloco = 1u << 20;
    std::vector<std::uintptr_t> achados;
    std::vector<unsigned char> buffer(kBloco);
    for (const auto& r : regioes) {
        for (std::size_t off = 0; off < r.tamanho; off += kBloco - 4) {
            const std::size_t pedir = (std::min)(kBloco, r.tamanho - off);
            if (pedir < 4) break;
            SIZE_T lido = 0;
            // em blocos, porque uma pagina ilegivel derrubaria a regiao inteira
            if (!ReadProcessMemory(proc, reinterpret_cast<LPCVOID>(r.base + off),
                                   buffer.data(), pedir, &lido) || lido < 4)
                continue;
            for (std::size_t i = 0; i + 4 <= lido; i += 4) {
                std::uint32_t atual;
                std::memcpy(&atual, buffer.data() + i, 4);
                if (atual == valor) {
                    achados.push_back(r.base + off + i);
                    if (achados.size() >= limite) return achados;
                }
            }
        }
    }
    return achados;
}

std::wstring Perguntar(const wchar_t* pergunta)
{
    std::wprintf(L"%s", pergunta);
    if (g_relatorio) std::fwprintf(g_relatorio, L"%s", pergunta);
    wchar_t linha[512] = {};
    if (!fgetws(linha, 512, stdin)) return {};
    std::wstring s(linha);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' ')) s.pop_back();
    if (g_relatorio) std::fwprintf(g_relatorio, L"%s\n", s.c_str());
    return s;
}

// loca_chaves.txt, gerado por gerar_chaves.py: "chave<TAB>texto" por linha
std::map<std::wstring, std::uint32_t> CarregarChaves()
{
    std::map<std::wstring, std::uint32_t> mapa;
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* barra = wcsrchr(exe, L'\\');
    if (barra) *(barra + 1) = L'\0';
    std::wstring caminho = std::wstring(exe) + L"loca_chaves.txt";

    FILE* f = nullptr;
    if (_wfopen_s(&f, caminho.c_str(), L"r, ccs=UTF-8") != 0 || !f) return mapa;
    wchar_t linha[1024];
    while (fgetws(linha, 1024, f)) {
        wchar_t* tab = wcschr(linha, L'\t');
        if (!tab) continue;
        *tab = L'\0';
        std::wstring texto(tab + 1);
        while (!texto.empty() && (texto.back() == L'\n' || texto.back() == L'\r')) texto.pop_back();
        if (!texto.empty()) mapa[texto] = static_cast<std::uint32_t>(_wtoi64(linha));
    }
    fclose(f);
    return mapa;
}

void AbrirRelatorio()
{
    wchar_t caminho[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", caminho, MAX_PATH)) return;
    wcscat_s(caminho, L"\\EnshroudedQuestPanel");
    CreateDirectoryW(caminho, nullptr);
    wcscat_s(caminho, L"\\rastreio.txt");
    _wfopen_s(&g_relatorio, caminho, L"w, ccs=UTF-8");
}

void Fim(int codigo)
{
    Diz(L"");
    Diz(L"  Relatorio em %%LOCALAPPDATA%%\\EnshroudedQuestPanel\\rastreio.txt");
    if (g_relatorio) fclose(g_relatorio);
    Diz(L"  (Enter para fechar)");
    getchar();
    exit(codigo);
}

}  // namespace

int wmain()
{
    AbrirRelatorio();
    Diz(L"");
    Diz(L"  EXPERIMENTO 2: a missao marcada, pela chave de texto");
    Diz(L"  ===================================================");
    Diz(L"  O jogo nao guarda o nome da missao como texto, e sim o numero que");
    Diz(L"  identifica esse texto. E esse numero que procuramos agora.");
    Diz(L"");

    auto chaves = CarregarChaves();
    if (chaves.empty()) {
        Diz(L"  Nao achei loca_chaves.txt ao lado deste programa.");
        Diz(L"  Gere com: python gerar_chaves.py \"<pasta do jogo>\"");
        Fim(1);
    }
    Diz(L"  %zu textos conhecidos do jogo carregados", chaves.size());

    const DWORD pid = AcharJogo();
    if (!pid) { Diz(L"  Abra o Enshrouded e entre no mundo primeiro."); Fim(1); }
    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc) { Diz(L"  Nao consegui abrir o processo (erro %lu).", GetLastError()); Fim(1); }
    Diz(L"  enshrouded.exe encontrado (pid %lu)", pid);
    Diz(L"");

    Diz(L"  PASSO 1  No jogo, marque uma missao como rota.");
    const std::wstring nomeA = Perguntar(L"  Nome dela, igual ao da tela: ");
    auto itA = chaves.find(nomeA);
    if (itA == chaves.end()) {
        Diz(L"  Esse texto nao esta na lista do jogo. Confira a grafia e os acentos.");
        Fim(1);
    }
    const std::uint32_t chaveA = itA->second;
    Diz(L"  chave de \"%s\" = %u (0x%08X)", nomeA.c_str(), chaveA, chaveA);

    auto regioes = Regioes(proc);
    Diz(L"  varrendo %zu regioes gravaveis...", regioes.size());
    auto candidatos = ProcurarU32(proc, regioes, chaveA, 2000000);
    Diz(L"  a chave aparece em %zu enderecos", candidatos.size());
    if (candidatos.empty()) {
        Diz(L"");
        Diz(L"  A chave tambem nao esta na memoria gravavel.");
        Diz(L"  O jogo deve usar outro identificador interno para a missao.");
        Fim(1);
    }
    Diz(L"");

    Diz(L"  PASSO 2  Agora marque OUTRA missao como rota (nao feche o jogo).");
    const std::wstring nomeB = Perguntar(L"  Nome da nova, igual ao da tela: ");
    auto itB = chaves.find(nomeB);
    if (itB == chaves.end() || nomeB == nomeA) {
        Diz(L"  Precisa ser outra missao, com o nome igual ao da tela.");
        Fim(1);
    }
    const std::uint32_t chaveB = itB->second;
    Diz(L"  chave de \"%s\" = %u (0x%08X)", nomeB.c_str(), chaveB, chaveB);
    Diz(L"");

    std::vector<std::uintptr_t> vencedores;
    for (auto endereco : candidatos) {
        std::uint32_t valor = 0;
        if (Ler(proc, endereco, &valor, 4) && valor == chaveB)
            vencedores.push_back(endereco);
    }

    if (vencedores.empty()) {
        Diz(L"  RESULTADO: nenhum dos %zu enderecos passou de A para B.", candidatos.size());
        Diz(L"  O campo da missao marcada nao e essa chave, ou vive em memoria que");
        Diz(L"  nao varremos. Aqui eu pararia: o custo ja passou do que o recurso vale.");
    } else {
        Diz(L"  RESULTADO: %zu endereco(s) seguem a missao marcada!", vencedores.size());
        Diz(L"");
        for (std::size_t i = 0; i < vencedores.size() && i < 20; ++i) {
            std::uintptr_t e = vencedores[i];
            Diz(L"    0x%llX", (unsigned long long)e);
        }
        Diz(L"");
        Diz(L"  Da para ler a missao da rota. Proximo passo: ver se esse endereco");
        Diz(L"  se mantem depois de fechar e abrir o jogo.");
    }
    CloseHandle(proc);
    Fim(0);
    return 0;
}
