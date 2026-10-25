-- Exporta o catalogo oficial de missoes do Enshrouded, com os textos em
-- portugues tirados do proprio jogo. So le: roda com --export, sem --patch.
--
-- Duas diferencas em relacao ao exemplo do repositorio, descobertas na marra:
--   * hoje e l.data.hash (e nao l.dataHash) e loca.keenglishData.hash;
--   * os campos de texto ja trazem a chave pronta em .value -- nao se passa
--     por game.guid.hash, que era o que fazia tudo voltar vazio.
--
-- Alem do texto, saem os ids de conhecimento de cada objetivo. Sao eles que
-- permitem saber, cruzando com o player_knowledge do save, o que o jogador ja
-- concluiu -- e assim sumir com as missoes prontas da lista do jogo.
if not loader.features.export then
    print("rode com --export")
    return
end

local TAB = string.char(9)

local function traducoes(idioma)
    local loca = game.assets.get_resources_by_type("keen::LocaTagCollectionResource")[1].data
    local alvo = nil
    for _, l in ipairs(loca.languages or {}) do
        if tostring(l.language) == idioma then alvo = l.data.hash break end
    end
    if alvo == nil then alvo = loca.keenglishData.hash end
    local dados = game.assets.get_content(game.guid.from_content_hash(alvo)):read_data()
    local tabela = dados:read_resource("keen::LocaTagCollectionResourceData")
    local dic, n = {}, 0
    for _, tag in ipairs(tabela.tags) do dic[tag.id.value] = tag.text; n = n + 1 end
    return dic, n
end

local pt, nPt = traducoes("Pt_Br")
local en = traducoes("En_Us")
print("textos em portugues: " .. nPt)

local function limpa(s)
    if s == nil then return "" end
    s = tostring(s):gsub("<[^>]+>", "")
    s = s:gsub("[\r\n\t]", " "):gsub("%s+", " ")
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function texto(ref, dic)
    if ref == nil or ref.value == nil or ref.value == 0 then return "" end
    return limpa(dic[ref.value])
end

local function idDe(req)
    if req == nil or req.knowledgeOrQueryId == nil then return "" end
    local v = req.knowledgeOrQueryId.value
    if v == nil or v == 0 then return "" end
    return tostring(v)
end

local function tipoDe(req)
    if req == nil then return "" end
    return tostring(req.type or "")
end

local reg = game.assets.get_resources_by_type("keen::JournalRegistryResource")[1].data
local linhas = {}
local nQuests, nEntradas, comConclusao = 0, 0, 0

for _, q in ipairs(reg.quests or {}) do
    nQuests = nQuests + 1
    linhas[#linhas + 1] = table.concat({
        "QUEST", texto(q.name, pt), texto(q.name, en),
        tostring(q.source or ""), tostring(q.type or ""),
        tostring(q.isMainQuest or false),
    }, TAB)
    for _, e in ipairs(q.entries or {}) do
        nEntradas = nEntradas + 1
        local fim = idDe(e.completionRequirement)
        if fim ~= "" then comConclusao = comConclusao + 1 end
        linhas[#linhas + 1] = table.concat({
            "ENTRY", texto(e.name, pt), texto(e.text, pt),
            texto(e.mapMarkerReference, pt),
            tostring(e.recommendedLevel or ""),
            idDe(e.knowledgeRequirement), tipoDe(e.knowledgeRequirement),
            fim, tipoDe(e.completionRequirement),
            tostring((e.entryId or {}).value or ""),
        }, TAB)
    end
end

for _, c in ipairs(reg.collections or {}) do
    linhas[#linhas + 1] = table.concat({
        "COLLECTION", texto(c.name, pt), texto(c.name, en),
        tostring(c.priority or ""), tostring(c.isTutorial or false),
    }, TAB)
end

io.export("missoes_do_jogo.tsv", table.concat(linhas, "\n"))
print("quests=" .. nQuests .. " entradas=" .. nEntradas .. " com id de conclusao=" .. comConclusao)
