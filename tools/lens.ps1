# Pomocky na nahliadnutie do beziacej AgentAloud z ineho procesu.
#
# Preco to existuje: appka sa da preskusat aj bez oci, ale nie kazdou cestou.
# WM_GETTEXT sa cez hranicu procesu marshaluje, GetWindowText nie -- ten cez
# hranicu vrati prazdny retazec, cize ZLYHA TICHO a vyzera to, akoby bolo pole
# prazdne. Preto Lens::Text posiela WM_GETTEXT rucne.
#
# Klavesy sa takto posielat NEDAJU. SendKeys aj keybd_event idu do okna
# v popredi, cize by pristali u pouzivatela, nie v testovanej instancii.
# PostMessage WM_KEYDOWN priamo prvku funguje a je to legitimne overenie pre
# klavesu, ktoru chyta subclass procedura (F1, F2, F4, chordy) -- tie sa
# obsluhuju v tej istej procedure, do ktorej by prisla aj skutocna sprava.
# Pre klavesu, ktoru rozhoduje az dialogova slucka alebo akceleratory, to
# nedokazuje nic.
#
# SendMessage do modalneho dialogu tento shell ZABLOKUJE, kym sa dialog
# nezavrie -- SendMessage caka na navrat. Na otvorenie dialogu teda
# PostMessage, nie SendMessage.
#
# Dot-source pred kazdym pouzitim: shell si stav medzi volaniami nedrzi.
#
#   . tools/lens.ps1
#   $w = Get-LensWindows <pid>
#   Get-LensChildren $w[0].Hwnd
#
# Testovaciu instanciu zatvaraj PODLA PID, nikdy taskkill /IM -- pouzivatel ma
# vlastnu AgentAloud spustenu.

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct GUITHREADINFO {
  public int cbSize; public int flags;
  public IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret;
  public int left, top, right, bottom;
}

public class Lens {
  public delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, IntPtr p);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, StringBuilder s, int max);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wp, StringBuilder lp);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint tid, ref GUITHREADINFO gti);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);

  public static string Text(IntPtr hwnd) {
    int len = (int)SendMessageW(hwnd, 0x000E, IntPtr.Zero, IntPtr.Zero); // WM_GETTEXTLENGTH
    StringBuilder sb = new StringBuilder(len + 2);
    SendMessageW(hwnd, 0x000D, (IntPtr)(len + 1), sb); // WM_GETTEXT
    return sb.ToString();
  }
  public static string Cls(IntPtr hwnd) {
    StringBuilder sb = new StringBuilder(256);
    GetClassName(hwnd, sb, 256);
    return sb.ToString();
  }
}
"@ -ErrorAction SilentlyContinue

function Get-LensWindows([int]$TargetPid) {
  $found = New-Object System.Collections.ArrayList
  $cb = [Lens+EnumProc]{
    param($h, $l)
    $p = 0
    [void][Lens]::GetWindowThreadProcessId($h, [ref]$p)
    if ($p -eq $TargetPid -and [Lens]::IsWindowVisible($h)) {
      [void]$found.Add([pscustomobject]@{ Hwnd = $h; Class = [Lens]::Cls($h); Text = [Lens]::Text($h) })
    }
    return $true
  }
  [void][Lens]::EnumWindows($cb, [IntPtr]::Zero)
  return $found
}

function Get-LensChildren([IntPtr]$Parent) {
  $found = New-Object System.Collections.ArrayList
  $cb = [Lens+EnumProc]{
    param($h, $l)
    [void]$found.Add([pscustomobject]@{ Hwnd = $h; Class = [Lens]::Cls($h); Text = [Lens]::Text($h) })
    return $true
  }
  [void][Lens]::EnumChildWindows($Parent, $cb, [IntPtr]::Zero)
  return $found
}

# Ktory prvok ma fokus. Odpoved na "precita to NVDA hned po otvoreni dialogu",
# lebo NVDA cita titulok a potom prave tento prvok. GetFocus sama by vratila
# fokus TOHTO vlakna, cize nic -- pyta sa cez vlakno cudzieho okna.
function Get-LensFocus([IntPtr]$Window) {
  $tid = [Lens]::GetWindowThreadProcessId($Window, [IntPtr]::Zero)
  $g = New-Object GUITHREADINFO
  $g.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($g)
  [void][Lens]::GetGUIThreadInfo($tid, [ref]$g)
  return [pscustomobject]@{ Hwnd = $g.hwndFocus; Class = [Lens]::Cls($g.hwndFocus); Text = [Lens]::Text($g.hwndFocus) }
}

# WM_KEYDOWN priamo prvku. Virtualne kody: F1 = 0x70, F2 = 0x71, F4 = 0x73,
# Esc = 0x1B, Enter = 0x0D.
function Send-LensKey([IntPtr]$Control, [int]$VirtualKey) {
  [void][Lens]::PostMessageW($Control, 0x0100, [IntPtr]$VirtualKey, [IntPtr]0)
}
