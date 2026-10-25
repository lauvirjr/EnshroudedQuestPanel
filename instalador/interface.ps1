# Janela unica do Painel de Missoes: mostra o estado, procura atualizacao
# sozinha ao abrir e tem os botoes de instalar, atualizar e remover.
#
# E um script, e nao um .exe, de proposito: um executavel nao assinado vindo de
# um repositorio novo cai no SmartScreen e em heuristica de antivirus. Assim
# qualquer um abre no bloco de notas e confere o que roda antes de clicar.
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$aqui = Split-Path -Parent $MyInvocation.MyCommand.Path
$instalador = Join-Path $aqui 'instalador.ps1'
$dados = Join-Path $env:LOCALAPPDATA 'EnshroudedQuestPanel'
$catalogo = Join-Path $dados 'catalogo.txt'
$chaveReg = 'HKCU:\SOFTWARE\Khronos\Vulkan\ImplicitLayers'
$manifesto = Join-Path $aqui 'questpanel_layer.json'
$REPO = 'lauvirjr/EnshroudedQuestPanel'

# ---------------------------------------------------------------- aparencia
$fundo = [System.Drawing.Color]::FromArgb(20, 26, 34)
$fundo2 = [System.Drawing.Color]::FromArgb(28, 36, 46)
$texto = [System.Drawing.Color]::FromArgb(230, 237, 243)
$suave = [System.Drawing.Color]::FromArgb(139, 155, 171)
$destaque = [System.Drawing.Color]::FromArgb(79, 209, 197)
$fonte = New-Object System.Drawing.Font('Segoe UI', 9)
$fonteT = New-Object System.Drawing.Font('Segoe UI', 14, [System.Drawing.FontStyle]::Bold)
$fonteM = New-Object System.Drawing.Font('Consolas', 8.5)

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Painel de Missões - Enshrouded'
$form.Size = New-Object System.Drawing.Size(560, 470)
$form.StartPosition = 'CenterScreen'
$form.BackColor = $fundo
$form.ForeColor = $texto
$form.Font = $fonte
$form.FormBorderStyle = 'FixedSingle'
$form.MaximizeBox = $false

$titulo = New-Object System.Windows.Forms.Label
$titulo.Text = 'Painel de Missões'
$titulo.Font = $fonteT
$titulo.ForeColor = $texto
$titulo.Location = New-Object System.Drawing.Point(20, 16)
$titulo.Size = New-Object System.Drawing.Size(330, 28)
$form.Controls.Add($titulo)

$versaoLbl = New-Object System.Windows.Forms.Label
$versaoLbl.Text = ''
$versaoLbl.ForeColor = $suave
$versaoLbl.TextAlign = 'MiddleRight'
$versaoLbl.Location = New-Object System.Drawing.Point(350, 22)
$versaoLbl.Size = New-Object System.Drawing.Size(180, 20)
$form.Controls.Add($versaoLbl)

# painel de estado
$caixa = New-Object System.Windows.Forms.Panel
$caixa.Location = New-Object System.Drawing.Point(20, 54)
$caixa.Size = New-Object System.Drawing.Size(510, 92)
$caixa.BackColor = $fundo2
$form.Controls.Add($caixa)

function NovoRotulo($txt, $x, $y, $w, $cor) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $txt
    $l.Location = New-Object System.Drawing.Point($x, $y)
    $l.Size = New-Object System.Drawing.Size($w, 20)
    $l.ForeColor = $cor
    return $l
}

$caixa.Controls.Add((NovoRotulo 'Camada no jogo' 16 12 140 $suave))
$caixa.Controls.Add((NovoRotulo 'Lista de missões' 16 36 140 $suave))
$caixa.Controls.Add((NovoRotulo 'Atualização' 16 60 140 $suave))
$vCamada = NovoRotulo '...' 164 12 330 $texto
$vLista = NovoRotulo '...' 164 36 330 $texto
$vAtualiza = NovoRotulo 'procurando...' 164 60 330 $suave
$caixa.Controls.Add($vCamada); $caixa.Controls.Add($vLista); $caixa.Controls.Add($vAtualiza)

