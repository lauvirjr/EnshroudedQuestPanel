// Camada Vulkan que desenha o painel de missao por cima do jogo.
//
// Por que uma camada e nao um mod do Shroudtopia: o mod do minimapa remenda a
// dispatch table do device e, a cada quadro, confere se o slot de
// vkQueuePresentKHR ainda e o ponteiro original dele -- se nao for, desiste de
// desenhar. Dois mods disputando esse slot nao convivem. Uma camada fica abaixo
// do jogo na cadeia do loader: o minimapa continua vendo o ponteiro que espera,
// instala o gancho dele por cima do nosso e os dois aparecem.
//
// O desenho e so uma copia: interceptamos vkCreateSwapchainKHR para acrescentar
// VK_IMAGE_USAGE_TRANSFER_DST_BIT e, no present, copiamos o painel ja pronto
// (um VkBuffer visivel pela CPU) para dentro da imagem da swapchain.
#define VK_NO_PROTOTYPES
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

// As exportacoes ficam em questpanel.def: o header do Khronos ja declara
// vkNegotiateLoaderLayerInterfaceVersion sem dllexport, e marcar aqui daria
// conflito de vinculo.

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "panel_model.h"

namespace {

constexpr const char* kLayerName = "VK_LAYER_lauvas_quest_panel";

// ---------------------------------------------------------------- utilidades

std::mutex g_lock;

// Log em arquivo: dentro do jogo nao ha console, e sem isto um problema de
// desenho e indistinguivel de "a camada nem carregou".
void Log(const char* format, ...)
{
    static FILE* file = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        wchar_t* base = nullptr;
        std::size_t len = 0;
        if (_wdupenv_s(&base, &len, L"LOCALAPPDATA") == 0 && base) {
            std::wstring path = std::wstring(base) + L"\\EnshroudedQuestPanel";
            free(base);
            CreateDirectoryW(path.c_str(), nullptr);
            path += L"\\layer.log";
            // _SH_DENYNO: da para acompanhar o log com o jogo aberto
            file = _wfsopen(path.c_str(), L"w", _SH_DENYNO);
        }
    }
    if (!file) return;
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
}

// O loader entrega handles cujo primeiro campo aponta para a dispatch table;
// e esse ponteiro que identifica o objeto para a camada.
void* DispatchKey(void* handle) { return *reinterpret_cast<void**>(handle); }

bool HostProcessIsGame()
{
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"enshrouded.exe") == 0;
}

bool GameProcessForced()
{
    static const bool forced = GetEnvironmentVariableW(L"QUESTPANEL_FORCE", nullptr, 0) > 0;
    return forced;
}

bool GameProcess()
{
    // QUESTPANEL_FORCE=1 liga o desenho em qualquer processo; serve para o teste
    // de loader exercitar o caminho inteiro sem precisar abrir o jogo.
    static const bool yes = HostProcessIsGame() || GameProcessForced();
    return yes;
}

// ------------------------------------------------------------------ tabelas

struct InstanceTable {
    VkInstance instance = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
};

struct SwapchainInfo {
    std::vector<VkImage> images;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
};

struct FrameSlot {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore done = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool submitted = false;
};

struct DeviceTable {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    InstanceTable* instance = nullptr;

    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;
    PFN_vkDestroyDevice DestroyDevice = nullptr;
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
    PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
    PFN_vkCreateBuffer CreateBuffer = nullptr;
    PFN_vkDestroyBuffer DestroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory AllocateMemory = nullptr;
    PFN_vkFreeMemory FreeMemory = nullptr;
    PFN_vkBindBufferMemory BindBufferMemory = nullptr;
    PFN_vkMapMemory MapMemory = nullptr;
    PFN_vkUnmapMemory UnmapMemory = nullptr;
    PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges = nullptr;
    PFN_vkCreateCommandPool CreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
    PFN_vkResetCommandBuffer ResetCommandBuffer = nullptr;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage = nullptr;
    PFN_vkCreateSemaphore CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore = nullptr;
    PFN_vkCreateFence CreateFence = nullptr;
    PFN_vkDestroyFence DestroyFence = nullptr;
    PFN_vkWaitForFences WaitForFences = nullptr;
    PFN_vkResetFences ResetFences = nullptr;
    PFN_vkQueueSubmit QueueSubmit = nullptr;
    PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;

    std::map<VkSwapchainKHR, SwapchainInfo> swapchains;
    std::map<VkQueue, std::uint32_t> queueFamilies;
    std::vector<std::uint32_t> createdFamilies;   // reserva: ver FamilyForQueue

    // recursos do painel
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize bufferSize = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::uint32_t poolFamily = UINT32_MAX;
    std::vector<FrameSlot> frames;
    std::size_t nextFrame = 0;

    int panelWidth = 0, panelHeight = 0;
    bool panelReady = false;
};

std::map<void*, InstanceTable*> g_instances;
std::map<void*, DeviceTable*> g_devices;

InstanceTable* FindInstance(void* handle)
{
    auto it = g_instances.find(DispatchKey(handle));
    return it == g_instances.end() ? nullptr : it->second;
}

DeviceTable* FindDevice(void* handle)
{
    auto it = g_devices.find(DispatchKey(handle));
    return it == g_devices.end() ? nullptr : it->second;
}

// ------------------------------------------------------- estado do painel

struct PanelState {
    questpanel::Quest quest;
    questpanel::Raster raster;
    questpanel::FileStamp stamp;
    bool hasStamp = false;
    bool wanted = false;        // F5; comeca escondido e lembra do que ficou
    bool header = true;         // cabecalho ligado (F7)
    bool inWorld = false;       // dentro de um mundo, pelo log do jogo
    bool dirty = false;
    DWORD lastCheck = 0;
    DWORD lastWorldCheck = 0;
    bool redraw = false;        // F7/F9 mudaram: rasterizar sem reler o arquivo
    int lastWidth = 0;
    std::wstring logPath;

    // catalogo: todas as missoes, para escolher sem sair do jogo
    std::vector<questpanel::Quest> catalogo;
    questpanel::FileStamp catalogoStamp;
    bool temCatalogo = false;
    bool escolhendo = false;    // F6 abriu a lista
    bool mostrandoPelaLista = false;   // a lista forcou o painel a aparecer
    std::vector<questpanel::Quest> visiveis;   // catalogo sem as ja concluidas
    std::size_t cumpridosVistos = 0;           // para refazer so quando muda
    std::size_t escolha = 0;    // item em destaque na lista
    DWORD lastCatalogo = 0;
};

PanelState g_panel;


