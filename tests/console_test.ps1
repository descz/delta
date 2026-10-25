# tests/console_test.ps1
# Testa a TUI de verdade: inicia delta.exe num console, envia teclas e le o buffer da tela.
param([string]$Root, [string]$Out, [string]$Extra = "--no-altscreen")
Set-Location $Root

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class Con {
    [StructLayout(LayoutKind.Sequential)] public struct COORD { public short X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct SMALL_RECT { public short L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct CSBI {
        public COORD dwSize; public COORD dwCursorPosition; public ushort wAttributes;
        public SMALL_RECT srWindow; public COORD dwMaximumWindowSize;
    }
    [DllImport("kernel32.dll")] public static extern bool FreeConsole();
    [DllImport("kernel32.dll")] public static extern bool AttachConsole(uint pid);
    [DllImport("kernel32.dll")] public static extern IntPtr GetStdHandle(int n);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    public static extern bool ReadConsoleOutputCharacterW(IntPtr h, StringBuilder sb, uint len, COORD pos, out uint read);
    [DllImport("kernel32.dll")] public static extern bool GetConsoleScreenBufferInfo(IntPtr h, out CSBI info);

    public static string Dump(uint pid) {
        var sb = new StringBuilder();
        FreeConsole();
        if (!AttachConsole(pid)) { return "<nao consegui anexar ao console " + pid + ">\n"; }
        IntPtr h = GetStdHandle(-11);
        CSBI info;
        GetConsoleScreenBufferInfo(h, out info);
        int width = info.dwSize.X;
        if (width <= 0) width = 120;
        int rows = info.srWindow.B - info.srWindow.T + 1;
        sb.Append("(buffer ").Append(width).Append("x").Append(info.dwSize.Y)
          .Append(" janela ").Append(rows).Append(" linhas)\n");
        for (int y = 0; y < rows; y++) {
            var line = new StringBuilder(width + 1);
            uint got = 0;
            ReadConsoleOutputCharacterW(h, line, (uint)width, new COORD { X = 0, Y = (short)y }, out got);
            sb.Append("|").Append(line.ToString().TrimEnd()).Append("|\n");
        }
        FreeConsole();
        return sb.ToString();
    }
}
"@

$log = New-Object System.Text.StringBuilder
function Log($t) { [void]$log.AppendLine($t) }

$p = Start-Process -FilePath 'cmd.exe' -ArgumentList '/c',"bin\delta.exe $Extra" -PassThru
Start-Sleep -Seconds 3
$child = (Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)" |
          Where-Object { $_.Name -eq 'delta.exe' } | Select-Object -First 1)
if (-not $child) { "ERRO: delta.exe nao iniciou" | Set-Content $Out; exit 1 }
$target = [uint32]$child.ProcessId

$ws = New-Object -ComObject WScript.Shell
[void]$ws.AppActivate($target)
Start-Sleep -Milliseconds 500

Log "===== [1] TELA INICIAL ====="
Log ([Con]::Dump($target))

$ws.SendKeys("/help")
Start-Sleep -Milliseconds 700
Log "===== [2] TYPED /help (antes do Enter) ====="
Log ([Con]::Dump($target))

$ws.SendKeys("{ENTER}")
Start-Sleep -Milliseconds 1500
Log "===== [3] DEPOIS DO ENTER ====="
Log ([Con]::Dump($target))

$ws.SendKeys("teste de digitacao")
Start-Sleep -Milliseconds 900
Log "===== [4] TEXTO DIGITADO ====="
Log ([Con]::Dump($target))

$ws.SendKeys("{ENTER}")
Start-Sleep -Milliseconds 1500
Log "===== [5] ENVIADO (sem chave de API) ====="
Log ([Con]::Dump($target))

$log.ToString() | Set-Content -Encoding UTF8 $Out
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $target -Force -ErrorAction SilentlyContinue
