# tests/set-dib.ps1 - coloca um CF_DIB de verdade no clipboard (para testar o Ctrl+V de imagem)
# Uso: powershell -STA -File tests/set-dib.ps1 [largura] [altura]
param([int]$W = 64, [int]$H = 32)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Clip {
  [DllImport("user32.dll")] public static extern bool OpenClipboard(IntPtr h);
  [DllImport("user32.dll")] public static extern bool EmptyClipboard();
  [DllImport("user32.dll")] public static extern IntPtr SetClipboardData(uint f, IntPtr h);
  [DllImport("user32.dll")] public static extern bool CloseClipboard();
  [DllImport("kernel32.dll")] public static extern IntPtr GlobalAlloc(uint flags, UIntPtr size);
  [DllImport("kernel32.dll")] public static extern IntPtr GlobalLock(IntPtr h);
  [DllImport("kernel32.dll")] public static extern bool GlobalUnlock(IntPtr h);
}
"@

$linha = [int]((($W * 3 + 3) / 4) * 4)
$tam = 40 + $linha * $H
$hMem = [Clip]::GlobalAlloc(0x42, [UIntPtr]::new([uint64]$tam))      # MOVEABLE | ZEROINIT
$p = [Clip]::GlobalLock($hMem)

$bytes = New-Object byte[] $tam
[BitConverter]::GetBytes([int]40).CopyTo($bytes, 0)        # biSize
[BitConverter]::GetBytes([int]$W).CopyTo($bytes, 4)        # biWidth
[BitConverter]::GetBytes([int]$H).CopyTo($bytes, 8)        # biHeight (positivo = bottom-up)
[BitConverter]::GetBytes([int16]1).CopyTo($bytes, 12)      # biPlanes
[BitConverter]::GetBytes([int16]24).CopyTo($bytes, 14)     # biBitCount
[BitConverter]::GetBytes([int]0).CopyTo($bytes, 16)        # BI_RGB
[BitConverter]::GetBytes([int]($linha * $H)).CopyTo($bytes, 20)

# metade vermelha, metade azul (BGR)
for ($y = 0; $y -lt $H; $y++) {
    for ($x = 0; $x -lt $W; $x++) {
        $o = 40 + $y * $linha + $x * 3
        if ($x -lt ($W / 2)) { $bytes[$o] = 0; $bytes[$o+1] = 0; $bytes[$o+2] = 255 }
        else                 { $bytes[$o] = 255; $bytes[$o+1] = 0; $bytes[$o+2] = 0 }
    }
}
[System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $p, $tam)
[Clip]::GlobalUnlock($hMem) | Out-Null

$abriu = [Clip]::OpenClipboard([IntPtr]::Zero)
$vazio = [Clip]::EmptyClipboard()
$set   = [Clip]::SetClipboardData(8, $hMem)                # CF_DIB = 8
$err   = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
[Clip]::CloseClipboard() | Out-Null
"  open=$abriu empty=$vazio set=$set err=$err hmem=$hMem p=$p"

"CF_DIB no clipboard: ${W}x${H}"

Start-Sleep -Seconds 6