// --- teclado ---------------------------------------------------------------
// Sem isto as setas chegam ao jogo junto com a lista e o personagem sai
// andando. Nao da para "consumir" a tecla pela camada: ela so desenha. Entao
// usamos um gancho de teclado de baixo nivel, que ve a tecla antes do jogo e
// pode engoli-la. So engole as setas, so com a lista aberta e so com o jogo em
// primeiro plano -- fora disso o teclado segue intocado.
HMODULE g_modulo = nullptr;
HHOOK g_ganchoTeclado = nullptr;
HANDLE g_threadTeclado = nullptr;
DWORD g_threadTecladoId = 0;
std::atomic<int> g_navegacao{0};        // quanto andar na lista, somado pelo gancho
std::atomic<bool> g_confirmar{false};   // Enter com a lista aberta
std::atomic<bool> g_cancelar{false};    // Esc com a lista aberta
std::atomic<bool> g_listaAberta{false};

bool JogoEmFoco()
{
    const HWND janela = GetForegroundWindow();
    DWORD pid = 0;
    if (janela) GetWindowThreadProcessId(janela, &pid);
    return pid == GetCurrentProcessId();
}

bool TeclaDeLista(DWORD vk)
{
    return vk == VK_UP || vk == VK_DOWN || vk == VK_PRIOR || vk == VK_NEXT ||
           vk == VK_RETURN || vk == VK_ESCAPE;
}

// O Esc fecha a lista mas segue para o jogo. Engolido, ele deixava de fechar o
// inventario ou o mapa abertos por cima da lista -- e fora do mundo, com a
// lista aberta e invisivel, parecia que o Esc tinha parado de funcionar.
bool TeclaEngolida(DWORD vk)
{
    return TeclaDeLista(vk) && vk != VK_ESCAPE;
}

// Soma a tecla na navegacao da lista; chamado pelo desvio de GetRawInputData.
void ContarTecla(DWORD vk)
{
    switch (vk) {
        case VK_UP:     g_navegacao -= 1;  break;
        case VK_DOWN:   g_navegacao += 1;  break;
        case VK_PRIOR:  g_navegacao -= 10; break;
        case VK_NEXT:   g_navegacao += 10; break;
        case VK_RETURN: g_confirmar.store(true); break;
        case VK_ESCAPE: g_cancelar.store(true);  break;
        default: break;
    }
}

LRESULT CALLBACK ProcTeclado(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_listaAberta.load() && JogoEmFoco()) {
        const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (TeclaEngolida(info->vkCode)) {
            // So engole; quem conta e o desvio de GetRawInputData. Medido em
            // 29/10/2026: com o Enshrouded em foco este gancho nem e chamado,
            // entao contar aqui deixava a lista parada.
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

DWORD WINAPI ThreadTeclado(LPVOID)
{
    // um gancho de baixo nivel exige uma thread com fila de mensagens
    g_ganchoTeclado = SetWindowsHookExW(WH_KEYBOARD_LL, ProcTeclado, g_modulo, 0);
    if (!g_ganchoTeclado) {
        Log("gancho de teclado falhou (erro %lu): as setas vao vazar para o jogo",
            GetLastError());
        return 0;
    }
    Log("gancho de teclado instalado");
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { }
    UnhookWindowsHookEx(g_ganchoTeclado);
    g_ganchoTeclado = nullptr;
    return 0;
}

void GarantirTeclado()
{
    if (g_threadTeclado) return;
    g_threadTeclado = CreateThread(nullptr, 0, ThreadTeclado, nullptr, 0, &g_threadTecladoId);
}


// --- Raw Input -------------------------------------------------------------
// O gancho de teclado de baixo nivel engole a tecla da fila de mensagens, mas
// o Enshrouded le o teclado por Raw Input (importa GetRawInputData e
// RegisterRawInputDevices), e esse caminho vem do driver, sem passar por
// gancho nenhum. Resultado: a lista navegava e o personagem andava junto.
//
// Entao trocamos o ponteiro de GetRawInputData na tabela de importacao do
// executavel. Com a lista aberta, as setas viram "nenhuma tecla" antes de o
// jogo olhar. E API do Windows, nao codigo do jogo: nao depende de assinatura
// de bytes e nao quebra quando o Enshrouded atualizar.
using PFN_GetRawInputData = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
PFN_GetRawInputData g_origGetRawInputData = nullptr;

UINT WINAPI HookGetRawInputData(HRAWINPUT entrada, UINT comando, LPVOID dados,
                                PUINT tamanho, UINT cabecalho)
{
    const UINT r = g_origGetRawInputData(entrada, comando, dados, tamanho, cabecalho);
    if (!g_listaAberta.load() || dados == nullptr || comando != RID_INPUT ||
        r == static_cast<UINT>(-1))
        return r;
    auto* bruto = static_cast<RAWINPUT*>(dados);
    if (bruto->header.dwType != RIM_TYPEKEYBOARD) return r;
    const USHORT vk = bruto->data.keyboard.VKey;
    if (TeclaDeLista(vk)) {
        // A navegacao sai daqui: e o unico caminho que comprovadamente ve as
        // setas com o jogo em foco. A repeticao automatica do teclado tambem
        // chega como Raw Input, entao segurar a seta rola a lista.
        if (!(bruto->data.keyboard.Flags & RI_KEY_BREAK))      // so ao apertar
            ContarTecla(vk);
        if (TeclaEngolida(vk)) {
            // 0xFF e o codigo que o proprio Windows usa para "tecla sem mapeamento"
            bruto->data.keyboard.VKey = 0xFF;
            bruto->data.keyboard.MakeCode = 0;
            bruto->data.keyboard.Message = WM_NULL;
        }
    }
    return r;
}

// Troca a entrada de uma funcao na tabela de importacao do modulo.
bool TrocarNaIAT(HMODULE modulo, const char* dll, const char* funcao,
                 void* novo, void** antigo)
{
    auto* base = reinterpret_cast<std::uint8_t*>(modulo);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;

    auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; imp->Name; ++imp) {
        const char* nome = reinterpret_cast<const char*>(base + imp->Name);
        if (_stricmp(nome, dll) != 0) continue;
        auto* nomes = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto* enderecos = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; nomes->u1.AddressOfData; ++nomes, ++enderecos) {
            if (IMAGE_SNAP_BY_ORDINAL(nomes->u1.Ordinal)) continue;
            auto* importado = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + nomes->u1.AddressOfData);
            if (std::strcmp(importado->Name, funcao) != 0) continue;
            DWORD antes = 0;
            if (!VirtualProtect(&enderecos->u1.Function, sizeof(void*), PAGE_READWRITE, &antes))
                return false;
            if (antigo) *antigo = reinterpret_cast<void*>(enderecos->u1.Function);
            enderecos->u1.Function = reinterpret_cast<ULONG_PTR>(novo);
            VirtualProtect(&enderecos->u1.Function, sizeof(void*), antes, &antes);
            return true;
        }
    }
    return false;
}

