# tests/console-while-working.ps1 - mensagem enviada ENQUANTO o agente gera
# Prova pelo arquivo de sessao (JSONL): a mensagem digitada durante o stream
# entra na fila e e entregue ao modelo, sem esperar o turno terminar.
param([string]$Root, [int]$Port = 8471)
Set-Location $Root

$sess = "$env:USERPROFILE\.deltaCore\sessions"
Get-ChildItem $sess -Recurse -Filter *.jsonl -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

$cfg = "$env:USERPROFILE\.deltaCore\config.json"
$auth = "$env:USERPROFILE\.deltaCoreuth.json"
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
'{ "openai": "sk-teste" }' | Set-Content -Encoding ascii "$env:USERPROFILE\.deltaCore\auth.json"

$mock = Start-Process -FilePath 'python' -ArgumentList 'tests/mock_openai.py', "$Port", 0, 0.3 -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 2

$p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', 'bin\delta.exe --no-altscreen --api-key sk-teste' -PassThru
Start-Sleep -Seconds 3
$c = Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)" |
     Where-Object { $_.Name -eq 'delta.exe' } | Select-Object -First 1
if (-not $c) { "FAIL delta.exe nao iniciou"; exit 1 }

$ws = New-Object -ComObject WScript.Shell
[void]$ws.AppActivate($c.ProcessId)
Start-Sleep -Milliseconds 700

$ws.SendKeys("leia tests/mock-target.txt")      # dispara o turno
Start-Sleep -Milliseconds 250
$ws.SendKeys("{ENTER}")

Start-Sleep -Milliseconds 1200                   # ainda gerando (stream lento)
$ws.SendKeys("mensagem na fila")
Start-Sleep -Milliseconds 250
$ws.SendKeys("{ENTER}")
Start-Sleep -Milliseconds 1200
$ws.SendKeys("{UP}{UP}{UP}")                     # rolar durante a geracao tambem
Start-Sleep -Seconds 16                          # deixa o turno e a fila rodarem

$arquivo = Get-ChildItem $sess -Recurse -Filter *.jsonl -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
$texto = if ($arquivo) { Get-Content $arquivo.FullName -Raw } else { "" }

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $c.ProcessId -Force -ErrorAction SilentlyContinue
Stop-Process -Id $mock.Id -Force -ErrorAction SilentlyContinue

# devolve a configuracao do usuario
if ($null -ne $cfgAntes) { $cfgAntes | Set-Content -Encoding ascii $cfg } else { Remove-Item $cfg -ErrorAction SilentlyContinue }
if ($null -ne $authAntes) { $authAntes | Set-Content -Encoding ascii $auth } else { Remove-Item $auth -ErrorAction SilentlyContinue }

if ($texto -match 'mensagem na fila') { "PASS mensagem enviada durante a geracao chegou ao modelo" }
else { "FAIL mensagem da fila nao aparece na sessao" }
