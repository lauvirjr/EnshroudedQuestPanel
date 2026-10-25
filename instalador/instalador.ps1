# Instala o Painel de Missoes do Enshrouded.
#
# O pacote NAO traz os textos do jogo: eles sao propriedade da Keen Games e
# seriam redistribuicao. Em vez disso, este script extrai o catalogo da SUA
# copia do jogo (com o kfc-parser, que ele mesmo baixa) e junta com os guias
# pesquisados que vem no pacote. O resultado fica so na sua maquina.
param(
    [ValidateSet('instalar', 'remover', 'estado', 'catalogo')][string]$acao = 'estado',
    [string]$Jogo = ''
)

$ErrorActionPreference = 'Stop'
$aqui = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifesto = Join-Path $aqui 'questpanel_layer.json'
$dll = Join-Path $aqui 'questpanel_layer.dll'
$chave = 'HKCU:\SOFTWARE\Khronos\Vulkan\ImplicitLayers'
$dados = Join-Path $env:LOCALAPPDATA 'EnshroudedQuestPanel'
$catalogo = Join-Path $dados 'catalogo.txt'
$EML_RELEASE = 'https://api.github.com/repos/Brabb3l/kfc-parser/releases/latest'

# O -Encoding utf8 do PowerShell 5.1 escreve BOM, e nem o kfc-parser nem a
# camada aceitam: um recusa o JSON, a outra espera o 'V' no primeiro byte.
function EscreverSemBom($caminho, $texto) {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($caminho, $texto, $utf8)
}

# O -Encoding utf8 do PowerShell 5.1 nao le UTF-8 de forma confiavel e os
# acentos chegam trocados ("VÃ¡" em vez de "Vá"). Lendo pelo .NET nao ha duvida.
function LerUtf8($caminho) {
    return [System.IO.File]::ReadAllText($caminho, [System.Text.Encoding]::UTF8)
}

function LerLinhasUtf8($caminho) {
    return [System.IO.File]::ReadAllLines($caminho, [System.Text.Encoding]::UTF8)
}