void GarantirRawInput()
{
    static bool tentado = false;
    if (tentado) return;
    tentado = true;
    void* original = nullptr;
    if (TrocarNaIAT(GetModuleHandleW(nullptr), "USER32.dll", "GetRawInputData",
                    reinterpret_cast<void*>(&HookGetRawInputData), &original)) {
        g_origGetRawInputData = reinterpret_cast<PFN_GetRawInputData>(original);
        Log("GetRawInputData desviado: as setas param na lista");
    } else {
        Log("nao achei GetRawInputData na tabela de importacao; as setas vao vazar");
    }
}


// A preferencia de mostrar ou esconder o painel fica num arquivinho, para
// valer na proxima sessao. Sem arquivo, comeca escondido: quem nunca usou nao
// pediu para ter nada na tela.
std::wstring CaminhoPreferencia()
{
    wchar_t* base = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&base, &len, L"LOCALAPPDATA") != 0 || !base) return {};
    std::wstring caminho = std::wstring(base) + L"\\EnshroudedQuestPanel";
    free(base);
    CreateDirectoryW(caminho.c_str(), nullptr);
    return caminho + L"\\painel.cfg";
}

void CarregarPreferencia()
{
    const std::wstring caminho = CaminhoPreferencia();
    if (caminho.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, caminho.c_str(), L"r") != 0 || !f) return;
    char linha[64] = {};
    if (fgets(linha, sizeof(linha), f))
        g_panel.wanted = (std::atoi(linha) != 0);
    fclose(f);
    Log("preferencia: painel comeca %s", g_panel.wanted ? "visivel" : "escondido");
}

void GravarPreferencia()
{
    const std::wstring caminho = CaminhoPreferencia();
    if (caminho.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, caminho.c_str(), L"w") != 0 || !f) return;
    std::fputs(g_panel.wanted ? "1" : "0", f);
    fclose(f);
}


// --- missoes concluidas durante o jogo -------------------------------------
std::mutex g_concluidasLock;
std::vector<std::uint32_t> g_idsProcurados;      // todos os ids do catalogo
std::unordered_set<std::uint32_t> g_idsCumpridos;  // os que ja apareceram
std::vector<std::pair<std::uintptr_t, std::size_t>> g_regioesQuentes;
std::atomic<bool> g_varrendo{false};
std::atomic<unsigned> g_passadas{0};
HANDLE g_threadVarredura = nullptr;


// Os ids ja vistos ficam em disco: assim a lista abre filtrada na sessao
// seguinte, sem esperar a varredura rodar de novo.
std::wstring CaminhoConcluidas()
{
    wchar_t* base = nullptr;
    std::size_t len = 0;
    if (_wdupenv_s(&base, &len, L"LOCALAPPDATA") != 0 || !base) return {};
    std::wstring caminho = std::wstring(base) + L"\\EnshroudedQuestPanel";
    free(base);
    CreateDirectoryW(caminho.c_str(), nullptr);
    return caminho + L"\\concluidas.txt";
}

void CarregarConcluidas()
{
    const std::wstring caminho = CaminhoConcluidas();
    if (caminho.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, caminho.c_str(), L"r") != 0 || !f) return;
    char linha[64];
    std::size_t n = 0;
    {
        std::lock_guard<std::mutex> guard(g_concluidasLock);
        while (fgets(linha, sizeof(linha), f)) {
            const unsigned long v = std::strtoul(linha, nullptr, 10);
            if (v) { g_idsCumpridos.insert(static_cast<std::uint32_t>(v)); ++n; }
        }
    }
    fclose(f);
    if (n) Log("concluidas lembradas da sessao anterior: %zu ids", n);
}

void GravarConcluidas()
{
    const std::wstring caminho = CaminhoConcluidas();
    if (caminho.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, caminho.c_str(), L"w") != 0 || !f) return;
    {
        std::lock_guard<std::mutex> guard(g_concluidasLock);
        for (auto id : g_idsCumpridos) std::fprintf(f, "%u\n", id);
    }
    fclose(f);
}

bool RegiaoVale(const MEMORY_BASIC_INFORMATION& info)
{
    return info.State == MEM_COMMIT &&
           !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
           (info.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
           info.RegionSize < (1ull << 31);
}

// Procura os ids numa regiao. Devolve true se achou algum (regiao "quente").
bool VarrerRegiao(std::uintptr_t base, std::size_t tamanho,
                  const std::unordered_set<std::uint32_t>& procurados,
                  std::unordered_set<std::uint32_t>& achados,
                  std::vector<unsigned char>& buffer)
{
    bool quente = false;
    HANDLE eu = GetCurrentProcess();
    for (std::size_t off = 0; off < tamanho; off += buffer.size() - 4) {
        const std::size_t pedir = (std::min)(buffer.size(), tamanho - off);
        if (pedir < 4) break;
        SIZE_T lido = 0;
        // ReadProcessMemory mesmo sendo o proprio processo: nao estoura se a
        // pagina sumir entre o VirtualQuery e a leitura
        if (!ReadProcessMemory(eu, reinterpret_cast<LPCVOID>(base + off),
                               buffer.data(), pedir, &lido) || lido < 4)
            continue;
        for (std::size_t i = 0; i + 4 <= lido; i += 4) {
            std::uint32_t v;
            std::memcpy(&v, buffer.data() + i, 4);
            if (v && procurados.count(v)) { achados.insert(v); quente = true; }
        }
    }
    return quente;
}

DWORD WINAPI ThreadVarredura(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
    std::vector<unsigned char> buffer(1u << 20);
    for (;;) {
        std::unordered_set<std::uint32_t> procurados;
        std::vector<std::pair<std::uintptr_t, std::size_t>> quentes;
        {
            std::lock_guard<std::mutex> guard(g_concluidasLock);
            procurados.insert(g_idsProcurados.begin(), g_idsProcurados.end());
            quentes = g_regioesQuentes;
        }
        if (procurados.empty()) { Sleep(5000); continue; }

        std::unordered_set<std::uint32_t> achados;
        const unsigned passada = g_passadas.load();
        const bool completa = quentes.empty() || (passada % 20 == 0);

        if (completa) {
            // passada cara: percorre tudo e anota onde os ids moram
            quentes.clear();
            MEMORY_BASIC_INFORMATION info{};
            std::uintptr_t endereco = 0;
            while (VirtualQuery(reinterpret_cast<LPCVOID>(endereco), &info, sizeof(info)) == sizeof(info)) {
                const auto proximo = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
                if (RegiaoVale(info)) {
                    const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
                    if (VarrerRegiao(base, info.RegionSize, procurados, achados, buffer))
                        quentes.emplace_back(base, info.RegionSize);
                    Sleep(1);     // devolve a CPU: o jogo vem primeiro
                }
                if (proximo <= endereco) break;
                endereco = proximo;
            }
        } else {
            for (const auto& [base, tam] : quentes) {
                MEMORY_BASIC_INFORMATION info{};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(base), &info, sizeof(info)) != sizeof(info) ||
                    !RegiaoVale(info))
                    continue;
                VarrerRegiao(base, (std::min)(tam, info.RegionSize), procurados, achados, buffer);
            }
        }

        bool mudou = false;
        {
            std::lock_guard<std::mutex> guard(g_concluidasLock);
            const std::size_t antes = g_idsCumpridos.size();
            g_idsCumpridos.insert(achados.begin(), achados.end());
            if (completa) g_regioesQuentes = quentes;
            mudou = g_idsCumpridos.size() != antes;
            if (mudou)
                Log("conhecimento visto na memoria: %zu ids (%s)",
                    g_idsCumpridos.size(), completa ? "passada completa" : "regioes conhecidas");
        }
        if (mudou) GravarConcluidas();
        g_passadas.fetch_add(1);
        Sleep(completa ? 30000 : 8000);
    }
    return 0;
}

