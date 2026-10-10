# Pomocky na nahliadnutie do beziacej AgentAloud z ineho procesu.
#
# Preco to existuje: appka sa da preskusat aj bez oci, ale nie kazdou cestou.
# WM_GETTEXT sa cez hranicu procesu marshaluje, GetWindowText nie -- ten cez
# hranicu vrati prazdny retazec, cize ZLYHA TICHO a vyzera to, akoby bolo pole
# prazdne. Preto Lens::Text posiela WM_GETTEXT rucne.
#
# SendKeys ani keybd_event sa pouzit NESMU: idu do okna v popredi, cize by
# pristali u pouzivatela, nie v testovanej instancii. PostMessage WM_KEYDOWN
# priamo prvku funguje a je to legitimne overenie pre klavesu, ktoru chyta
# subclass procedura (F1, F2, F4) -- obsluhuje ju ta ista procedura, do ktorej
# by prisla aj skutocna sprava. Chord s Ctrl alebo Shift posiela
# Send-LensChord, ktory modifikator nastavi cez AttachThreadInput +
# SetKeyboardState (nizsie). Pre klavesu, ktoru rozhoduje az dialogova slucka
# alebo akceleratory, nic z toho nedokazuje nic.
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
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wp, string lp);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("user32.dll")] public static extern bool GetKeyboardState(byte[] s);
  [DllImport("user32.dll")] public static extern bool SetKeyboardState(byte[] s);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();

  // A key with modifiers held, as the target thread's GetKeyState sees them.
  // On a fresh thread, because AttachThreadInput shares the key state of the
  // two threads until it is undone, and the shell's own thread must not be
  // left sharing it.  Nothing goes to the foreground window.
  public static bool Chord(IntPtr control, int key, bool ctrl, bool shift) {
    bool ok = false;
    System.Threading.Thread t = new System.Threading.Thread(() => {
      uint me = GetCurrentThreadId();
      uint target = GetWindowThreadProcessId(control, IntPtr.Zero);
      if (!AttachThreadInput(me, target, true)) return;
      byte[] state = new byte[256];
      GetKeyboardState(state);
      byte[] saved = (byte[])state.Clone();
      if (ctrl) { state[0x11] = 0x80; state[0xA2] = 0x80; }
      if (shift) { state[0x10] = 0x80; state[0xA0] = 0x80; }
      ok = SetKeyboardState(state);
      PostMessageW(control, 0x0100, (IntPtr)key, IntPtr.Zero);  // WM_KEYDOWN
      // The message is handled on the target's thread; the state must still
      // be set when it is.
      System.Threading.Thread.Sleep(300);
      SetKeyboardState(saved);
      AttachThreadInput(me, target, false);
    });
    t.Start();
    t.Join();
    return ok;
  }

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

# Klavesa s Ctrl alebo Shift, napriklad Ctrl+Enter, ktory odosle prompt.
# Procedura okna sa na modifikator pyta cez GetKeyState, a ten PostMessage
# nastavit nevie. Preto AttachThreadInput na vlakno appky + SetKeyboardState:
# stav klaves je po pripojeni spolocny, takze ho appka vidi, a nic sa
# neposiela do okna v popredi. Overene 6. 10. 2026 (claude-gui-lkk.52) --
# Ctrl+Enter odoslal prompt trikrat z troch.
#
#   Set-LensText $prompt 'Run echo hello > hello.txt with Bash.'
#   Send-LensChord $prompt 0x0D -Ctrl
function Send-LensChord([IntPtr]$Control, [int]$VirtualKey, [switch]$Ctrl,
                        [switch]$Shift) {
  return [Lens]::Chord($Control, $VirtualKey, $Ctrl.IsPresent, $Shift.IsPresent)
}

# Text do editacneho pola (WM_SETTEXT), napriklad prompt pred odoslanim.
function Set-LensText([IntPtr]$Control, [string]$Text) {
  [void][Lens]::SendMessageW($Control, 0x000C, [IntPtr]::Zero, $Text)
}

# Casti stavoveho riadka. SB_GETTEXTW system cez hranicu procesu NEmarshaluje
# (na rozdiel od WM_GETTEXT) -- buffer musi lezat v pamati appky, preto
# VirtualAllocEx + ReadProcessMemory. Overene 9. a 10. 10. 2026 (b8n.4, b8n.6).
#
#   Get-LensStatus <pid>    # -> '[0] na pozadi: 1 prikaz', '[1] model ...'
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class LensBar {
  [DllImport("user32.dll")] static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, UIntPtr s, uint t, uint pr);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr p, IntPtr a, UIntPtr s, uint t);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, UIntPtr s, out UIntPtr r);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  public static string[] Parts(IntPtr bar) {
    uint pid; GetWindowThreadProcessId(bar, out pid);
    IntPtr proc = OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, false, pid);
    int n = (int)SendMessageW(bar, 0x0406, IntPtr.Zero, IntPtr.Zero); // SB_GETPARTS
    string[] result = new string[n];
    IntPtr mem = VirtualAllocEx(proc, IntPtr.Zero, (UIntPtr)8192, 0x3000, 0x04);
    for (int i = 0; i < n; i++) {
      SendMessageW(bar, 0x040D, (IntPtr)i, mem); // SB_GETTEXTW
      byte[] buf = new byte[8192]; UIntPtr r;
      ReadProcessMemory(proc, mem, buf, (UIntPtr)8192, out r);
      string s = Encoding.Unicode.GetString(buf);
      int z = s.IndexOf('\0'); result[i] = z >= 0 ? s.Substring(0, z) : s;
    }
    VirtualFreeEx(proc, mem, UIntPtr.Zero, 0x8000);
    CloseHandle(proc);
    return result;
  }
}
"@ -ErrorAction SilentlyContinue

function Get-LensStatus([int]$TargetPid) {
  foreach ($w in Get-LensWindows $TargetPid) {
    foreach ($c in Get-LensChildren $w.Hwnd) {
      if ($c.Class -ne 'msctls_statusbar32') { continue }
      $i = 0
      foreach ($p in [LensBar]::Parts($c.Hwnd)) { "[$i] $p"; $i++ }
    }
  }
}