# botoes
function NovoBotao($txt, $x, $principal) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $txt
    $b.Location = New-Object System.Drawing.Point($x, 158)
    $b.Size = New-Object System.Drawing.Size(120, 34)
    $b.FlatStyle = 'Flat'
    $b.FlatAppearance.BorderSize = 0
    $b.ForeColor = if ($principal) { [System.Drawing.Color]::FromArgb(4, 19, 26) } else { $texto }
    $b.BackColor = if ($principal) { $destaque } else { $fundo2 }
    $b.Cursor = 'Hand'
    return $b
}

$btInstalar = NovoBotao 'Instalar' 20 $true
$btAtualizar = NovoBotao 'Atualizar' 150 $false
$btLista = NovoBotao 'Refazer lista' 280 $false
$btRemover = NovoBotao 'Remover' 410 $false
$form.Controls.AddRange(@($btInstalar, $btAtualizar, $btLista, $btRemover))

$saida = New-Object System.Windows.Forms.TextBox
$saida.Multiline = $true
$saida.ScrollBars = 'Vertical'
$saida.ReadOnly = $true
$saida.Location = New-Object System.Drawing.Point(20, 204)
$saida.Size = New-Object System.Drawing.Size(510, 190)
$saida.BackColor = [System.Drawing.Color]::FromArgb(13, 19, 26)
$saida.ForeColor = $texto
$saida.Font = $fonteM
$saida.BorderStyle = 'FixedSingle'
$form.Controls.Add($saida)

$rodape = NovoRotulo 'F6 abre a lista no jogo  ·  F5 mostra e esconde o painel' 20 404 510 $suave
$form.Controls.Add($rodape)

# ---------------------------------------------------------------- logica
function Diz($txt) {
    $saida.AppendText($txt + "`r`n")
    $saida.SelectionStart = $saida.TextLength
    $saida.ScrollToCaret()
    [System.Windows.Forms.Application]::DoEvents()
}

function VersaoLocal {
    $v = Join-Path $aqui 'VERSION'
    if (Test-Path $v) { return (Get-Content $v -TotalCount 1).Trim() }
    return '0.0.0'
}

function ComoNumero($v) {
    $p = ($v -replace '^v', '') -split '[.\-+]'
    $n = 0
    for ($i = 0; $i -lt 3; $i++) {
        $parte = 0
        if ($i -lt $p.Count) { [int]::TryParse($p[$i], [ref]$parte) | Out-Null }
        $n = $n * 100 + $parte
    }
    return $n
}

function AtualizarEstado {
    $reg = (Test-Path $chaveReg) -and
           ($null -ne (Get-ItemProperty $chaveReg -ErrorAction SilentlyContinue).PSObject.Properties[$manifesto])
    if ($reg) {
        $vCamada.Text = 'instalada'
        $vCamada.ForeColor = [System.Drawing.Color]::FromArgb(94, 227, 154)
        $btInstalar.Text = 'Reinstalar'
    } else {
        $vCamada.Text = 'não instalada'
        $vCamada.ForeColor = [System.Drawing.Color]::FromArgb(246, 173, 85)
        $btInstalar.Text = 'Instalar'
    }
    if (Test-Path $catalogo) {
        $n = (Select-String -Path $catalogo -Pattern '^MISSAO' -AllMatches).Count
        $vLista.Text = "$n missões"
        $vLista.ForeColor = $texto
    } else {
        $vLista.Text = 'nenhuma - use Instalar'
        $vLista.ForeColor = [System.Drawing.Color]::FromArgb(246, 173, 85)
    }
    $versaoLbl.Text = 'versão ' + (VersaoLocal)
    $btRemover.Enabled = $reg
}