void AtualizarVisiveis();

// Uma missao sai da lista quando TODOS os ids de conclusao dela apareceram.
bool MissaoConcluida(const questpanel::Quest& q)
{
    if (q.completionIds.empty()) return false;
    std::lock_guard<std::mutex> guard(g_concluidasLock);
    if (g_idsCumpridos.empty()) return false;
    for (auto id : q.completionIds)
        if (!g_idsCumpridos.count(id)) return false;
    return true;
}

// Refaz a lista visivel so quando a varredura encontrou algo novo.
void AtualizarVisiveis()
{
    std::size_t agora;
    {
        std::lock_guard<std::mutex> guard(g_concluidasLock);
        agora = g_idsCumpridos.size();
    }
    if (!g_panel.visiveis.empty() && agora == g_panel.cumpridosVistos) return;
    g_panel.cumpridosVistos = agora;
    std::vector<questpanel::Quest> fora;
    fora.reserve(g_panel.catalogo.size());
    std::size_t tiradas = 0;
    for (const auto& q : g_panel.catalogo) {
        if (MissaoConcluida(q)) { ++tiradas; continue; }
        fora.push_back(q);
    }
    if (fora.empty()) return;        // nunca deixa a lista vazia
    g_panel.visiveis.swap(fora);
    if (tiradas)
        Log("lista: %zu missoes (%zu ja concluidas saíram)", g_panel.visiveis.size(), tiradas);
}

void GarantirVarredura()
{
    if (g_threadVarredura) return;
    g_threadVarredura = CreateThread(nullptr, 0, ThreadVarredura, nullptr, 0, nullptr);
}

// Verdadeiro so no quadro em que a tecla foi apertada. Sem isto, segurar a
// tecla dispara a acao a cada passagem e o painel pisca.
bool Apertou(int vk, bool& anterior)
{
    const bool agora = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool borda = agora && !anterior;
    anterior = agora;
    return borda;
}

void PollHotkey()
{
    static bool antesF5 = false, antesF6 = false, antesEnter = false, antesEsc = false;
    // Duas teclas, so. F6 abre a lista e confirma; F5 mostra e esconde o painel.
    // F7..F11 ficaram para o minimapa Shroudweave, e no Enshrouded o Enter abre
    // o chat, o Esc abre o menu e o F10 abre o menu principal.
    const bool f5 = Apertou(VK_F5, antesF5);
    // F6, e nao F9: o minimapa usa F9 para diminuir o proprio tamanho
    const bool f6 = Apertou(VK_F6, antesF6);
    const int passos = g_navegacao.exchange(0);     // o desvio acumulou

    // Enter e Esc vem do desvio de GetRawInputData. Onde ele nao instalou,
    // valem pelo teclado comum: ai tambem chegam ao jogo, mas a lista responde.
    const bool semDesvio = g_origGetRawInputData == nullptr;
    const bool enterTeclado = Apertou(VK_RETURN, antesEnter);
    const bool escTeclado = Apertou(VK_ESCAPE, antesEsc);
    const bool enter = g_confirmar.exchange(false) || (semDesvio && enterTeclado);
    const bool esc = g_cancelar.exchange(false) || (semDesvio && escTeclado);

    const bool navegando = g_panel.escolhendo && (passos != 0 || enter || esc);
    if (!f5 && !f6 && !navegando) return;

    const HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    if (pid != GetCurrentProcessId()) return;

    const auto pedirRedesenho = []() {
        g_panel.redraw = true;
        g_panel.dirty = true;
        g_panel.lastCheck = 0;
    };

    // Fixar a escolha do jogador: o arquivo do editor continua valendo, mas so
    // quando mudar de verdade. Zerar hasStamp mandava reler o arquivo no ciclo
    // seguinte, e era isso que apagava a missao recem-escolhida.
    const auto fixarEscolha = []() {
        const auto& lista = g_panel.visiveis.empty() ? g_panel.catalogo : g_panel.visiveis;
        if (g_panel.escolha < lista.size()) g_panel.quest = lista[g_panel.escolha];
        questpanel::FileStamp marca;
        if (questpanel::ReadStamp(questpanel::PanelFilePath(), marca)) {
            g_panel.stamp = marca;
            g_panel.hasStamp = true;
        }
    };

    if (f6) {
        if (!g_panel.temCatalogo) {
            Log("F6: sem catalogo. No editor: aba Missoes -> Enviar lista para o jogo");
            return;
        }
        if (g_panel.escolhendo) {
            // F6 tambem confirma: no Enshrouded o Enter abre o chat e o Esc abre
            // o menu, entao obrigar o jogador a usa-los seria pedir encrenca.
            g_panel.escolhendo = false;
            if (g_panel.mostrandoPelaLista) {
                // escolher com o painel escondido: mostra o que foi escolhido,
                // e passa a valer como preferencia
                g_panel.mostrandoPelaLista = false;
                GravarPreferencia();
            }
            fixarEscolha();
            Log("missao escolhida: %zu de %zu", g_panel.escolha + 1, g_panel.catalogo.size());
        } else {
            g_panel.escolhendo = true;
            // a lista aparece mesmo com o painel escondido, mas isso nao muda
            // a preferencia: ao escolher, o painel volta ao que o jogador quer
            g_panel.mostrandoPelaLista = !g_panel.wanted;
            if (!g_panel.wanted) g_panel.wanted = true;
            Log("F6: lista aberta (%zu missoes)", g_panel.catalogo.size());
        }
        g_listaAberta.store(g_panel.escolhendo);
        g_navegacao.store(0);
        g_confirmar.store(false);
        g_cancelar.store(false);
        pedirRedesenho();
        return;
    }
    if (g_panel.escolhendo) {
        const std::size_t total = g_panel.visiveis.empty()
                                ? g_panel.catalogo.size() : g_panel.visiveis.size();
        if (esc || f5) {
            g_panel.escolhendo = false;
            g_listaAberta.store(false);
            if (g_panel.mostrandoPelaLista) {   // cancelou: volta a esconder
                g_panel.mostrandoPelaLista = false;
                g_panel.wanted = false;
            }
            if (f5 && g_panel.wanted) {         // F5 esconde, com ou sem lista
                g_panel.wanted = false;
                GravarPreferencia();
            }
            if (f5) Log("F5: lista fechada, painel escondido");
            pedirRedesenho();
            return;
        }
        if (enter) {                       // mesmo efeito do F6
            g_panel.escolhendo = false;
            g_listaAberta.store(false);
            fixarEscolha();
            pedirRedesenho();
            Log("missao escolhida (Enter): %zu de %zu", g_panel.escolha + 1, total);
            return;
        }
        if (passos != 0) {
            const long long atual = static_cast<long long>(g_panel.escolha) + passos;
            const long long n = static_cast<long long>(total);
            g_panel.escolha = static_cast<std::size_t>(((atual % n) + n) % n);   // da a volta
            pedirRedesenho();
        }
        return;
    }
    if (f5) {
        g_panel.wanted = !g_panel.wanted;
        GravarPreferencia();
        Log("F5: painel %s", g_panel.wanted ? "ligado" : "escondido");
    }
}

