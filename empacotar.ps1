# Monta o .zip que vai na release do GitHub.
#
# Entra no pacote: a camada compilada, o instalador, o atualizador, o script de
# extracao e os guias pesquisados.
# NAO entra: nada de texto do jogo (catalogo, tabelas de idioma). Isso e
# propriedade da Keen Games; o instalador gera na maquina de quem usa, a partir
# da copia do jogo da propria pessoa.
param([string]$Versao = '')

$ErrorActionPreference = 'Stop'
$raiz = $PSScriptRoot
if (-not $Versao) { $Versao = (Get-Content (Join-Path $raiz 'instalador\VERSION') -TotalCount 1).Trim() }

$saida = Join-Path $raiz "pacote\EnshroudedQuestPanel-$Versao"
if (Test-Path (Join-Path $raiz 'pacote')) { Remove-Item (Join-Path $raiz 'pacote') -Recurse -Force }
New-Item -ItemType Directory -Force $saida | Out-Null

$dll = Join-Path $raiz 'dist\questpanel_layer.dll'
if (-not (Test-Path $dll)) { throw 'compile antes: .\build.ps1' }

Copy-Item $dll $saida
Copy-Item (Join-Path $raiz 'dist\questpanel_layer.json') $saida
Copy-Item (Join-Path $raiz 'data\guias_pt.json') $saida
foreach ($f in 'instalador.ps1', 'atualizar.ps1', 'interface.ps1', 'extrair_missoes.lua',
                'VERSION', 'Painel de Missoes.bat') {
    Copy-Item (Join-Path $raiz "instalador\$f") $saida
}
# os atalhos sem janela ficam numa subpasta, para quem preferir
Copy-Item (Join-Path $raiz 'instalador\avancado') $saida -Recurse -Force

Copy-Item (Join-Path $raiz 'LEIA-ME.txt') $saida -ErrorAction SilentlyContinue

# conferencia: nada de texto do jogo pode vazar para o pacote
$proibidos = 'catalogo.txt', 'loca_chaves.txt', 'loca_pt.json', 'missoes_do_jogo.tsv', 'ids_missoes.txt'
foreach ($p in $proibidos) {
    if (Test-Path (Join-Path $saida $p)) { throw "o pacote nao pode levar $p (texto do jogo)" }
}

$zip = Join-Path $raiz "pacote\EnshroudedQuestPanel-$Versao.zip"
Compress-Archive -Path "$saida\*" -DestinationPath $zip -Force
$tam = [math]::Round((Get-Item $zip).Length / 1KB)
Write-Host ''
Write-Host "  pacote: $zip  ($tam KB)" -ForegroundColor Green
Get-ChildItem $saida | ForEach-Object { Write-Host ('    ' + $_.Name) }
Write-Host ''