function Rodar($argumentos, $titulo) {
    foreach ($b in @($btInstalar, $btAtualizar, $btLista, $btRemover)) { $b.Enabled = $false }
    $form.Cursor = 'WaitCursor'
    Diz ''
    Diz "=== $titulo ==="
    try {
        # o trabalho pesado vai para o instalador.ps1: a janela so mostra
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = 'powershell.exe'
        $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$instalador`" $argumentos"
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.CreateNoWindow = $true
        $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
        $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8
        $proc = [System.Diagnostics.Process]::Start($psi)
        while (-not $proc.HasExited) {
            while (-not $proc.StandardOutput.EndOfStream) { Diz ($proc.StandardOutput.ReadLine()) }
            [System.Windows.Forms.Application]::DoEvents()
            Start-Sleep -Milliseconds 80
        }
        while (-not $proc.StandardOutput.EndOfStream) { Diz ($proc.StandardOutput.ReadLine()) }
        $erro = $proc.StandardError.ReadToEnd()
        if ($erro) { Diz ''; Diz ($erro.Trim()) }
    } catch {
        Diz ('falhou: ' + $_.Exception.Message)
    }
    $form.Cursor = 'Default'
    foreach ($b in @($btInstalar, $btAtualizar, $btLista, $btRemover)) { $b.Enabled = $true }
    AtualizarEstado
}

function JogoAberto {
    return $null -ne (Get-Process enshrouded -ErrorAction SilentlyContinue)
}

function ProcurarAtualizacao {
    $local = VersaoLocal
    try {
        $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$REPO/releases/latest" `
                                 -Headers @{ 'User-Agent' = 'EnshroudedQuestPanel' } -TimeoutSec 12
        $remota = $rel.tag_name
        if ((ComoNumero $remota) -gt (ComoNumero $local)) {
            $vAtualiza.Text = "versão $remota disponível"
            $vAtualiza.ForeColor = $destaque
            $btAtualizar.BackColor = $destaque
            $btAtualizar.ForeColor = [System.Drawing.Color]::FromArgb(4, 19, 26)
            Diz "Há uma versão nova: $remota (você tem $local)."
            Diz 'Use o botão Atualizar.'
        } else {
            $vAtualiza.Text = 'na versão mais nova'
            $vAtualiza.ForeColor = [System.Drawing.Color]::FromArgb(94, 227, 154)
            $btAtualizar.Enabled = $false
        }
    } catch {
        $vAtualiza.Text = 'não consegui verificar'
        $vAtualiza.ForeColor = $suave
        Diz 'Não consegui falar com o GitHub para ver se há versão nova.'
        Diz 'Isso não atrapalha o que já está instalado.'
    }
}

$btInstalar.Add_Click({
    if (JogoAberto) {
        [System.Windows.Forms.MessageBox]::Show(
            'Feche o Enshrouded antes de instalar: a camada fica carregada enquanto o jogo roda.',
            'Jogo aberto', 'OK', 'Warning') | Out-Null
        return
    }
    Rodar 'instalar' 'Instalando'
})

$btAtualizar.Add_Click({
    if (JogoAberto) {
        [System.Windows.Forms.MessageBox]::Show(
            'Feche o Enshrouded antes de atualizar.', 'Jogo aberto', 'OK', 'Warning') | Out-Null
        return
    }
    foreach ($b in @($btInstalar, $btAtualizar, $btLista, $btRemover)) { $b.Enabled = $false }
    Diz ''
    Diz '=== Atualizando ==='
    try {
        & (Join-Path $aqui 'atualizar.ps1') -SemPergunta 2>&1 | ForEach-Object { Diz ([string]$_) }
    } catch {
        Diz ('falhou: ' + $_.Exception.Message)
    }
    foreach ($b in @($btInstalar, $btAtualizar, $btLista, $btRemover)) { $b.Enabled = $true }
    AtualizarEstado
})

$btLista.Add_Click({ Rodar 'catalogo' 'Refazendo a lista de missões' })

$btRemover.Add_Click({
    $r = [System.Windows.Forms.MessageBox]::Show(
        'Remover o painel? O jogo volta ao normal na próxima abertura.',
        'Confirmar', 'YesNo', 'Question')
    if ($r -eq 'Yes') { Rodar 'remover' 'Removendo' }
})

$form.Add_Shown({
    $form.Activate()
    Diz 'Painel de Missões para Enshrouded'
    Diz ''
    AtualizarEstado
    if (JogoAberto) {
        Diz 'O Enshrouded está aberto. Para instalar ou atualizar, feche o jogo antes.'
        Diz ''
    }
    ProcurarAtualizacao
})

[void]$form.ShowDialog()