// O painel so vale dentro de um mundo; no menu principal ele atrapalha.
void RefreshWorldState()
{
    const DWORD now = GetTickCount();
    if (g_panel.lastWorldCheck != 0 && now - g_panel.lastWorldCheck < 1500) return;
    g_panel.lastWorldCheck = now;
    if (g_panel.logPath.empty()) g_panel.logPath = questpanel::GameLogPath();
    const bool before = g_panel.inWorld;
    // fora do jogo (teste) nao ha log de sessao: QUESTPANEL_FORCE faz as vezes dele
    g_panel.inWorld = GameProcessForced() ? true
                                          : questpanel::PlayerInWorld(g_panel.logPath, before);
    if (before != g_panel.inWorld)
        Log("estado: %s", g_panel.inWorld ? "dentro do mundo (painel pode aparecer)"
                                          : "no menu (painel escondido)");
}

// Recarrega o arquivo no maximo uma vez por segundo.
void RefreshCatalogo();

// O catalogo e grande e muda pouco: basta conferir de tempos em tempos.
void RefreshCatalogo()
{
    const DWORD now = GetTickCount();
    if (g_panel.lastCatalogo != 0 && now - g_panel.lastCatalogo < 5000) return;
    g_panel.lastCatalogo = now;
    const std::wstring caminho = questpanel::CatalogFilePath();
    questpanel::FileStamp stamp;
    if (!questpanel::ReadStamp(caminho, stamp)) return;
    if (g_panel.temCatalogo && stamp == g_panel.catalogoStamp) return;
    if (questpanel::ParseCatalogFile(caminho, g_panel.catalogo)) {
        g_panel.catalogoStamp = stamp;
        g_panel.temCatalogo = true;
        if (g_panel.escolha >= g_panel.catalogo.size()) g_panel.escolha = 0;
        std::size_t comId = 0;
        {
            std::lock_guard<std::mutex> guard(g_concluidasLock);
            g_idsProcurados.clear();
            for (const auto& q : g_panel.catalogo) {
                if (!q.completionIds.empty()) ++comId;
                g_idsProcurados.insert(g_idsProcurados.end(),
                                       q.completionIds.begin(), q.completionIds.end());
            }
            g_regioesQuentes.clear();      // catalogo novo, procura tudo de novo
        }
        Log("catalogo carregado: %zu missoes (%zu com id de conclusao)",
            g_panel.catalogo.size(), comId);
    }
}

void RefreshPanel(int maxWidth)
{
    const DWORD now = GetTickCount();
    RefreshCatalogo();
    if (g_panel.escolhendo) {
        if (g_panel.redraw || g_panel.lastWidth != maxWidth) {
            g_panel.redraw = false;
            AtualizarVisiveis();
            const auto& lista = g_panel.visiveis.empty() ? g_panel.catalogo : g_panel.visiveis;
            const std::size_t onde = (std::min)(g_panel.escolha, lista.size() - 1);
            questpanel::RasterizeSelector(lista, onde, maxWidth, 96, g_panel.raster);
            g_panel.lastWidth = maxWidth;
            g_panel.dirty = true;
        }
        return;                      // com a lista aberta, o painel nao aparece
    }
    if (g_panel.redraw && !g_panel.quest.empty()) {
        g_panel.redraw = false;
        questpanel::RasterizeQuest(g_panel.quest, maxWidth, 96, g_panel.raster, g_panel.header);
        g_panel.lastWidth = maxWidth;
        g_panel.dirty = true;
        return;
    }
    if (now - g_panel.lastCheck < 1000) return;
    g_panel.lastCheck = now;

    const std::wstring path = questpanel::PanelFilePath();
    questpanel::FileStamp stamp;
    if (!questpanel::ReadStamp(path, stamp)) {
        // So apaga se a missao veio do arquivo e ele sumiu. Quem instala so o
        // pacote nao tem editor nem active.txt: a missao escolhida pela lista
        // era apagada no segundo seguinte e o painel abria e fechava.
        if (g_panel.hasStamp && !g_panel.quest.empty()) {
            g_panel.quest = questpanel::Quest{};
            g_panel.raster.clear();
            g_panel.dirty = true;
        }
        g_panel.hasStamp = false;
        return;
    }
    if (g_panel.hasStamp && stamp == g_panel.stamp) return;

    questpanel::Quest quest;
    if (!questpanel::ParsePanelFile(path, quest)) return;
    g_panel.stamp = stamp;
    g_panel.hasStamp = true;
    g_panel.quest = std::move(quest);
    questpanel::RasterizeQuest(g_panel.quest, maxWidth, 96, g_panel.raster, g_panel.header);
    g_panel.lastWidth = maxWidth;
    g_panel.dirty = true;
    Log("painel recarregado: %d objetivos, %dx%d",
        (int)g_panel.quest.objectives.size(), g_panel.raster.width, g_panel.raster.height);
}

// ------------------------------------------------------- recursos por device

std::uint32_t PickMemoryType(DeviceTable* table, std::uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties props{};
    table->instance->GetPhysicalDeviceMemoryProperties(table->physical, &props);
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}

void ReleasePanelBuffer(DeviceTable* table)
{
    if (table->mapped) { table->UnmapMemory(table->device, table->memory); table->mapped = nullptr; }
    if (table->buffer) { table->DestroyBuffer(table->device, table->buffer, nullptr); table->buffer = VK_NULL_HANDLE; }
    if (table->memory) { table->FreeMemory(table->device, table->memory, nullptr); table->memory = VK_NULL_HANDLE; }
    table->bufferSize = 0;
    table->panelReady = false;
}

