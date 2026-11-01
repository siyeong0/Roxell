param(
    [string]$DxcPath
)

$ErrorActionPreference = 'Stop'
$repositoryPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
if (!$DxcPath) {
    $DxcPath = Join-Path $repositoryPath 'ThirdParty/dxc/restored/windows-x64/bin/x64/dxc.exe'
}
$outputDirectory = Join-Path $repositoryPath '.build/integer-clear-shaders'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$sourcePath = Join-Path $PSScriptRoot 'D3D12IntegerClear.hlsl'
$shaders = @(
    @{ Name = 'VERTEX'; Entry = 'ClearVertex'; Target = 'vs_6_0' },
    @{ Name = 'UNSIGNED'; Entry = 'ClearUnsigned'; Target = 'ps_6_0' },
    @{ Name = 'SIGNED'; Entry = 'ClearSigned'; Target = 'ps_6_0' }
)
$generatedText = [System.Text.StringBuilder]::new()
[void]$generatedText.AppendLine('#pragma once')
[void]$generatedText.AppendLine('')
[void]$generatedText.AppendLine('// Generated from D3D12IntegerClear.hlsl by GenerateD3D12IntegerClear.ps1; do not edit bytes.')
[void]$generatedText.AppendLine('namespace rox::rhi')
[void]$generatedText.AppendLine('{')
foreach ($shader in $shaders) {
    $binaryPath = Join-Path $outputDirectory "$($shader.Name.ToLowerInvariant()).dxil"
    & $DxcPath -T $shader.Target -E $shader.Entry -O3 -Qstrip_debug -Qstrip_reflect -Fo $binaryPath $sourcePath
    if ($LASTEXITCODE -ne 0) {
        throw "DXC failed to compile $($shader.Entry): $LASTEXITCODE"
    }
    $shaderBytes = [System.IO.File]::ReadAllBytes($binaryPath)
    [void]$generatedText.AppendLine("`tinline constexpr unsigned char INTEGER_CLEAR_$($shader.Name)[] =")
    [void]$generatedText.AppendLine("`t{")
    for ($byteIndex = 0; $byteIndex -lt $shaderBytes.Length; $byteIndex += 16) {
        $lastIndex = [Math]::Min($byteIndex + 15, $shaderBytes.Length - 1)
        $hexValues = ($shaderBytes[$byteIndex..$lastIndex] | ForEach-Object { '0x{0:x2}' -f $_ }) -join ', '
        [void]$generatedText.AppendLine("`t`t$hexValues,")
    }
    [void]$generatedText.AppendLine("`t};")
}
[void]$generatedText.AppendLine('}')
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot 'D3D12IntegerClearShaders.h'),
    $generatedText.ToString(), [System.Text.UTF8Encoding]::new($false))
