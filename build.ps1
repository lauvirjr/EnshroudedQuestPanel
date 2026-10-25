# Compila a camada e os dois testes. Precisa do VS 2022 Build Tools (MSVC v143).
param(
    [string]$VulkanHeaders = "$PSScriptRoot\third_party\Vulkan-Headers\include",
    [switch]$Testes
)
$ErrorActionPreference = 'Stop'
$raiz = $PSScriptRoot
$build = Join-Path $raiz 'build'
$dist = Join-Path $raiz 'dist'
New-Item -ItemType Directory -Force $build, $dist | Out-Null

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $inst = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($inst) { $vcvars = Join-Path $inst 'VC\Auxiliary\Build\vcvars64.bat' }
    }
}
if (-not (Test-Path $vcvars)) { throw 'nao achei o vcvars64.bat do VS 2022 Build Tools' }
if (-not (Test-Path $VulkanHeaders)) {
    throw "headers do Vulkan nao encontrados em $VulkanHeaders -- passe -VulkanHeaders <caminho> (git clone https://github.com/KhronosGroup/Vulkan-Headers)"
}

function Compilar($descricao, $linha) {
    Write-Host "  $descricao" -ForegroundColor Cyan
    # o vcvars escreve avisos inofensivos no stderr (ex.: vswhere ausente no PATH);
    # com ErrorActionPreference=Stop isso abortaria o build sem motivo
    $antes = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $saida = cmd /c "`"$vcvars`" >nul && $linha" 2>&1
    $codigo = $LASTEXITCODE
    $ErrorActionPreference = $antes
    $erros = $saida | Select-String 'error|warning C'
    if ($erros) { $erros | ForEach-Object { Write-Host "    $_" -ForegroundColor Red } }
    if ($codigo -ne 0) { throw "$descricao falhou" }
}

$comum = "/nologo /std:c++17 /EHsc /W4 /O2 /I`"$VulkanHeaders`" /Fo:$build\"
Compilar 'camada' "cl $comum /LD /Fe:$build\questpanel_layer.dll $raiz\src\layer.cpp $raiz\src\panel_model.cpp /link user32.lib gdi32.lib /DEF:$raiz\src\questpanel.def"
try {
    Copy-Item "$build\questpanel_layer.dll" "$dist\questpanel_layer.dll" -Force -ErrorAction Stop
} catch {
    # com o jogo aberto a DLL esta carregada e nao da para substituir
    Write-Host ''
    Write-Host '  AVISO: nao deu para atualizar dist\questpanel_layer.dll' -ForegroundColor Yellow
    Write-Host '         (o Enshrouded esta aberto e segurando a camada)' -ForegroundColor Yellow
    Write-Host '         Feche o jogo e rode o build de novo para aplicar.' -ForegroundColor Yellow
    Write-Host ''
}

if ($Testes) {
    Compilar 'teste de rasterizacao' "cl $comum /Fe:$build\raster_test.exe $raiz\tests\raster_test.cpp $raiz\src\panel_model.cpp /link user32.lib gdi32.lib"
    Compilar 'teste de loader'       "cl $comum /Fe:$build\loader_test.exe  $raiz\tests\loader_test.cpp /link user32.lib gdi32.lib"
    Compilar 'teste de estado de mundo' "cl $comum /Fe:$build\world_test.exe $raiz\tests\world_test.cpp $raiz\src\panel_model.cpp /link user32.lib gdi32.lib"
    Write-Host ''
    & "$build\world_test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'o teste de estado de mundo falhou' }
    Write-Host ''
    Write-Host '  rodando o teste de loader (QUESTPANEL_FORCE liga o desenho fora do jogo)' -ForegroundColor Cyan
    Push-Location $build
    $env:QUESTPANEL_FORCE = '1'
    & "$build\loader_test.exe"
    $codigo = $LASTEXITCODE
    Remove-Item Env:\QUESTPANEL_FORCE
    Pop-Location
    if ($codigo -ne 0) { throw 'o teste de loader falhou' }
}
Write-Host ''
Write-Host '  pronto: dist\questpanel_layer.dll' -ForegroundColor Green
