# Supplementary automated checks, run in the SAME simulation session right after suite.ps1.
# They replace two checks that suite.ps1 can only confirm from a saved frame:
#   S-14  (TC-14) the DOSE footer is on every OLED page and on the offline banner, and follows the dose
#   S-09b (TC-09b) the OLED THERAPY page reflects "Auto bed: ON/OFF" when a manual command is given
# Method: the 128x64 SSD1306 canvas is read as pixels. The footer band (rows 52..63) is hashed per page; a
# frame is only accepted when two reads 90 ms apart are identical (the SSD1306 model repaints progressively).
param([string]$EvidenceDir = (Join-Path $PSScriptRoot '..\..\docs\evidence'))
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'simlib.ps1')
New-Item -ItemType Directory -Force $EvidenceDir | Out-Null; $ev = (Resolve-Path $EvidenceDir).Path; $log = "$ev\supplement_pass.log"; $script:results = @()
function L($m) { $m | Out-File -FilePath $log -Append -Encoding utf8; Write-Output $m }
function Check($id, $name, $ok, $detail) { $script:results += [pscustomobject]@{ id = $id; name = $name; pass = [bool]$ok; detail = $detail }; L ("[{0}] {1} {2} -- {3}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $id, $name, $detail) }
function CSec($c) { $m = [regex]::Match([string]$c, '^(\d\d):(\d\d)\.(\d{3})$'); if ($m.Success) { [int]$m.Groups[1].Value * 60 + [int]$m.Groups[2].Value + [int]$m.Groups[3].Value / 1000.0 } else { -1 } }
function Retry($sb) { for ($i = 0; $i -lt 3; $i++) { try { return (& $sb) } catch { Start-Sleep -Milliseconds 700 } } return $null }
function Sim-Now { CSec (Retry { Sim-Clock }) }
function Sleep-SimUntil($target, $wallCap = 300) { $deadline = (Get-Date).AddSeconds($wallCap); $t = Sim-Now; while ($t -lt $target -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 400; $t = Sim-Now }; return $t }
function Sleep-Sim($sec, $wallCap = 300) { $t0 = Sim-Now; while ($t0 -lt 0) { Start-Sleep -Milliseconds 500; $t0 = Sim-Now }; Sleep-SimUntil ($t0 + $sec) $wallCap }
function Set-Switch($id, $state) { $r = Retry { Sim-Switch $id $state }; if (-not $r) { L ("   [rig] WARNING: no answer while setting $id=$state") } elseif (($r.PSObject.Properties.Name -contains 'ok') -and -not $r.ok) { L ("   [rig] WARNING: switch $id did not reach $state") }; $r }
function EvalJs($js) { (Invoke-RestMethod -Uri "$Base/eval" -Method Post -Body $js -ContentType "text/plain" -TimeoutSec 20).result }

# Stable-frame region signature: hash + count of lit pixels of canvas rows y0..y0+h-1.
function BandSig($y0, $h) {
  $js = "(async () => { const e = document.querySelector('wokwi-ssd1306'); const c = e.shadowRoot.querySelector('canvas'); const sig = () => { const d = c.getContext('2d').getImageData(0, $y0, c.width, $h).data; let x = 0, lit = 0; for (let i = 0; i < d.length; i += 4) { if (d[i] > 100) lit++; x = (x * 31 + d[i]) | 0; } return x + ':' + lit; }; let prev = sig(); for (let i = 0; i < 14; i++) { await new Promise(r => setTimeout(r, 90)); const cur = sig(); if (cur === prev) return cur; prev = cur; } return 'torn'; })()"
  EvalJs $js
}
# Wait until the OLED shows the wanted page (or, for 'BANNER', a title that is none of the five pages), return its band signature.
function PageBand($want, $y0, $h, $capSimSec = 40) {
  $t0 = Sim-Now; $wall = (Get-Date).AddSeconds(240)
  while ((Get-Date) -lt $wall -and ((Sim-Now) - $t0) -lt $capSimSec) {
    $n = Retry { Sim-OledPageName }
    $isBanner = ($n -like 'OTHER(*')
    if (($want -eq 'BANNER' -and $isBanner) -or $n -eq $want) { $s = BandSig $y0 $h; if ($s -ne 'torn') { $n2 = Retry { Sim-OledPageName }; if ($n2 -eq $n) { return $s } } }
    Start-Sleep -Milliseconds 250
  }
  return $null
}
function Idle-Normal { foreach ($sw in 'swTemp','swHr','swSpo2','swMotion','swWifiCut','swBrokerCut') { $null = Set-Switch $sw 0 }; Sim-Pub "control.sim-hr" "0"; Sim-Pub "control.sim-spo2" "0"; Sim-Pub "control.dosage" "0"; Sim-Pub "control.bed-auto" "1"; Sim-Pub "control.sampling-mode" "1" }

L "================================================================================"
L ("SUPPLEMENTARY CHECKS  started " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
L "================================================================================"

# ---------------------------------------------------------------- S-14: dose footer on every page, banner included
L ""; L "---- S-14 OLED dose footer on every page and on the offline banner (Task 3, real-time read-out)"
Idle-Normal; $null = Sim-WaitSerial 'delivered 0\.0 -> target 0\.0|\[RATE\] mode: auto' 60 0; Sleep-Sim 6
$pages = 'PATIENT','EXTENDED','FACILITY','THERAPY','SYSTEM'
$f0 = [ordered]@{}; foreach ($p in $pages) { $f0[$p] = PageBand $p 52 12 }
$u0 = @($f0.Values | Where-Object { $_ } | Select-Object -Unique)
$lit0 = @($f0.Values | Where-Object { $_ } | ForEach-Object { [int]($_ -split ':')[1] })
Check 'S-14a' 'footer band identical on all five pages at dose 0 and not blank' ($f0.Values.Count -eq 5 -and @($f0.Values | Where-Object { -not $_ }).Count -eq 0 -and $u0.Count -eq 1 -and ($lit0 | Measure-Object -Minimum).Minimum -gt 40) ("distinct footer signatures: {0} over {1} pages; lit pixels per page: {2}" -f $u0.Count, @($f0.Values | Where-Object { $_ }).Count, ($lit0 -join ','))
Sim-Mark; Sim-Pub "control.dosage" "60"; $null = Sim-WaitSerial '\[DOSE\] t=\d+ ms delivered 60\.0 -> target 60\.0' 150; Sleep-Sim 3
$f60 = [ordered]@{}; foreach ($p in $pages) { $f60[$p] = PageBand $p 52 12 }
$u60 = @($f60.Values | Where-Object { $_ } | Select-Object -Unique)
Check 'S-14b' 'at dose 60 the footer is again identical on all five pages and differs from the dose-0 footer' ($f60.Values.Count -eq 5 -and @($f60.Values | Where-Object { -not $_ }).Count -eq 0 -and $u60.Count -eq 1 -and $u60[0] -ne $u0[0]) ("distinct footer signatures: {0}; equals the dose-0 footer: {1}" -f $u60.Count, ($u60[0] -eq $u0[0]))
# offline banner: cut Wi-Fi, wait for the banner, footer must still show the (unchanged) dose-60 footer
$c0 = Sim-Now; $null = Set-Switch swWifiCut 1; $null = Sim-WaitSerial '\[NET\] state ONLINE -> OFFLINE \(fault: WIFI\)' 40; Sleep-Sim 3
$fb = PageBand 'BANNER' 52 12 20
Check 'S-14c' 'the offline banner carries the same dose footer (dose 60)' ([bool]$fb -and $fb -eq $u60[0]) ("banner footer signature equals the dose-60 page footer: {0}" -f ($fb -eq $u60[0]))
$null = Set-Switch swWifiCut 0; Sim-Pub "control.dosage" "0"; $null = Sim-WaitSerial '\[NET\] state OFFLINE -> ONLINE' 150; $null = Sim-WaitSerial 'delivered 0\.0 -> target 0\.0' 150; Sleep-Sim 3

# ---------------------------------------------------------------- S-09b: OLED shows Auto bed ON / OFF
L ""; L "---- S-09b OLED THERAPY page reflects Auto bed ON/OFF (Task 4)"
Sim-Pub "control.bed-auto" "1"; Sleep-Sim 4
# the "Auto bed:" row is the 5th content row: y = 41..48
$rowOn1 = PageBand 'THERAPY' 41 9
Sim-Pub "control.bed-angle" "30"; Sleep-Sim 5
$rowOff = PageBand 'THERAPY' 41 9
Sim-Pub "control.bed-auto" "1"; Sleep-Sim 5
$rowOn2 = PageBand 'THERAPY' 41 9
Check 'S-09b' 'a manual bed command changes the Auto-bed row (ON -> OFF) and "auto" restores it (OFF -> ON)' ([bool]$rowOn1 -and [bool]$rowOff -and [bool]$rowOn2 -and $rowOn1 -ne $rowOff -and $rowOn1 -eq $rowOn2) ("ON {0} | after manual angle {1} | auto again {2}" -f $rowOn1, $rowOff, $rowOn2)
Sim-Pub "control.bed-angle" "10"; Sleep-Sim 3; Sim-Pub "control.bed-auto" "1"

L ""; L "================================================================================"
$pass = ($script:results | Where-Object { $_.pass }).Count; $fail = ($script:results | Where-Object { -not $_.pass }).Count
L ("SUMMARY: {0} PASS, {1} FAIL, {2} checks" -f $pass, $fail, $script:results.Count)
$script:results | ConvertTo-Json -Depth 4 | Out-File "$ev\supplement_pass_results.json" -Encoding utf8
L ("finished " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
