# Painel de Missões para Enshrouded

Mostra o passo a passo da missão **dentro do jogo**, e deixa escolher qual
missão ver sem sair dele.

Não é o texto da wiki traduzido: para cada missão há um guia prático —
onde cavar, que ferramenta levar, qual inimigo aparece — pesquisado na
internet e escrito em português.

```
┌─ Enshrouded ────────────────────────────────┐
│                                             │
│  ┌───────────────────────────┐              │
│  │ "Suprimentos de Enfermagem"│   ╭────────╮ │
│  │ 1. Encontre o Contrabando  │   │minimapa│ │
│  │    [Repouso de Surat]      │   ╰────────╯ │
│  │  Suba pela escada de mão   │              │
│  │  e leia a nota.            │              │
│  │  Atire no barril explosivo │              │
│  │  no patamar a sudoeste.    │              │
│  └───────────────────────────┘              │
└─────────────────────────────────────────────┘
```

## Teclas

| Tecla | O que faz |
|---|---|
| **F6** | abre a lista de missões · setas movem · Enter ou F6 escolhe · Esc cancela |
| **F5** | mostra e esconde o painel |

F7 a F11 ficam livres para o minimapa Shroudweave. No Enshrouded o Enter abre
o chat, o Esc abre o menu e o F10 é o menu principal — por isso, com a lista
aberta, essas teclas são interceptadas e não chegam ao jogo.

## Instalar

1. Baixe o `.zip` da [última release](../../releases/latest) e descompacte
2. Feche o Enshrouded
3. Dois cliques em **`Instalar.bat`**
4. Abra o jogo

Na instalação ele lê as missões da **sua** cópia do jogo (veja
[Por que não vem pronto](#por-que-não-vem-pronto)). Precisa de internet uma
vez, para baixar o extrator.

Para atualizar depois: **`Atualizar.bat`**. Ele consulta as releases, avisa o
que mudou e troca os arquivos, guardando a versão anterior.

Para sair: **`Remover.bat`**. Tira o registro e o jogo volta ao normal.

## O que ele faz

- **186 missões** com passo a passo, em português
- **Esconde o que você já concluiu** — lê o progresso e tira da lista durante
  o jogo, sem precisar reiniciar nada
- Começa escondido e lembra como você deixou
- Convive com o minimapa Shroudweave

## Por que não vem pronto

Os nomes e textos das missões são da Keen Games. Distribuí-los seria
redistribuir conteúdo do jogo, então o pacote **não** os traz: na instalação
eles são lidos da sua própria cópia do Enshrouded, com o
[kfc-parser](https://github.com/Brabb3l/kfc-parser), que o instalador baixa.

O que vem no pacote são os **guias pesquisados** (`guias_pt.json`), texto
escrito a partir de busca na internet, não cópia do jogo.

## Como funciona

É uma **camada Vulkan**, não um mod carregado por mod loader. A camada se
registra no loader do Vulkan e desenha por cima do quadro pronto.

Isso foi escolha de projeto, não comodidade: o mod do minimapa remenda a
dispatch table do device e confere, a cada quadro, se o ponteiro de
`vkQueuePresentKHR` ainda é o que ele instalou — achando outro, desiste de
desenhar. Dois mods disputando esse ponteiro não convivem. Uma camada fica
abaixo do jogo na cadeia do loader, então o minimapa continua enxergando o que
espera e os dois aparecem.

De quebra, a camada não procura padrões de bytes dentro do `enshrouded.exe`,
que é o que costuma quebrar mods a cada atualização. Ela usa só API pública do
Vulkan e do Windows.

Detalhes de implementação:

- o painel é rasterizado com GDI e copiado para a imagem da swapchain com um
  `vkCmdCopyBufferToImage` por quadro (a camada acrescenta
  `VK_IMAGE_USAGE_TRANSFER_DST_BIT` ao interceptar `vkCreateSwapchainKHR`);
- o painel só aparece dentro de um mundo: isso vem do `enshrouded.log`, que
  registra cada troca de sessão — texto de log, não assinatura de bytes;
- as setas não movem o personagem com a lista aberta porque o jogo lê o teclado
  por Raw Input, e a camada desvia `GetRawInputData` pela tabela de importação.

## Compilar

Visual Studio 2022 Build Tools (MSVC v143) e os
[headers do Vulkan](https://github.com/KhronosGroup/Vulkan-Headers).

```powershell
.\build.ps1 -VulkanHeaders "<caminho>\Vulkan-Headers\include" -Testes
.\empacotar.ps1
```

`-Testes` roda a bateria: leitura do estado de sessão, geração do painel e um
teste que sobe o Vulkan de verdade, apresenta quadros e lê a imagem de volta
para provar que a camada desenhou.

## Créditos e licença

Código sob [MIT](LICENSE).

- [Shroudweave Minimap](https://github.com/Aerox912/Enshrouded-minimap) —
  elxokker e Aerox912. Mod independente; a leitura do código dele foi o que
  mostrou por que uma camada era o caminho.
- [kfc-parser / EML](https://github.com/Brabb3l/kfc-parser) — Brabb3l (GPL-3.0).
  Usado pelo instalador para ler o catálogo do jogo; é baixado na instalação,
  não redistribuído aqui.
- Os guias foram escritos com apoio de IA a partir de busca na internet, e cada
  um registra as fontes consultadas em `guias_pt.json`.

Enshrouded é marca da Keen Games. Este é um projeto independente, sem relação
com a Keen Games.
