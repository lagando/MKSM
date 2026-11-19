# ShaolinMonks RPG, instalador do Windows (chamado pelo Instalar.bat). Compila no próprio Windows com o kit que vem no
# pacote (ferramentas\mingw64: GCC, CMake e Ninja do WinLibs), sem instalar nada e sem internet, monta
# portatil\MKSM-windows e, no fim, apaga o kit e o que foi intermediário. A ISO fica onde está.
$ErrorActionPreference = 'Stop'
$Raiz = Split-Path -Parent $PSScriptRoot
Set-Location $Raiz
$Jobs = [Math]::Max(1, [Environment]::ProcessorCount)

function Passo($texto) { Write-Host ""; Write-Host "== $texto" -ForegroundColor Yellow }
function Rode($exe, [string[]]$argumentos) {
    & $exe @argumentos
    if ($LASTEXITCODE -ne 0) { throw "falhou: $exe $($argumentos -join ' ')" }
}

$kit = Join-Path $Raiz 'ferramentas\mingw64\bin'
if (-not (Test-Path (Join-Path $kit 'g++.exe'))) { throw "Falta a pasta ferramentas\mingw64 (o kit de compilação que vem no zip do Windows)." }
$env:PATH = "$kit;$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\WindowsPowerShell\v1.0"

Passo '1/6 Lendo a ISO'
if (-not (Test-Path 'extraido_full\SLUS_210.87')) {
    $iso = Get-ChildItem 'coloque_o_jogo_aqui' -Filter *.iso | Select-Object -First 1
    if (-not $iso) { throw 'Coloque a ISO do seu jogo (PS2, USA, SLUS-210.87) na pasta coloque_o_jogo_aqui.' }
    $imagem = Mount-DiskImage -ImagePath $iso.FullName -PassThru
    try {
        $letra = ($imagem | Get-Volume).DriveLetter
        New-Item -ItemType Directory -Force extraido_full | Out-Null
        Copy-Item "${letra}:\*" extraido_full -Recurse -Force
    } finally { Dismount-DiskImage -ImagePath $iso.FullName | Out-Null }
    Get-ChildItem extraido_full -Recurse -File | ForEach-Object { $_.IsReadOnly = $false } # cópia de CD vem só leitura
}
if (-not (Test-Path 'extraido_full\SLUS_210.87')) { throw 'SLUS_210.87 não está na ISO: é a versão USA?' }
New-Item -ItemType Directory -Force extraido | Out-Null
Copy-Item extraido_full\SLUS_210.87 extraido\ -Force

Passo '2/6 Preparando o PS2Recomp (já vem com os patches)'
if (-not (Test-Path PS2Recomp)) { Copy-Item vendor\PS2Recomp PS2Recomp -Recurse }
$B = 'PS2Recomp\out\build'
New-Item -ItemType Directory -Force $B | Out-Null
# Vulkan com carga atrasada: o jogo abre mesmo em PC sem Vulkan (desenha pela CPU).
Rode dlltool @('-d', 'vendor\vulkan-1.def', '-y', "$B\libvulkan-1.a")

Passo '3/6 Configurando'
$opcoes = @('-S', 'PS2Recomp', '-B', $B, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=None',
    '-DCMAKE_C_COMPILER=gcc', '-DCMAKE_CXX_COMPILER=g++', '-DCMAKE_CXX_FLAGS=-msse4.1 -O1 -g0',
    '-DCMAKE_EXE_LINKER_FLAGS=-static', '-DPS2X_NATIVE_ARCH=OFF', '-DPS2X_OPTIMIZE_RUNTIME_LIB=ON',
    '-DPS2X_BUILD_ANALYZER=OFF', '-DPS2X_BUILD_STUDIO=OFF', '-DPS2X_BUILD_TEST=OFF', '-DPS2X_SHOW_WINDOWS_CONSOLE=OFF',
    "-DPS2X_VULKAN_LIBRARY=$((Resolve-Path "$B\libvulkan-1.a").Path)",
    "-DPS2X_GS_SHADER_PREBUILT_DIR=$((Resolve-Path 'vendor\gs_shaders').Path)",
    "-DPS2X_FFMPEG_URL=$((Resolve-Path 'vendor\ffmpeg.zip').Path -replace '\\', '/')", '-DFETCHCONTENT_FULLY_DISCONNECTED=ON')
foreach ($d in Get-ChildItem vendor\deps -Directory) {
    $opcoes += "-DFETCHCONTENT_SOURCE_DIR_$($d.Name.ToUpper())=$($d.FullName)"
}
Rode cmake $opcoes

Passo '4/6 Recompilando o ELF para C++'
if (-not (Test-Path 'PS2Recomp\ps2xRuntime\src\runner\.mksm_ok')) {
    Rode cmake @('--build', $B, '-j', $Jobs, '--target', 'ps2_recomp')
    Rode "$B\ps2xRecomp\ps2_recomp.exe" @('sm.toml')
    Copy-Item output\*.cpp PS2Recomp\ps2xRuntime\src\runner\ -Force
    Copy-Item output\*.h PS2Recomp\ps2xRuntime\include\ -Force
    New-Item PS2Recomp\ps2xRuntime\src\runner\.mksm_ok -ItemType File -Force | Out-Null
}

Passo '5/6 Compilando o jogo (a primeira vez leva algumas horas; pode deixar rodando)'
Rode cmake @('--build', $B, '-j', $Jobs, '--target', 'ps2EntryRunner')

Passo '6/6 Montando a pasta do jogo'
$Out = 'portatil\MKSM-windows'
New-Item -ItemType Directory -Force "$Out\bin", "$Out\jogo" | Out-Null
Copy-Item vendor\modelo_portatil\* $Out -Recurse -Force -Exclude mksm.ini
if (-not (Test-Path "$Out\mksm.ini")) { Copy-Item vendor\modelo_portatil\mksm.ini $Out } # não apaga a configuração de quem já joga
Copy-Item "$B\ps2xRuntime\ps2EntryRunner.exe" "$Out\bin\MKSM.exe" -Force
Copy-Item "$B\ThirdParty\ffmpeg-prefix\src\ffmpeg_external\bin\*.dll" "$Out\bin\" -Force
foreach ($item in Get-ChildItem extraido_full) {
    if ($item.Name -in 'mc0', 'mc1' -and (Test-Path "$Out\jogo\$($item.Name)")) { continue } # saves de quem já joga
    Move-Item $item.FullName "$Out\jogo\" -Force
}

Passo 'Limpando (apaga o kit e o que foi intermediário; o jogo fica em portatil\MKSM-windows)'
foreach ($p in 'ferramentas', 'vendor', 'PS2Recomp', 'output', 'extraido', 'extraido_full') {
    if (Test-Path $p) { Remove-Item $p -Recurse -Force }
}
Write-Host ""
Write-Host "Pronto! Abra a pasta portatil\MKSM-windows e dê dois cliques em Jogar.bat." -ForegroundColor Green