// Sobe os pixels do painel para um buffer visivel pela CPU, trocando R e B
// quando a swapchain nao usa a ordem BGRA do GDI.
bool UploadPanel(DeviceTable* table, VkFormat swapFormat)
{
    const auto& raster = g_panel.raster;
    if (raster.empty()) { table->panelReady = false; return false; }

    const VkDeviceSize needed = raster.pixels.size();
    if (needed > table->bufferSize) {
        ReleasePanelBuffer(table);
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = needed;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (table->CreateBuffer(table->device, &info, nullptr, &table->buffer) != VK_SUCCESS)
            return false;
        VkMemoryRequirements req{};
        table->GetBufferMemoryRequirements(table->device, table->buffer, &req);
        const std::uint32_t type = PickMemoryType(table, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (type == UINT32_MAX) { ReleasePanelBuffer(table); return false; }
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = type;
        if (table->AllocateMemory(table->device, &alloc, nullptr, &table->memory) != VK_SUCCESS ||
            table->BindBufferMemory(table->device, table->buffer, table->memory, 0) != VK_SUCCESS ||
            table->MapMemory(table->device, table->memory, 0, VK_WHOLE_SIZE, 0, &table->mapped) != VK_SUCCESS) {
            ReleasePanelBuffer(table);
            return false;
        }
        table->bufferSize = req.size;
    }

    const bool swapRB = (swapFormat == VK_FORMAT_R8G8B8A8_UNORM || swapFormat == VK_FORMAT_R8G8B8A8_SRGB);
    auto* out = static_cast<std::uint8_t*>(table->mapped);
    if (swapRB) {
        for (std::size_t i = 0; i < raster.pixels.size(); i += 4) {
            out[i + 0] = raster.pixels[i + 2];
            out[i + 1] = raster.pixels[i + 1];
            out[i + 2] = raster.pixels[i + 0];
            out[i + 3] = raster.pixels[i + 3];
        }
    } else {
        std::memcpy(out, raster.pixels.data(), raster.pixels.size());
    }
    table->panelWidth = raster.width;
    table->panelHeight = raster.height;
    table->panelReady = true;
    return true;
}

bool EnsureFrameResources(DeviceTable* table, std::uint32_t family, std::size_t count)
{
    if (table->pool != VK_NULL_HANDLE && table->poolFamily != family) {
        table->DeviceWaitIdle(table->device);
        for (auto& slot : table->frames) {
            if (slot.done) table->DestroySemaphore(table->device, slot.done, nullptr);
            if (slot.fence) table->DestroyFence(table->device, slot.fence, nullptr);
        }
        table->frames.clear();
        table->DestroyCommandPool(table->device, table->pool, nullptr);
        table->pool = VK_NULL_HANDLE;
    }
    if (table->pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        info.queueFamilyIndex = family;
        if (table->CreateCommandPool(table->device, &info, nullptr, &table->pool) != VK_SUCCESS)
            return false;
        table->poolFamily = family;
    }
    while (table->frames.size() < count) {
        FrameSlot slot;
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = table->pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (table->AllocateCommandBuffers(table->device, &alloc, &slot.commandBuffer) != VK_SUCCESS)
            return false;
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (table->CreateSemaphore(table->device, &sem, nullptr, &slot.done) != VK_SUCCESS)
            return false;
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (table->CreateFence(table->device, &fence, nullptr, &slot.fence) != VK_SUCCESS)
            return false;
        table->frames.push_back(slot);
    }
    return true;
}

// O loader costuma resolver vkGetDeviceQueue pelo proprio trampolim, sem passar
// pela camada, entao o mapa queue->familia pode ficar vazio. Nesse caso vale a
// familia que o device pediu na criacao -- jogos praticamente sempre usam uma so.
std::uint32_t FamilyForQueue(DeviceTable* table, VkQueue queue)
{
    auto found = table->queueFamilies.find(queue);
    if (found != table->queueFamilies.end()) return found->second;
    if (table->createdFamilies.size() == 1) return table->createdFamilies[0];
    return UINT32_MAX;
}

void Barrier(DeviceTable* table, VkCommandBuffer cmd, VkImage image,
             VkImageLayout from, VkImageLayout to,
             VkAccessFlags srcAccess, VkAccessFlags dstAccess,
             VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    table->CmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

}  // namespace

// --------------------------------------------------------------- intercepcao

extern "C" {

static VKAPI_ATTR VkResult VKAPI_CALL QuestPanel_CreateSwapchainKHR(
    VkDevice device, const VkSwapchainCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain)
{
    DeviceTable* table = nullptr;
    { std::lock_guard<std::mutex> guard(g_lock); table = FindDevice(device); }
    if (!table) return VK_ERROR_INITIALIZATION_FAILED;

    // Sem TRANSFER_DST nao da para copiar o painel para dentro da imagem.
    VkSwapchainCreateInfoKHR patched = *info;
    patched.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkResult result = table->CreateSwapchainKHR(device, &patched, allocator, swapchain);
    if (result != VK_SUCCESS)         // driver recusou o uso extra: segue sem painel
        result = table->CreateSwapchainKHR(device, info, allocator, swapchain);
    if (result != VK_SUCCESS) return result;

    SwapchainInfo entry;
    entry.format = info->imageFormat;
    entry.extent = info->imageExtent;
    std::uint32_t count = 0;
    if (table->GetSwapchainImagesKHR(device, *swapchain, &count, nullptr) == VK_SUCCESS && count) {
        entry.images.resize(count);
        table->GetSwapchainImagesKHR(device, *swapchain, &count, entry.images.data());
    }
    Log("swapchain %ux%u formato %d, %u imagens, TRANSFER_DST %s",
        entry.extent.width, entry.extent.height, (int)entry.format, count,
        (patched.imageUsage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ? "sim" : "nao");
    std::lock_guard<std::mutex> guard(g_lock);
    table->swapchains[*swapchain] = std::move(entry);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL QuestPanel_DestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* allocator)
{
    DeviceTable* table = nullptr;
    { std::lock_guard<std::mutex> guard(g_lock); table = FindDevice(device); }
    if (!table) return;
    { std::lock_guard<std::mutex> guard(g_lock); table->swapchains.erase(swapchain); }
    table->DestroySwapchainKHR(device, swapchain, allocator);
}

static VKAPI_ATTR void VKAPI_CALL QuestPanel_GetDeviceQueue(
    VkDevice device, std::uint32_t family, std::uint32_t index, VkQueue* queue)
{
    DeviceTable* table = nullptr;
    { std::lock_guard<std::mutex> guard(g_lock); table = FindDevice(device); }
    if (!table) return;
    table->GetDeviceQueue(device, family, index, queue);
    if (queue && *queue) {
        std::lock_guard<std::mutex> guard(g_lock);
        table->queueFamilies[*queue] = family;     // o present precisa da familia
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL QuestPanel_QueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR* presentInfo)
{
    DeviceTable* table = nullptr;
    { std::lock_guard<std::mutex> guard(g_lock); table = FindDevice(queue); }
    if (!table) return VK_ERROR_INITIALIZATION_FAILED;

    std::lock_guard<std::mutex> guard(g_lock);
    GarantirTeclado();
    GarantirRawInput();
    RefreshCatalogo();       // ja no menu: ao entrar no mundo a lista esta pronta
    static bool leuPreferencia = false;
    if (!leuPreferencia) {
        leuPreferencia = true;
        CarregarPreferencia();
        CarregarConcluidas();
    }
    GarantirVarredura();
    PollHotkey();

    RefreshWorldState();
    if (g_panel.escolhendo && !g_panel.inWorld) {
        // Fora do mundo a lista nao aparece; aberta, seguiria engolindo as
        // setas e o Enter dos menus sem o jogador ver por que.
        g_panel.escolhendo = false;
        g_listaAberta.store(false);
        if (g_panel.mostrandoPelaLista) {
            g_panel.mostrandoPelaLista = false;
            g_panel.wanted = false;
        }
        g_panel.redraw = true;
        Log("saiu do mundo: lista fechada");
    }
    const bool drawable = g_panel.wanted && g_panel.inWorld && presentInfo->swapchainCount > 0;
    if (!drawable) return table->QueuePresentKHR(queue, presentInfo);

    auto found = table->swapchains.find(presentInfo->pSwapchains[0]);
    if (found == table->swapchains.end() || found->second.images.empty())
        return table->QueuePresentKHR(queue, presentInfo);
    const SwapchainInfo& chain = found->second;

    int wanted = (std::max)(320, (std::min)(static_cast<int>(chain.extent.width) / 4, 560));
    if (g_panel.escolhendo) wanted = (std::max)(wanted, (std::min)(static_cast<int>(chain.extent.width) / 3, 680));
    RefreshPanel(wanted);
    if (g_panel.raster.empty()) return table->QueuePresentKHR(queue, presentInfo);
    if (g_panel.dirty || !table->panelReady) {
        if (!UploadPanel(table, chain.format)) return table->QueuePresentKHR(queue, presentInfo);
        g_panel.dirty = false;
    }

    const std::uint32_t family = FamilyForQueue(table, queue);
    if (family == UINT32_MAX) {
        static bool warned = false;
        if (!warned) { warned = true; Log("sem familia para a fila: painel desligado"); }
        return table->QueuePresentKHR(queue, presentInfo);
    }
    if (!EnsureFrameResources(table, family, chain.images.size()))
        return table->QueuePresentKHR(queue, presentInfo);

    const std::uint32_t imageIndex = presentInfo->pImageIndices[0];
    if (imageIndex >= chain.images.size()) return table->QueuePresentKHR(queue, presentInfo);
    FrameSlot& slot = table->frames[table->nextFrame % table->frames.size()];
    table->nextFrame++;

    if (slot.submitted) {
        table->WaitForFences(table->device, 1, &slot.fence, VK_TRUE, 50ull * 1000 * 1000);
        table->ResetFences(table->device, 1, &slot.fence);
    }
    table->ResetCommandBuffer(slot.commandBuffer, 0);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (table->BeginCommandBuffer(slot.commandBuffer, &begin) != VK_SUCCESS)
        return table->QueuePresentKHR(queue, presentInfo);

    VkImage target = chain.images[imageIndex];
    Barrier(table, slot.commandBuffer, target,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // encostado na esquerda e centrado na vertical
    const std::int32_t margin = 24;
    const std::int32_t width = (std::min)(table->panelWidth, static_cast<int>(chain.extent.width) - margin * 2);
    const std::int32_t height = (std::min)(table->panelHeight, static_cast<int>(chain.extent.height) - margin * 2);
    const std::int32_t top = (std::max)(margin, (static_cast<std::int32_t>(chain.extent.height) - height) / 2);
    if (width > 0 && height > 0) {
        VkBufferImageCopy copy{};
        copy.bufferRowLength = static_cast<std::uint32_t>(table->panelWidth);
        copy.bufferImageHeight = static_cast<std::uint32_t>(table->panelHeight);
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {margin, top, 0};
        copy.imageExtent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        table->CmdCopyBufferToImage(slot.commandBuffer, table->buffer, target,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }

    Barrier(table, slot.commandBuffer, target,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    table->EndCommandBuffer(slot.commandBuffer);

    // Esperamos os semaforos que o jogo entregaria ao present e sinalizamos o
    // nosso; o present original passa a esperar so por ele.
    std::vector<VkPipelineStageFlags> stages(presentInfo->waitSemaphoreCount,
                                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = presentInfo->waitSemaphoreCount;
    submit.pWaitSemaphores = presentInfo->pWaitSemaphores;
    submit.pWaitDstStageMask = stages.empty() ? nullptr : stages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot.commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &slot.done;
    if (table->QueueSubmit(queue, 1, &submit, slot.fence) != VK_SUCCESS) {
        slot.submitted = false;
        return table->QueuePresentKHR(queue, presentInfo);
    }
    slot.submitted = true;

    static unsigned drawn = 0;
    if (++drawn <= 3 || drawn % 600 == 0)
        Log("painel copiado no quadro %u (imagem %u, %dx%d)", drawn, imageIndex,
            table->panelWidth, table->panelHeight);

    VkPresentInfoKHR patched = *presentInfo;
    patched.waitSemaphoreCount = 1;
    patched.pWaitSemaphores = &slot.done;
    return table->QueuePresentKHR(queue, &patched);
}

// ------------------------------------------------- criacao de instance/device

static VKAPI_ATTR VkResult VKAPI_CALL QuestPanel_CreateInstance(
    const VkInstanceCreateInfo* info, const VkAllocationCallbacks* allocator, VkInstance* instance)
{
    auto* link = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                     link->function == VK_LAYER_LINK_INFO))
        link = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(link->pNext));
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;      // cede a vez a proxima camada

    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(nextGipa(nullptr, "vkCreateInstance"));
    if (!createInstance) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult result = createInstance(info, allocator, instance);
    if (result != VK_SUCCESS) return result;

    auto* table = new InstanceTable();
    table->instance = *instance;
    table->GetInstanceProcAddr = nextGipa;
    table->DestroyInstance =
        reinterpret_cast<PFN_vkDestroyInstance>(nextGipa(*instance, "vkDestroyInstance"));
    table->GetPhysicalDeviceMemoryProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            nextGipa(*instance, "vkGetPhysicalDeviceMemoryProperties"));
    std::lock_guard<std::mutex> guard(g_lock);
    g_instances[DispatchKey(*instance)] = table;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL QuestPanel_DestroyInstance(
    VkInstance instance, const VkAllocationCallbacks* allocator)
{
    InstanceTable* table = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_instances.find(DispatchKey(instance));
        if (it == g_instances.end()) return;
        table = it->second;
        g_instances.erase(it);
    }
    table->DestroyInstance(instance, allocator);
    delete table;
}

static VKAPI_ATTR VkResult VKAPI_CALL QuestPanel_CreateDevice(
    VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkDevice* device)
{
    auto* link = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                     link->function == VK_LAYER_LINK_INFO))
        link = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(link->pNext));
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr nextGipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr nextGdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;

    InstanceTable* parent = nullptr;
    { std::lock_guard<std::mutex> guard(g_lock); parent = FindInstance(physical); }

    auto createDevice = reinterpret_cast<PFN_vkCreateDevice>(nextGipa(nullptr, "vkCreateDevice"));
    if (!createDevice) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult result = createDevice(physical, info, allocator, device);
    if (result != VK_SUCCESS) return result;

    auto* table = new DeviceTable();
    for (std::uint32_t i = 0; i < info->queueCreateInfoCount; ++i)
        table->createdFamilies.push_back(info->pQueueCreateInfos[i].queueFamilyIndex);
    table->device = *device;
    table->physical = physical;
    table->instance = parent;
    table->GetDeviceProcAddr = nextGdpa;

    const auto load = [&](const char* name) { return nextGdpa(*device, name); };
#define QP_LOAD(field) table->field = reinterpret_cast<PFN_vk##field>(load("vk" #field))
    QP_LOAD(DestroyDevice);          QP_LOAD(CreateSwapchainKHR);
    QP_LOAD(DestroySwapchainKHR);    QP_LOAD(GetSwapchainImagesKHR);
    QP_LOAD(QueuePresentKHR);        QP_LOAD(GetDeviceQueue);
    QP_LOAD(CreateBuffer);           QP_LOAD(DestroyBuffer);
    QP_LOAD(GetBufferMemoryRequirements);
    QP_LOAD(AllocateMemory);         QP_LOAD(FreeMemory);
    QP_LOAD(BindBufferMemory);       QP_LOAD(MapMemory);
    QP_LOAD(UnmapMemory);            QP_LOAD(FlushMappedMemoryRanges);
    QP_LOAD(CreateCommandPool);      QP_LOAD(DestroyCommandPool);
    QP_LOAD(AllocateCommandBuffers); QP_LOAD(BeginCommandBuffer);
    QP_LOAD(EndCommandBuffer);       QP_LOAD(ResetCommandBuffer);
    QP_LOAD(CmdPipelineBarrier);     QP_LOAD(CmdCopyBufferToImage);
    QP_LOAD(CreateSemaphore);        QP_LOAD(DestroySemaphore);
    QP_LOAD(CreateFence);            QP_LOAD(DestroyFence);
    QP_LOAD(WaitForFences);          QP_LOAD(ResetFences);
    QP_LOAD(QueueSubmit);            QP_LOAD(DeviceWaitIdle);
#undef QP_LOAD

    Log("device criado; familias pedidas: %zu; present=%p",
        table->createdFamilies.size(), (void*)table->QueuePresentKHR);
    std::lock_guard<std::mutex> guard(g_lock);
    g_devices[DispatchKey(*device)] = table;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL QuestPanel_DestroyDevice(
    VkDevice device, const VkAllocationCallbacks* allocator)
{
    DeviceTable* table = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_lock);
        auto it = g_devices.find(DispatchKey(device));
        if (it == g_devices.end()) return;
        table = it->second;
        g_devices.erase(it);
    }
    if (table->DeviceWaitIdle) table->DeviceWaitIdle(device);
    for (auto& slot : table->frames) {
        if (slot.done) table->DestroySemaphore(device, slot.done, nullptr);
        if (slot.fence) table->DestroyFence(device, slot.fence, nullptr);
    }
    if (table->pool) table->DestroyCommandPool(device, table->pool, nullptr);
    ReleasePanelBuffer(table);
    table->DestroyDevice(device, allocator);
    delete table;
}