function AcharJogo {
    if ($Jogo -and (Test-Path (Join-Path $Jogo 'enshrouded.exe'))) { return $Jogo }
    $candidatos = @()
    $raiz = "${env:ProgramFiles(x86)}\Steam"
    $vdf = Join-Path $raiz 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        $candidatos += $raiz
        foreach ($m in (Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' -AllMatches).Matches) {
            $candidatos += $m.Groups[1].Value.Replace('\\', '\')
        }
    }
    foreach ($c in $candidatos) {
        $p = Join-Path $c 'steamapps\common\Enshrouded'
        if (Test-Path (Join-Path $p 'enshrouded.exe')) { return $p }
    }
    return $null
}

function BaixarEmm($destino) {
    # O kfc-parser e GPL; por isso nao vai no pacote -- baixamos do oficial.
    Write-Host '  Baixando o extrator (kfc-parser, de Brabb3l)...'
    $rel = Invoke-RestMethod -Uri $EML_RELEASE -Headers @{ 'User-Agent' = 'EnshroudedQuestPanel' }
    $asset = $rel.assets | Where-Object { $_.name -eq 'emm.exe' } | Select-Object -First 1
    if (-not $asset) { throw 'nao achei emm.exe na release do kfc-parser' }
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $destino -UseBasicParsing
    Write-Host ("    " + $rel.tag_name + ' baixado')
}

function MontarCatalogo($pastaJogo) {
    $tmp = Join-Path $env:TEMP 'questpanel_extracao'
    New-Item -ItemType Directory -Force $tmp | Out-Null
    $emm = Join-Path $tmp 'emm.exe'
    if (-not (Test-Path $emm)) { BaixarEmm $emm }

    # o mod Lua so le; roda com --export, nunca com --patch
    $mod = Join-Path $pastaJogo 'mods\questpanel_extrair'
    New-Item -ItemType Directory -Force (Join-Path $mod 'src') | Out-Null
    EscreverSemBom (Join-Path $mod 'mod.json') @'
{ "id": "questpanel_extrair", "name": "Extrair missoes", "version": "1.0.0",
  "description": "Le o catalogo de missoes. Nao altera nada.",
  "capabilities": ["export"] }
'@
    Copy-Item (Join-Path $aqui 'extrair_missoes.lua') (Join-Path $mod 'src\mod.lua') -Force

    Write-Host '  Lendo as missoes da sua copia do jogo...'
    $saida = Join-Path $tmp 'saida'
    New-Item -ItemType Directory -Force $saida | Out-Null
    & $emm run -g $pastaJogo --export --export-directory $saida 2>&1 | Out-Null
    Remove-Item $mod -Recurse -Force -ErrorAction SilentlyContinue

    $tsv = Join-Path $saida 'missoes_do_jogo.tsv'
    if (-not (Test-Path $tsv)) { throw 'a extracao nao gerou o catalogo; o jogo pode ter atualizado' }

    # junta: estrutura do jogo + guia pesquisado quando existir
    $guias = @{}
    $arqGuias = Join-Path $aqui 'guias_pt.json'
    if (Test-Path $arqGuias) {
        $j = (LerUtf8 $arqGuias) | ConvertFrom-Json
        foreach ($p in $j.PSObject.Properties) {
            $k = ($p.Name -replace '[^a-zA-Z0-9]', '').ToLower()
            $guias[$k] = $p.Value
        }
    }

    # 1) le o TSV para uma lista de missoes
    $T = [char]9
    $missoes = [System.Collections.Generic.List[object]]::new()
    foreach ($linha in (LerLinhasUtf8 $tsv)) {
        $c = $linha -split $T
        if ($c[0] -eq 'QUEST') {
            $missoes.Add([pscustomobject]@{
                nome = $c[1]; nomeEn = $c[2]
                objetivos = [System.Collections.Generic.List[object]]::new()
                fins = [System.Collections.Generic.List[string]]::new()
            })
        } elseif ($c[0] -eq 'ENTRY' -and $missoes.Count -gt 0) {
            $m = $missoes[$missoes.Count - 1]
            $m.objetivos.Add([pscustomobject]@{
                titulo = $c[1]; texto = $c[2]; local = $c[3] })
            if ($c.Count -gt 7 -and $c[7]) { $m.fins.Add($c[7]) }
        }
    }

    # 2) monta o texto, trocando pelo guia pesquisado quando houver
    $linhas = [System.Collections.Generic.List[string]]::new()
    $linhas.Add('V2')
    $feitas = 0; $comGuia = 0
    foreach ($m in ($missoes | Sort-Object { $_.nome })) {
        if (-not $m.nome -or $m.objetivos.Count -eq 0) { continue }
        $feitas++
        $k = ($m.nomeEn -replace '[^a-zA-Z0-9]', '').ToLower()
        $g = $guias[$k]
        $linhas.Add('MISSAO' + $T + $m.nome)
        if ($m.fins.Count) { $linhas.Add('CONCLUI' + $T + ($m.fins -join ',')) }
        if ($g -and $g.objetivos) {
            $comGuia++
            if ($g.recompensa) { $linhas.Add('PREMIO' + $T + $g.recompensa) }
            $i = 1
            foreach ($o in $g.objetivos) {
                $linhas.Add('OBJ' + $T + $i + $T + $o.titulo + $T + $o.local)
                foreach ($passo in $o.passos) { $linhas.Add('PASSO' + $T + $passo) }
                $i++
            }
        } else {
            $i = 1
            foreach ($o in $m.objetivos) {
                $linhas.Add('OBJ' + $T + $i + $T + $o.titulo + $T + $o.local)
                if ($o.texto) { $linhas.Add('PASSO' + $T + $o.texto) }
                $i++
            }
        }
    }

    New-Item -ItemType Directory -Force $dados | Out-Null
    EscreverSemBom $catalogo (($linhas -join "`n") + "`n")
    Write-Host ("  Catalogo pronto: $feitas missoes, $comGuia com guia pesquisado.")
}

function Estado {
    Write-Host ''
    $reg = (Test-Path $chave) -and ($null -ne (Get-ItemProperty $chave).PSObject.Properties[$manifesto])
    Write-Host ('  camada registrada : ' + $(if ($reg) { 'SIM' } else { 'nao' }))
    Write-Host ('  arquivos no lugar : ' + $(if ((Test-Path $dll) -and (Test-Path $manifesto)) { 'SIM' } else { 'NAO' }))
    if (Test-Path $catalogo) {
        $n = (Select-String -Path $catalogo -Pattern '^MISSAO' -AllMatches).Count
        Write-Host ("  lista de missoes  : $n  (F6 no jogo)")
    } else {
        Write-Host '  lista de missoes  : nenhuma -- rode a instalacao'
    }
    $v = Join-Path $aqui 'VERSION'
    if (Test-Path $v) { Write-Host ('  versao instalada  : ' + (Get-Content $v -TotalCount 1).Trim()) }
    Write-Host ''
}

switch ($acao) {
    'instalar' {
        if (-not (Test-Path $dll)) { throw "nao achei $dll" }
        $jogo = AcharJogo
        if (-not $jogo) { throw 'nao achei a pasta do Enshrouded; rode com -Jogo "<caminho>"' }
        Write-Host ''
        Write-Host "  Jogo encontrado em: $jogo"
        if (Get-Process enshrouded -ErrorAction SilentlyContinue) {
            throw 'feche o Enshrouded antes de instalar'
        }
        New-Item -Path $chave -Force | Out-Null
        New-ItemProperty -Path $chave -Name $manifesto -PropertyType DWord -Value 0 -Force | Out-Null
        New-Item -ItemType Directory -Force $dados | Out-Null
        MontarCatalogo $jogo
        Write-Host ''
        Write-Host '  Instalado.' -ForegroundColor Green
        Write-Host '    F6  lista de missoes (setas movem, Enter ou F6 escolhe)'
        Write-Host '    F5  mostra e esconde o painel'
        Estado
    }
    'catalogo' {
        $jogo = AcharJogo
        if (-not $jogo) { throw 'nao achei a pasta do Enshrouded; rode com -Jogo "<caminho>"' }
        MontarCatalogo $jogo
    }
    'remover' {
        if (Test-Path $chave) { Remove-ItemProperty -Path $chave -Name $manifesto -ErrorAction SilentlyContinue }
        Write-Host ''
        Write-Host '  Removido. O jogo volta ao normal na proxima abertura.' -ForegroundColor Yellow
        Write-Host ("  Os dados continuam em $dados (apague a mao se quiser)")
        Write-Host ''
    }
    default { Estado }
}
