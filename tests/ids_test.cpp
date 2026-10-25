#include "../src/panel_model.h"
#include <cstdio>
using namespace questpanel;
int main() {
    std::vector<Quest> c;
    if (!ParseCatalogFile(CatalogFilePath(), c)) { std::printf("FALHA ao ler\n"); return 1; }
    std::size_t comId = 0, totalIds = 0;
    for (const auto& q : c) { if (!q.completionIds.empty()) ++comId; totalIds += q.completionIds.size(); }
    std::printf("missoes: %zu | com id de conclusao: %zu | ids no total: %zu\n",
                c.size(), comId, totalIds);
    for (std::size_t i = 0; i < c.size() && i < 3; ++i) {
        std::wprintf(L"  %s -> ", c[i].name.c_str());
        for (auto id : c[i].completionIds) std::printf("%u ", id);
        std::printf("\n");
    }
    return comId == c.size() ? 0 : 1;
}