// ------------------------------------------------------------- proc address

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
QuestPanel_GetDeviceProcAddr(VkDevice device, const char* name);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
QuestPanel_GetInstanceProcAddr(VkInstance instance, const char* name)
{
    const auto match = [&](const char* other) { return std::strcmp(name, other) == 0; };
    if (match("vkGetInstanceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_GetInstanceProcAddr);
    if (match("vkCreateInstance"))      return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_CreateInstance);
    if (match("vkDestroyInstance"))     return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_DestroyInstance);
    if (match("vkCreateDevice"))        return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_CreateDevice);
    if (match("vkGetDeviceProcAddr"))   return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_GetDeviceProcAddr);

    InstanceTable* table = nullptr;
    { std::lock_guard<std::mutex> guard(g_lock); if (instance) table = FindInstance(instance); }
    if (!table || !table->GetInstanceProcAddr) return nullptr;
    return table->GetInstanceProcAddr(instance, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
QuestPanel_GetDeviceProcAddr(VkDevice device, const char* name)
{
    const auto match = [&](const char* other) { return std::strcmp(name, other) == 0; };
    if (match("vkGetDeviceProcAddr"))     return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_GetDeviceProcAddr);
    if (match("vkDestroyDevice"))         return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_DestroyDevice);
    // so o processo do jogo recebe os ganchos de desenho
    if (GameProcess()) {
        if (match("vkCreateSwapchainKHR"))  return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_CreateSwapchainKHR);
        if (match("vkDestroySwapchainKHR")) return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_DestroySwapchainKHR);
        if (match("vkQueuePresentKHR"))     return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_QueuePresentKHR);
        if (match("vkGetDeviceQueue"))      return reinterpret_cast<PFN_vkVoidFunction>(&QuestPanel_GetDeviceQueue);
    }
    DeviceTable* table = nullptr;
    { std::lock_guard<std::mutex> guard(g_lock); if (device) table = FindDevice(device); }
    if (!table || !table->GetDeviceProcAddr) return nullptr;
    return table->GetDeviceProcAddr(device, name);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* version)
{
    if (!version || version->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (version->loaderLayerInterfaceVersion > 2)
        version->loaderLayerInterfaceVersion = 2;
    version->pfnGetInstanceProcAddr = &QuestPanel_GetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = &QuestPanel_GetDeviceProcAddr;
    version->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}

}  // extern "C"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        g_modulo = module;
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_threadTecladoId) PostThreadMessageW(g_threadTecladoId, WM_QUIT, 0, 0);
    }
    return TRUE;
}
