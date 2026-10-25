# tests/console-steer.ps1 - mensagem digitada NO MEIO do stream corta a resposta
# corrente e e' entregue ANTES da mensagem final do agente.
# Prova pela sessao (JSONL): o assistant imediatamente anterior a mensagem do
# usuario esta' TRUNCADO (nao contem o ultimo pedaco do eco do mock).
param([string]$Root, [int]$Port = 8481)
Set-Location $Root

$sess = "$env:USERPROFILE\.deltaCore\sessions"
Get-ChildItem $sess -Recurse -Filter *.jsonl -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

$cfg = "$env:USERPROFILE\.deltaCore\config.json"
$auth = "$env:USERPROFILE\.deltaCore\auth.json"
$cfgAntes = Get-Content $cfg -Raw -ErrorAction SilentlyContinue
$authAntes = Get-Content $auth -Raw -ErrorAction SilentlyContinue
@"
{
  "provider": "openai",
  "model": "gpt-4o",
  "baseUrl": "http://127.0.0.1:$Port/v1",
  "thinkingLevel": "off",
  "maxTokens": 8192
}
"@ | Set-Content -Encoding ascii $cfg
'{ "openai": "sk-teste" }' | Set-Content -Encoding ascii $auth

# stream lento: 0.45 s entre pedacos (~4 s a resposta final)
$mock = Start-Process -FilePath 'python' -ArgumentList 'tests/mock_openai.py', "$Port", 0, 0.5 -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

$p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', 'bin\delta.exe --no-altscreen --api-key sk-teste' -PassThru
Start-Sleep -Seconds 3
$c = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)" |
     Where-Object { $_.Name -eq 'delta.exe' } | Select-Object -First 1
if (-not $c) { "FAIL delta.exe nao iniciou"; exit 1 }

$ws = New-Object -ComObject WScript.Shell
[void]$ws.AppActivate($c.ProcessId)
Start-Sleep -Milliseconds 700

$ws.SendKeys("leia tests/mock-target.txt")
Start-Sleep -Milliseconds 250
$ws.SendKeys("{ENTER}")

# espera a tool terminar (o toolResult entra no JSONL na hora) e cai no
# comeco do texto final: e' ali que o corte precisa acontecer
$arq0 = $null
for ($i = 0; $i -lt 100; $i++) {
    Start-Sleep -Milliseconds 150
    $arq0 = Get-ChildItem $sess -Recurse -Filter *.jsonl -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $arq0) { continue }
    $temTool = $false
    foreach ($l in (Get-Content $arq0.FullName)) {
        if ($l -match '"role":"tool"') { $temTool = $true }
    }
    if ($temTool) { break }
}
Start-Sleep -Milliseconds 700          # ja' esta' escrevendo o texto final
$ws.SendKeys("mude o foco para o resumo")
Start-Sleep -Milliseconds 250
$ws.SendKeys("{ENTER}")
Start-Sleep -Seconds 18

$arquivo = Get-ChildItem $sess -Recurse -Filter *.jsonl -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
$linhas = if ($arquivo) { Get-Content $arquivo.FullName } else { @() }

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $c.ProcessId -Force -ErrorAction SilentlyContinue
Stop-Process -Id $mock.Id -Force -ErrorAction SilentlyContinue

if ($null -ne $cfgAntes) { $cfgAntes | Set-Content -Encoding ascii $cfg } else { Remove-Item $cfg -ErrorAction SilentlyContinue }
if ($null -ne $authAntes) { $authAntes | Set-Content -Encoding ascii $auth } else { Remove-Item $auth -ErrorAction SilentlyContinue }

$antes = ""
$achou = $false
$depois = ""
foreach ($l in $linhas) {
    try { $d = $l | ConvertFrom-Json } catch { continue }
    if (-not $d.message) { continue }
    $role = $d.message.role
    $txt = [string]$d.message.content
    if ($role -eq 'user' -and $txt -match 'mude o foco') { $achou = $true; continue }
    if (-not $achou -and $role -eq 'assistant' -and $txt.Length -gt 0) { $antes = $txt }
    if ($achou -and $role -eq 'assistant' -and $txt.Length -gt 0) { $depois = $txt }
}

if (-not $achou) { "FAIL mensagem do usuario nao chegou ao modelo"; exit 1 }
if ($antes -match 'eco: leia tests/mock-target.txt') {
    "FAIL a resposta anterior nao foi cortada (a mensagem chegou no fim do turno)"
    exit 1
}
if ($depois -notmatch 'mude o foco') {
    "FAIL o modelo nao respondeu a mensagem nova"
    exit 1
}
$mostra = if ($antes.Length -gt 0) { $antes.Substring(0,[Math]::Min(40,$antes.Length)) } else { "(cortado antes do texto)" }
"PASS stream cortado no meio: '$mostra...' -> mensagem entregue"
