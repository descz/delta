param([string]$Root)
Set-Location $Root

function Measure-App {
    param([string]$Name, [string]$Exe, [string[]]$AppArgs)

    if ($null -eq $AppArgs) { $AppArgs = @() }
    if ($AppArgs.Count -eq 0) {
        $p = Start-Process -FilePath $Exe -PassThru -WindowStyle Hidden
    } else {
        $p = Start-Process -FilePath $Exe -ArgumentList $AppArgs -PassThru -WindowStyle Hidden
    }
    Start-Sleep -Milliseconds 2500
    $p.Refresh()
    $ws = [math]::Round($p.WorkingSet64 / 1MB, 2)
    $priv = [math]::Round($p.PrivateMemorySize64 / 1MB, 2)
    $peak = [math]::Round($p.PeakWorkingSet64 / 1MB, 2)
    Write-Host ("{0,-16} WS={1,7} MB   Private={2,7} MB   Peak={3,7} MB" -f $Name, $ws, $priv, $peak)
    try { $p.Kill() } catch {}
    Start-Sleep -Milliseconds 300
}

Measure-App -Name "hello-static" -Exe "$Root\bin\hello-static.exe"
Measure-App -Name "hello-dyn"    -Exe "$Root\bin\hello-dyn.exe"
Measure-App -Name "Delta -p"   -Exe "$Root\bin\delta.exe" -AppArgs @('-p','oi','--provider','openai','--model','gpt-4o','--baseurl','http://127.0.0.1:8096/v1','--api-key','sk-t')
Measure-App -Name "node idle"    -Exe "node" -AppArgs @('-e','setInterval(()=>{},1000)')
Measure-App -Name "node cli"     -Exe "node" -AppArgs @('-e','const a=[];for(let i=0;i<200000;i++)a.push({i});setInterval(()=>{},1000)')
