# Runs the newest self-test ROM in the isolated Vortex64 copy (builds/emu) with its own
# APPDATA/LOCALAPPDATA, so the user's real emulator settings and saves are never touched.
param([int]$Seconds = 300, [string]$Rom = "", [string]$Profile = "profile", [string]$EmuArgs = "", [int]$ShotEvery = 0)
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$emu = "$root\builds\emu"
$rom = if ($Rom) { (Resolve-Path $Rom).Path } else { (Get-ChildItem "$root\builds" -Filter "sm64_selftest_*.us.z64" | Sort-Object LastWriteTime | Select-Object -Last 1).FullName }
$rdbEntry = Get-Content "$rom.rdb.txt" -Raw
$hdr = ($rdbEntry -split "`n")[0].Trim()
$rdb = "$emu\Config\Project64.rdb"
if (-not (Select-String -Path $rdb -SimpleMatch $hdr -Quiet)) {
    Add-Content -Path $rdb -Value ("`r`n" + $rdbEntry.Replace("`n", "`r`n")) -Encoding ascii
}
$profile = "$emu\$Profile"
New-Item -ItemType Directory -Force "$profile\appdata", "$profile\localappdata" | Out-Null
$oldA = $env:APPDATA; $oldL = $env:LOCALAPPDATA
# The debug build of Vortex64 sometimes hangs in plugin init before running a single
# instruction: detect it ("ROM loaded and running" never printed) and relaunch.
$started = $false
for ($try = 1; $try -le 3 -and -not $started; $try++) {
    $env:APPDATA = "$profile\appdata"; $env:LOCALAPPDATA = "$profile\localappdata"
    try {
        $p = Start-Process "$emu\vortex64-cli.exe" -ArgumentList ("`"$rom`" " + $EmuArgs) -WorkingDirectory $emu `
            -RedirectStandardOutput "$emu\last_run.txt" -RedirectStandardError "$emu\last_run_err.txt" -PassThru
    } finally { $env:APPDATA = $oldA; $env:LOCALAPPDATA = $oldL }
    Start-Sleep -Seconds 20
    $started = Select-String -Path "$emu\last_run.txt" -SimpleMatch "ROM loaded and running" -Quiet
    if (-not $started) {
        "emulator stuck in init (attempt $try), relaunching"
        Stop-Process $p -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
    }
}
if (-not $started) { "emulator never started"; exit 1 }
if ($ShotEvery -gt 0) {
    # screenshots of the emulator window, to SEE what the AI builds
    Add-Type -AssemblyName System.Drawing
    if (-not ("LivingWin" -as [type])) {
        Add-Type @"
using System; using System.Runtime.InteropServices;
public struct LRect { public int L, T, R, B; }
public static class LivingWin {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out LRect r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
    }
    $shots = Join-Path $root ("builds\shots\" + (Split-Path -Leaf $profile))  # $profile = emulator profile dir (PowerShell is case-insensitive)
    New-Item -ItemType Directory -Force $shots | Out-Null
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt ($Seconds - 20)) {
        Start-Sleep -Seconds $ShotEvery
        $p.Refresh()
        $h = $p.MainWindowHandle
        if ($h -eq [IntPtr]::Zero) { continue }
        [LivingWin]::SetForegroundWindow($h) | Out-Null
        $r = New-Object LRect
        [LivingWin]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.R - $r.L; $hh = $r.B - $r.T
        if ($w -lt 50 -or $hh -lt 50) { continue }
        $bmp = New-Object System.Drawing.Bitmap $w, $hh
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
        $bmp.Save(("$shots\shot_{0:D3}.png" -f [int]((Get-Date) - $t0).TotalSeconds), [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
    }
} else {
    Start-Sleep -Seconds ([Math]::Max(0, $Seconds - 20))
}
if (-not $p.HasExited) { Stop-Process $p -Force }
$eep = Get-ChildItem "$profile\localappdata" -Recurse -Filter *.eep | Sort-Object LastWriteTime | Select-Object -Last 1
"ROM: $rom"
if ($eep) {
    $dst = "$root\builds\runs\" + [IO.Path]::GetFileNameWithoutExtension($rom) + ".eep"
    New-Item -ItemType Directory -Force "$root\builds\runs" | Out-Null
    Copy-Item $eep.FullName $dst -Force
    python "$root\tools\read_living_eep.py" $dst
} else { "no .eep written" }
