Add-Type -AssemblyName System.Windows.Forms
$ErrorActionPreference = 'Continue' 
# tests/console-paste.ps1 - Ctrl+V no console de verdade, com a sessao como prova
# O conhost cola o texto sozinho (a tecla 'V' nunca chega ao app), entao o teste
# confere o resultado final: a mensagem enviada tem o texto colado, uma vez so.
param([string]$Root, [int]$Port = 8066)
Set-Location $Root

$sess = "$env:USERPROFILE\.deltaCore\sessions"
Get-ChildItem $sess -Recurse -Filter *.jsonl -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

# config apontando para o servidor falso (com chave), para o turno completar
$cfg = "$env:USERPROFILE\.deltaCore\config.json"
$cfgAntes = Get-Content $cfg -Raw -ErrorAction SilentlyContinue
@"
{
  "provider": "openai",
  "model": "gpt-4o",
  "baseUrl": "http://127.0.0.1:$Port/v1",
  "thinkingLevel": "off",
  "maxTokens": 8192
}
"@ | Set-Content -Encoding ascii $cfg

# chave para o turno completar (fica no auth.json, por provedor)
$auth = "$env:USERPROFILE\.deltaCoreuth.json"
$authAntes = Get-Content $auth -Raw -ErrorAction SilentlyContinue
'{ "openai": "sk-teste" }' | Set-Content -Encoding ascii $auth

$mock = Start-Process -FilePath 'python' -ArgumentList 'tests/mock_openai.py', "$Port" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

# texto no clipboard, com uma quebra no fim como num copiar de verdade
try { [System.Windows.Forms.Clipboard]::SetText("frase colada no console`r`n"); "clipboard de texto ok" } catch { "clipboard FALHOU: $_" }
Start-Sleep -Milliseconds 300

$env:DELTA_EVENT_LOG = "$env:TEMP\delta-paste-ev.log"
Remove-Item $env:DELTA_EVENT_LOG -ErrorAction SilentlyContinue
# o foco da janela e racy: tenta algumas vezes
$resultado = "FAIL"; $detalhe = ""
for ($tent = 1; $tent -le 3 -and $resultado -ne "PASS"; $tent++) {
    Get-ChildItem $sess -Recurse -Filter *.jsonl -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    Remove-Item $env:DELTA_EVENT_LOG -ErrorAction SilentlyContinue

    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','set DELTA_EVENT_LOG=%TEMP%\delta-paste-ev.log && bin\delta.exe --api-key sk-teste' -PassThru
    Start-Sleep -Seconds 3
    $c = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)" |
         Where-Object { $_.Name -eq 'delta.exe' } | Select-Object -First 1
    $ws = New-Object -ComObject WScript.Shell
    [void]$ws.AppActivate($c.ProcessId)
    Start-Sleep -Milliseconds 900
    $ws.SendKeys("^v")          # o console faz a colagem
    Start-Sleep -Milliseconds 1200
    $ws.SendKeys("{ENTER}")     # o Enter da colagem nao pode ter enviado sozinho
    Start-Sleep -Seconds 3

    $eventos = (Get-Content $env:DELTA_EVENT_LOG -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Stop-Process -Id $c.ProcessId -Force -ErrorAction SilentlyContinue

    $f = Get-ChildItem $sess -Recurse -Filter *.jsonl -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $f) { $detalhe = "tentativa ${tent}: sem sessao (eventos: $eventos)"; continue }

    $u = 0; $ok = 0; $placeholder = 0
    foreach ($l in Get-Content $f.FullName) {
        $d = $null
        try { $d = $l | ConvertFrom-Json } catch { continue }
        if ($d.type -ne 'message') { continue }
        if ($d.message.role -ne 'user') { continue }
        $u++
        $txt = [string]$d.message.content
        if ($txt -like "*frase colada no console*") { $ok++ }
        if ($txt -like "*{paste*") { $placeholder++ }
    }
    $detalhe = "tentativa ${tent}: eventos=$eventos mensagens=$u colado=$ok cru=$placeholder"
    if ($u -eq 1 -and $ok -eq 1 -and $placeholder -eq 0) { $resultado = "PASS" }
}

$detalhe
if ($resultado -eq "PASS") { "PASS" } else { "FAIL" }

# ---- imagem: Ctrl+V com um CF_DIB de verdade ----
Get-ChildItem "$env:USERPROFILE\.deltaCore\paste\*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
& powershell -STA -NoProfile -ExecutionPolicy Bypass -File tests/set-dib.ps1 24 24 | Out-Null
Start-Sleep -Milliseconds 400

$p2 = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c','bin\delta.exe --api-key sk-teste' -PassThru
Start-Sleep -Seconds 3
$c2 = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p2.Id)" |
      Where-Object { $_.Name -eq 'delta.exe' } | Select-Object -First 1
[void]$ws.AppActivate($c2.ProcessId)
Start-Sleep -Milliseconds 600
$ws.SendKeys("^v")
Start-Sleep -Seconds 2
Stop-Process -Id $p2.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $c2.ProcessId -Force -ErrorAction SilentlyContinue

$png = Get-ChildItem "$env:USERPROFILE\.deltaCore\paste\*.png" -ErrorAction SilentlyContinue
if ($png) { "imagem: PNG criado ($($png.Count)) -> " + $png[0].Name } else { "imagem: NENHUM PNG" }

if ($cfgAntes) { $cfgAntes | Set-Content -Encoding ascii $cfg }
if ($authAntes) { $authAntes | Set-Content -Encoding ascii $auth }
