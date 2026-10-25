# Procura uma versao nova do painel no GitHub e instala por cima.
#
# Baixa so o pacote da release, compara com a versao local e troca os arquivos.
# O catalogo e refeito depois, porque uma atualizacao do jogo costuma vir junto
# com a do mod -- e e o catalogo que carrega os textos da SUA copia do jogo.
param(
    [switch]$Forcar,        # reinstala mesmo estando na ultima versao
    [switch]$SemPergunta    # nao espera confirmacao (para atalho/automacao)
)

$ErrorActionPreference = 'Stop'
$aqui = Split-Path -Parent $MyInvocation.MyCommand.Path
$REPO = 'lauvirjr/EnshroudedQuestPanel'
$api = "https://api.github.com/repos/$REPO/releases/latest"

function VersaoLocal {
    $v = Join-Path $aqui 'VERSION'
    if (Test-Path $v) { return (Get-Content $v -TotalCount 1).Trim() }
    return '0.0.0'
}

function ComoNumero($v) {
    # "1.2.3" -> 10203, para comparar sem depender de ordem alfabetica
    $p = ($v -replace '^v', '') -split '[.\-+]'
    $n = 0
    for ($i = 0; $i -lt 3; $i++) {
        $parte = 0
        if ($i -lt $p.Count) { [int]::TryParse($p[$i], [ref]$parte) | Out-Null }
        $n = $n * 100 + $parte
    }
    return $n
}

Write-Host ''
Write-Host '  ATUALIZADOR DO PAINEL DE MISSOES' -ForegroundColor Cyan
Write-Host '  ================================'
$local = VersaoLocal
Write-Host "  versao instalada: $local"

try {
    $rel = Invoke-RestMethod -Uri $api -Headers @{ 'User-Agent' = 'EnshroudedQuestPanel-Updater' }
} catch {
    Write-Host ''
    Write-Host '  Nao consegui falar com o GitHub.' -ForegroundColor Yellow
    Write-Host "  ($($_.Exception.Message))"
    Write-Host '  Nada foi alterado; tente de novo mais tarde.'
    if (-not $SemPergunta) { Read-Host '  Enter para fechar' }
    exit 1
}

$remota = $rel.tag_name
Write-Host "  versao publicada: $remota   ($($rel.published_at))"
if (-not $Forcar -and (ComoNumero $remota) -le (ComoNumero $local)) {
    Write-Host ''
    Write-Host '  Ja esta na versao mais nova.' -ForegroundColor Green
    if (-not $SemPergunta) { Read-Host '  Enter para fechar' }
    exit 0
}

$asset = $rel.assets | Where-Object { $_.name -like '*.zip' } | Select-Object -First 1
if (-not $asset) {
    Write-Host '  A release nao tem pacote .zip; nada a fazer.' -ForegroundColor Yellow
    if (-not $SemPergunta) { Read-Host '  Enter para fechar' }
    exit 1
}

if (Get-Process enshrouded -ErrorAction SilentlyContinue) {
    Write-Host ''
    Write-Host '  Feche o Enshrouded antes de atualizar: a camada fica carregada' -ForegroundColor Yellow
    Write-Host '  enquanto o jogo roda, e o arquivo nao pode ser trocado.'
    if (-not $SemPergunta) { Read-Host '  Enter para fechar' }
    exit 1
}

if (-not $SemPergunta) {
    Write-Host ''
    if ($rel.body) {
        Write-Host '  O que mudou:'
        ($rel.body -split "`n" | Select-Object -First 12) | ForEach-Object { Write-Host "    $_" }
    }
    $r = Read-Host '  Atualizar agora? (S/n)'
    if ($r -and $r -notmatch '^[sSyY]') { Write-Host '  Cancelado.'; exit 0 }
}

$tmp = Join-Path $env:TEMP ('questpanel_update_' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $tmp | Out-Null
$zip = Join-Path $tmp 'pacote.zip'
Write-Host ''
Write-Host '  Baixando...'
Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -UseBasicParsing
Expand-Archive -Path $zip -DestinationPath (Join-Path $tmp 'novo') -Force

# a release pode vir com uma pasta raiz dentro do zip
$origem = Join-Path $tmp 'novo'
$dentro = Get-ChildItem $origem -Directory
if ((Get-ChildItem $origem -File).Count -eq 0 -and $dentro.Count -eq 1) { $origem = $dentro[0].FullName }

# guarda a versao atual antes de sobrescrever
$backup = Join-Path $aqui ('backup-' + $local)
New-Item -ItemType Directory -Force $backup | Out-Null
foreach ($f in @('questpanel_layer.dll', 'guias_pt.json', 'VERSION', 'instalador.ps1', 'atualizar.ps1')) {
    $p = Join-Path $aqui $f
    if (Test-Path $p) { Copy-Item $p $backup -Force }
}

Write-Host '  Instalando...'
Copy-Item (Join-Path $origem '*') $aqui -Recurse -Force

Write-Host '  Refazendo a lista de missoes com os arquivos do seu jogo...'
try {
    & (Join-Path $aqui 'instalador.ps1') catalogo
} catch {
    Write-Host "  (a lista nao pode ser refeita agora: $($_.Exception.Message))" -ForegroundColor Yellow
    Write-Host '   rode "Instalar.bat" depois para tentar de novo.'
}

Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ''
Write-Host "  Atualizado para $remota." -ForegroundColor Green
Write-Host "  (a versao anterior ficou em $backup)"
Write-Host ''
if (-not $SemPergunta) { Read-Host '  Enter para fechar' }
