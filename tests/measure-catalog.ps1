param([string]$Root)
Set-Location $Root
$exe = "$Root\bin\delta.exe"
$cliArgs = @('--replay', 'tests/replay-picker.txt', '110', '32')

function Medir($nome) {
    $p = Start-Process -FilePath $exe -ArgumentList $cliArgs -PassThru -WindowStyle Hidden
    if ($null -eq $p) { Write-Host "  falha ao iniciar"; return }
    $pico = 0
    $tentativas = 0
    while ($tentativas -lt 3000) {
        $tentativas++
        try {
            $p.Refresh()
            if ($p.HasExited) { break }
            if ($p.PeakWorkingSet64 -gt $pico) { $pico = $p.PeakWorkingSet64 }
        } catch { break }
        Start-Sleep -Milliseconds 10
    }
    "{0,-28} pico = {1,7:N2} MB" -f $nome, ($pico / 1MB)
}

Medir "cache quente"
Remove-Item "$env:USERPROFILE\.deltaCore\models.json" -ErrorAction SilentlyContinue
Medir "baixando 5,7 MB (1a vez)"
Medir "cache regravado"
