param([string]$KeilRoot = 'D:\project\keil5_project\Core')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$uv = Join-Path $KeilRoot 'UV4\UV4.exe'
$fromelf = Join-Path $KeilRoot 'ARM\ARMCC\bin\fromelf.exe'
foreach ($tool in @($uv, $fromelf)) {
    if (!(Test-Path -LiteralPath $tool)) { throw "Missing tool: $tool" }
}
$targets = @(
    @{Project='Bootloader\bootloader.uvprojx'; Output='OBJ_BL'; Name='Bootloader'; Limit=8192},
    @{Project='USER\project.uvprojx'; Output='OBJ'; Name='APP'; Limit=57344}
)
foreach ($target in $targets) {
    $project = Join-Path $repo $target.Project
    $output = Join-Path $repo $target.Output
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $log = Join-Path $output ('upgrade-'+[guid]::NewGuid().ToString('N')+'.log')
    $process = Start-Process -FilePath $uv -ArgumentList ('-r "'+$project+'" -o "'+$log+'"') -PassThru -WindowStyle Hidden
    $process.WaitForExit()
    $result = Get-Content -LiteralPath $log -Raw
    if ($process.ExitCode -gt 1 -or $result -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        Write-Output $result
        throw "Keil build failed or reported warnings: $($target.Name)"
    }
    $axf = Join-Path $output ($target.Name+'.axf')
    $bin = Join-Path $output ($target.Name+'.bin')
    & $fromelf --bin --output $bin $axf
    if ($LASTEXITCODE -ne 0) { throw 'Binary export failed' }
    $bytes = (Get-Item -LiteralPath $bin).Length
    if ($bytes -gt $target.Limit) { throw 'Image exceeds its flash partition' }
    Write-Output "$($target.Name): 0 errors / 0 warnings; BIN $bytes / $($target.Limit) bytes"
    $result -split "`r?`n" | Where-Object { $_ -match 'Program Size:' } | Write-Output
}
