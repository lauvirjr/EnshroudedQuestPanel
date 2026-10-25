// Checa a leitura do enshrouded.log que decide se o painel aparece.
// Monta logs sinteticos num arquivo temporario; nao precisa do jogo.
#include "../src/panel_model.h"

#include <cstdio>
#include <string>

using namespace questpanel;

namespace {

int falhas = 0;

std::wstring EscreverLog(const std::string& conteudo)
{
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    std::wstring path = std::wstring(dir) + L"questpanel_world_test.log";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    DWORD written = 0;
    WriteFile(f, conteudo.data(), static_cast<DWORD>(conteudo.size()), &written, nullptr);
    CloseHandle(f);
    return path;
}

void Checar(const char* caso, const std::string& log, bool anterior, bool esperado)
{
    const std::wstring path = EscreverLog(log);
    const bool obtido = PlayerInWorld(path, anterior);
    const bool ok = obtido == esperado;
    if (!ok) ++falhas;
    std::printf("  [%s] %-52s esperado=%d obtido=%d\n", ok ? "ok" : "FALHA", caso, esperado, obtido);
    DeleteFileW(path.c_str());
}

const char* kRuido =
    "[I 00:00:03,362] [OnlineProviderSteam] started transition from 'Uninitialized' to 'Initialized'!\n"
    "[I 00:00:03,624] [SessionPlayer] started transition from 'Free' to 'Local'!\n"
    "[I 00:00:54,044] [gameframework] Window became active ...\n";

}  // namespace

int main()
{
    std::printf("deteccao de 'dentro do mundo' pelo log:\n");

    Checar("log so com ruido -> menu", kRuido, true, false);

    Checar("entrou num mundo",
           std::string(kRuido) +
           "[I 00:01:10,000] [Session] started transition from 'Lobby' to 'Playing'!\n",
           false, true);

    Checar("voltou para o menu",
           std::string(kRuido) +
           "[I 00:01:10,000] [Session] started transition from 'Lobby' to 'Playing'!\n"
           "[I 00:05:00,000] [Session] started transition from 'Playing' to 'Lobby'!\n",
           true, false);

    Checar("entrou, saiu e entrou de novo",
           std::string(kRuido) +
           "[I 00:01:10,000] [Session] started transition from 'Lobby' to 'Playing'!\n"
           "[I 00:05:00,000] [Session] started transition from 'Playing' to 'Lobby'!\n"
           "[I 00:06:00,000] [Session] started transition from 'Lobby' to 'Playing'!\n",
           false, true);

    Checar("linhas depois da transicao nao atrapalham",
           std::string(kRuido) +
           "[I 00:01:10,000] [Session] started transition from 'Lobby' to 'Playing'!\n"
           "[I 00:01:11,000] [os] SPI_GETMOUSESPEED = 10\n"
           "[I 00:01:12,000] [gameframework] Window became active ...\n",
           false, true);

    // arquivo que nao existe: mantem o estado anterior em vez de piscar o painel
    const bool semArquivo = PlayerInWorld(L"C:\\nao\\existe\\enshrouded.log", true);
    const bool okSem = semArquivo == true;
    if (!okSem) ++falhas;
    std::printf("  [%s] %-52s esperado=1 obtido=%d\n", okSem ? "ok" : "FALHA",
                "log ausente mantem o estado anterior", semArquivo);

    // um log grande: so o fim e lido, e a decisao vem da ultima transicao
    std::string grande;
    grande.reserve(900 * 1024);
    while (grande.size() < 800 * 1024)
        grande += "[I 00:00:00,000] [render] quadro desenhado sem nada de interessante\n";
    grande += "[I 09:00:00,000] [Session] started transition from 'Playing' to 'Lobby'!\n";
    Checar("log de 800 KB, ultima transicao manda", grande, true, false);

    std::printf("\n%s\n", falhas == 0 ? "TODOS OS CASOS PASSARAM" : "HOUVE FALHA");
    return falhas == 0 ? 0 : 1;
}
