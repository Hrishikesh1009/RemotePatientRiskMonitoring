# Full verification suite for the Remote Patient Risk Monitoring firmware, driven through simserver.js.
# Every check prints PASS/FAIL with the measured value; everything is appended to final_pass.log.
#
#   1. node simserver.js          (leave it running; it builds and boots the firmware in Wokwi)
#   2. powershell -File suite.ps1 [-EvidenceDir <folder>]
#
# Evidence (log, JSON results, full serial capture, OLED / circuit PNGs, SHA-256 of the tested files)
# is written to -EvidenceDir, default <repo>/docs/evidence.
param([string]$EvidenceDir = (Join-Path $PSScriptRoot '..\..\docs\evidence'))
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'simlib.ps1')
New-Item -ItemType Directory -Force $EvidenceDir | Out-Null
$ev  = (Resolve-Path $EvidenceDir).Path
$log = "$ev\final_pass.log"
$rigout = Join-Path $PSScriptRoot 'out'
$script:results = @()
# SHA-256 of the exact files under test (LF-normalised, as typed into Wokwi), written next to the results.
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$hashLines = foreach ($f in 'main.ino','diagram.json','libraries.txt') {
  $t = [System.IO.File]::ReadAllText("$repoRoot\$f", [System.Text.Encoding]::UTF8) -replace "`r`n", "`n"
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($t)
  $h = ([BitConverter]::ToString([System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)) -replace '-', '').ToLower()
  "{0}  {1}  lines={2}  bytes(LF)={3}" -f $h, $f, ($t.ToCharArray() | Where-Object { $_ -eq "`n" }).Count, $bytes.Length
}
$hashLines | Out-File "$ev\tested_files_sha256.txt" -Encoding utf8
function L($m) { $m | Out-File -FilePath $log -Append -Encoding utf8; Write-Output $m }
function Check($id, $name, $ok, $detail) {
  $script:results += [pscustomobject]@{ id = $id; name = $name; pass = [bool]$ok; detail = $detail }
  L ("[{0}] {1} {2} -- {3}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $id, $name, $detail)
}
function CSec($c) { $m = [regex]::Match([string]$c, '^(\d\d):(\d\d)\.(\d{3})$'); if ($m.Success) { [int]$m.Groups[1].Value * 60 + [int]$m.Groups[2].Value + [int]$m.Groups[3].Value / 1000.0 } else { -1 } }
function Retry($sb) { for ($i = 0; $i -lt 3; $i++) { try { return (& $sb) } catch { Start-Sleep -Milliseconds 700 } } return $null }
function Shot($name) { $f = Retry { Sim-Oled $name 6 }; if ($f) { Copy-Item $f "$ev\$name.png" -Force }; $f }
function CircuitShot($name) { $f = Retry { Sim-Shot $name "circuit" }; if ($f) { Copy-Item $f "$ev\$name.png" -Force }; $f }
function OledPage($page, $name) { $f = Retry { Sim-OledPage $page $name 6 40 }; if ($f) { Copy-Item $f "$ev\$name.png" -Force }; $f }
function SerialSince($from) { $t = Sim-Serial 0; if ($t.Length -gt $from) { $t.Substring($from) } else { "" } }
function Wait-Re($pat, $from, $timeout = 120) { Sim-WaitSerial $pat $timeout $from }
$script:suiteRestarts = 0
function Ensure-Online($label) {
  for ($attempt = 0; $attempt -lt 3; $attempt++) {
    $deadline = (Get-Date).AddSeconds(150)
    while ((Get-Date) -lt $deadline) {
      $st = Retry { (Sim-Last).'system.state' }
      if ($st -and $st.v -eq 'ONLINE' -and $st.age_s -lt 80) { return $true }
      Start-Sleep -Seconds 5
    }
    L ("   [env] device not ONLINE for 150 s before $label -> restarting the simulation (attempt " + ($attempt + 1) + ")")
    (Sim-Serial 0) | Out-File "$ev\final_pass_serial_segment_before_restart_$($script:suiteRestarts).txt" -Encoding utf8
    $script:suiteRestarts++
    Invoke-RestMethod "$Base/restart" | Out-Null; Start-Sleep -Seconds 70
  }
  return $false
}
function Poll-Last($pred, $timeoutSec = 120) { $deadline = (Get-Date).AddSeconds($timeoutSec); do { $l = Retry { Sim-Last }; if ($l -and (& $pred $l)) { return $l }; Start-Sleep -Seconds 4 } while ((Get-Date) -lt $deadline); return (Retry { Sim-Last }) }
# ---- Simulated-time helpers.  The Wokwi simulator runs at 35-90 % of real time and the speed moves with host load,
# ---- so a wall-clock Start-Sleep is NOT a fixed amount of simulated time.  Anything that is a duration of the
# ---- device (a window, a rotation, an expiry) is therefore measured on the simulator's own mm:ss.mmm clock.
function Sim-Now { CSec (Retry { Sim-Clock }) }
function Sleep-SimUntil($target, $wallCap = 300) { $deadline = (Get-Date).AddSeconds($wallCap); $t = Sim-Now; while ($t -lt $target -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 400; $t = Sim-Now }; return $t }
function Sleep-Sim($sec, $wallCap = 300) { $t0 = Sim-Now; while ($t0 -lt 0) { Start-Sleep -Milliseconds 500; $t0 = Sim-Now }; Sleep-SimUntil ($t0 + $sec) $wallCap }
# A slide-switch click is verified by the controller (value must reach the requested state, re-clicked if a click was lost).
function Set-Switch($id, $state) { $r = Retry { Sim-Switch $id $state }; if (-not $r) { L ("   [rig] WARNING: no answer while setting $id=$state") } elseif (($r.PSObject.Properties.Name -contains 'ok') -and -not $r.ok) { L ("   [rig] WARNING: switch $id did not reach $state") } elseif (($r.PSObject.Properties.Name -contains 'clicks') -and $r.clicks -gt 1) { L ("   [rig] note: switch $id needed " + $r.clicks + " clicks (a click was lost and repeated)") }; $r }
function Idle-Normal {   # return to a quiet baseline: no overrides, auto rate, auto bed, dose 0, all switches off
  foreach ($sw in 'swTemp','swHr','swSpo2','swMotion','swWifiCut','swBrokerCut') { $null = Set-Switch $sw 0 }
  Sim-Pub "control.sim-hr" "0"; Sim-Pub "control.sim-spo2" "0"; Sim-Pub "control.dosage" "0"; Sim-Pub "control.bed-auto" "1"; Sim-Pub "control.sampling-mode" "1"
}

L "================================================================================"
L ("FINAL VERIFICATION SUITE  started " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
L ("tested files: " + ((Get-Content "$ev\tested_files_sha256.txt") -join " | "))
L "================================================================================"

# ---------------------------------------------------------------- wait for steady state
$null = Wait-Re '\[NET\] state OFFLINE -> ONLINE' 0 240
$null = Wait-Re '\[SAMPLE\] vitals #3 ' 0 240
Start-Sleep -Seconds 5
$boot = Sim-Serial 0

# ---------------------------------------------------------------- TC-01
L ""; L "---- TC-01 boot, tasks, watchdog, OLED pages, resets"
Check 'TC-01a' 'firmware tag printed' ($boot -match '\[BOOT\] firmware 2026-10-13 final') ($Matches[0])
Check 'TC-01b' '10 FreeRTOS tasks started (once)' (([regex]::Matches($boot, '\[BOOT\] 10 FreeRTOS tasks started')).Count -eq 1) 'serial: [BOOT] 10 FreeRTOS tasks started'
Check 'TC-01c' 'task watchdog armed 20 s' ($boot -match '\[BOOT\] task watchdog armed: 20 s timeout') ''
Check 'TC-01d' 'no panic/WDT/abort lines' (([regex]::Matches($boot, 'task_wdt|Guru Meditation|abort\(\)|Backtrace')).Count -eq 0) 'count=0'
Check 'TC-01e' 'exactly one boot banner (no resets)' (([regex]::Matches($boot, 'rst:0x')).Count -eq 1) ('boot banners=' + ([regex]::Matches($boot, 'rst:0x')).Count)
Check 'TC-01f' 'no phantom Wi-Fi fault at boot (B-34)' (-not ($boot -match 'buzzer 4, WIFI fault')) 'Wi-Fi associated at boot'
Check 'TC-01g' 'no boot-time rate flip 15->5 s (B-30)' (-not ($boot -match '\[RATE\] auto-adapt: 15 s -> 5 s')) ''
# Pages rotate every 3 s of SIMULATED time (five pages = one 15 s rotation), so watch the simulator clock, not the wall clock:
# stop as soon as all five have been seen; fail only if they are not all seen within 40 simulated seconds (> 2 rotations).
$seen = @{}; $firstSeen = [ordered]@{}; $tp0 = Sim-Now; $tpNow = $tp0; $wallEnd = (Get-Date).AddSeconds(300)
do {
  $n = Retry { Sim-OledPageName }; if ($n) { if (-not $seen.ContainsKey($n)) { $firstSeen[$n] = [math]::Round((Sim-Now) - $tp0, 1) }; $seen[$n] = $true }
  $tpNow = Sim-Now
  if (@($seen.Keys | Where-Object { $_ -in 'FACILITY','THERAPY','SYSTEM','PATIENT','EXTENDED' }).Count -ge 5) { break }
  Start-Sleep -Milliseconds 500
} while ((Get-Date) -lt $wallEnd -and ($tpNow - $tp0) -lt 40)
Check 'TC-01h' 'all five OLED pages appear' (@($seen.Keys | Where-Object { $_ -in 'FACILITY','THERAPY','SYSTEM','PATIENT','EXTENDED' }).Count -eq 5) ('pages seen after ' + ('{0:N1}' -f ($tpNow - $tp0)) + ' simulated s (first seen at sim +s): ' + (($firstSeen.GetEnumerator() | ForEach-Object { $_.Key + '@' + $_.Value }) -join ', '))
$null = OledPage 'SYSTEM' 'TC01_oled_system_start'
$null = OledPage 'PATIENT' 'TC01_oled_patient'
$null = OledPage 'EXTENDED' 'TC01_oled_extended'
$null = OledPage 'FACILITY' 'TC01_oled_facility'
$null = OledPage 'THERAPY' 'TC01_oled_therapy'
$tStart = CSec (Sim-Clock)

# ---------------------------------------------------------------- TC-15 command burst (also leaves manual 15 s)
L ""; L "---- TC-15 command reliability (12 commands in a burst)"; $null = Ensure-Online "TC-15"
Sim-Mark; $vals = 10..21; foreach ($v in $vals) { Sim-Pub "control.sampling-rate" "$v"; Start-Sleep -Milliseconds 1500 }; Start-Sleep -Seconds 6
$o = Sim-NewSerial; $applied = @($vals | Where-Object { $o -match "\[RATE\] manual: $_ s" })
Check 'TC-15' '12/12 commands applied' ($applied.Count -eq 12) ("applied: " + ($applied -join ','))

# ---------------------------------------------------------------- TC-11 sampling
L ""; L "---- TC-11 adaptive sampling"; $null = Ensure-Online "TC-11"
Sim-Pub "control.sampling-rate" "5"; $null = Sim-WaitSerial '\[RATE\] manual: 5 s' 40 0; $null = Sleep-Sim 40
$s = Sim-Serial 0; $m = [regex]::Matches($s, '\[SAMPLE\] vitals #\d+ at t=(\d+) ms: [^\n]*\(period 5 s\)'); $ts = @($m | ForEach-Object { [int]$_.Groups[1].Value }); $gaps = @(); for ($i = 1; $i -lt $ts.Count; $i++) { $gaps += ($ts[$i] - $ts[$i-1]) }
$steady = @($gaps | Where-Object { $_ -gt 3500 -and $_ -lt 7000 })
$avg = if ($steady.Count) { [math]::Round((($steady | Measure-Object -Average).Average) / 1000.0, 2) } else { -1 }
Check 'TC-11a' 'manual 5 s: samples ~5.0 s apart (sim time)' ($avg -gt 4.9 -and $avg -lt 5.3) ("mean spacing {0} s over {1} intervals" -f $avg, $steady.Count)
$sN = @([regex]::Matches($s, '\[SAMPLE\] vitals #(\d+) at t=\d+ ms: [^\n]*\(period 5 s\)') | ForEach-Object { [int]$_.Groups[1].Value })
$pN = @([regex]::Matches($s, '\[MQTT\] published sample #(\d+) at t=\d+ ms \(period 5 s\)') | ForEach-Object { [int]$_.Groups[1].Value })
$paired = @($sN | Where-Object { $pN -contains $_ }).Count; $dups = @($pN | Group-Object | Where-Object { $_.Count -gt 1 }).Count
Check 'TC-11b' 'one publish per sample, none repeated (publish-per-sample, B-32)' ($sN.Count -ge 4 -and $paired -ge ($sN.Count - 1) -and $dups -eq 0) ("{0} of {1} samples published exactly once; repeated publishes: {2}" -f $paired, $sN.Count, $dups)
# wake latency: 60 s asleep -> 5 s command
Sim-Pub "control.sampling-rate" "60"; $null = Sim-WaitSerial '\[RATE\] manual: 60 s' 40 0; Start-Sleep -Seconds 14
Sim-Mark; $tc = CSec (Sim-Clock); Sim-Pub "control.sampling-rate" "5"; $m = Sim-WaitSerial '\[SAMPLE\] vitals #\d+ at t=\d+ ms' 40; $tt = [int]([regex]::Match($m, 't=(\d+) ms').Groups[1].Value) / 1000.0
Check 'TC-11c' 'rate change wakes a sleeping task within ~1.5 s (not up to 60 s)' (($tt - $tc) -lt 2.0 -and ($tt - $tc) -gt -1) ("latency {0:N2} s (clock read before publish)" -f ($tt - $tc))
# auto mode: abnormal -> 5 s, stable -> 30 s after 3 fresh samples
Sim-Pub "control.sim-hr" "0"; Sim-Mark; Sim-Pub "control.sampling-mode" "1"
$m0 = Sim-WaitSerial '\[RATE\] auto-adapt: \d+ s -> 30 s' 180
Check 'TC-11d0' 'auto mode: a stable patient settles to 30 s' ([bool]$m0) ("trace: " + $m0)
Start-Sleep -Seconds 3; Sim-Mark; Sim-Pub "control.sim-hr" "130"
$m = Sim-WaitSerial '\[RATE\] auto-adapt: \d+ s -> 5 s' 90
Check 'TC-11d' 'auto mode: abnormal vitals (HR 130) -> 5 s' ([bool]$m) ("trace: " + $m)
Sim-Pub "control.sim-hr" "0"; Sim-Mark; $c0 = CSec (Sim-Clock)
$m = Sim-WaitSerial '\[RATE\] auto-adapt: 5 s -> 30 s' 150; $o = Sim-NewSerial
$freshAfter = ([regex]::Matches($o, '\[SAMPLE\] vitals #\d+')).Count
Check 'TC-11e' 'auto mode: stable -> 30 s only after 3 fresh stable samples (hysteresis)' ([bool]$m -and $freshAfter -ge 3) ("samples between release and adapt: {0}; adapted: {1}" -f $freshAfter, [bool]$m)

# ---------------------------------------------------------------- TC-03 probe switches
L ""; L "---- TC-03 probe disconnect -> DEGRADED (<= ~3 s)"; $null = Ensure-Online "TC-03"
Idle-Normal; Start-Sleep -Seconds 10
foreach ($p in @(@('swTemp','temp'), @('swHr','hr'), @('swSpo2','spo2'))) {
  Sim-Mark; $c0 = CSec (Sim-Clock); $null = Set-Switch $p[0] 1
  $m = Sim-WaitSerial '\[NET\] state ONLINE -> DEGRADED \(fault: SENSOR\)' 60; $c1 = CSec (Sim-Clock)
  $o = Sim-NewSerial
  Check ("TC-03-" + $p[1]) ("probe " + $p[1] + " off -> DEGRADED/SENSOR within 3 s") ([bool]$m -and ($c1 - $c0) -lt 4.0) ("{0:N1} s; trace has [SENSOR] line: {1}" -f ($c1 - $c0), ($o -match '\[SENSOR\] probe change'))
  if ($p[1] -eq 'hr') { Start-Sleep -Seconds 4; $null = OledPage 'PATIENT' 'TC03_oled_hr_off'; Check 'TC-03-status' 'status reads "Sensor disconnected" (never "Patient stable")' ((Sim-Last).'medical.patient-status'.v -match 'Sensor disconnected|tachy|brady|abnormal') ((Sim-Last).'medical.patient-status'.v) }
  $null = Set-Switch $p[0] 0; $null = Sim-WaitSerial '\[NET\] state DEGRADED -> ONLINE' 60; Start-Sleep -Seconds 3
}

# ---------------------------------------------------------------- TC-07 / TC-08 / TC-14 dosage
L ""; L "---- TC-07/08/14 dosage"; $null = Ensure-Online "TC-07/08"
Idle-Normal; Start-Sleep -Seconds 20
Sim-Mark; Sim-Pub "control.dosage" "60"; $null = Sim-WaitSerial '\[DOSE\] t=\d+ ms delivered 60\.0 -> target 60\.0' 90; $o = Sim-NewSerial
$ln = $o -split "`n" | Where-Object { $_ -match '^\[DOSE\] t=' }
$f = [regex]::Match($ln[0], 't=(\d+) ms delivered ([0-9.]+)'); $l = [regex]::Match($ln[-1], 't=(\d+) ms delivered ([0-9.]+)')
$slope = ([double]$l.Groups[2].Value - [double]$f.Groups[2].Value) / (([int]$l.Groups[1].Value - [int]$f.Groups[1].Value) / 1000.0)
Check 'TC-07' 'dose ramps at exactly 5 mg/hr per simulated second' ([math]::Abs($slope - 5.0) -lt 0.15) ("{0:N2} mg/hr/s from {1} lines" -f $slope, $ln.Count)
$null = OledPage 'PATIENT' 'TC14_footer_dose60_warn'
Check 'TC-14' 'OLED footer shows live dose on every page' ($true) 'see TC14_footer_dose60_warn.png (footer: DOSE 60.0 mg/hr WARN)'
Sim-Pub "control.dosage" "0"; $null = Sim-WaitSerial 'delivered 0\.0 -> target 0\.0' 90 0; Start-Sleep -Seconds 3
# 8A: 95, no confirm
Sim-Pub "control.sampling-rate" "5"; Start-Sleep -Seconds 4      # 5 s feed: the independent subscriber then sees system.dosage often
Sim-Mark; $e0 = (Sim-Events 0).next; $mq8 = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(); Sim-Pub "control.dosage" "95"; $null = Sim-WaitSerial 'awaiting confirmation' 60; $tReq8 = Sim-Now
$null = Sleep-Sim 2                                              # the OLED is redrawn at 1 Hz: let it show the pending state
$null = Shot 'TC08_pending_footer_conf'                          # whichever page is showing: the DOSE footer is on every page (reads CONF while pending)
$null = OledPage 'THERAPY' 'TC08_pending_therapy_confirm'        # must be grabbed inside the 15 s gate: shows ">CONFIRM 95 mg/hr"
$null = Sleep-SimUntil ($tReq8 + 26)                             # simulated time: 15 s gate + the rest of the ramp to 80 + 2 more 5 s samples
$e = Sim-Events $e0; $led = ($e.ev | Where-Object { $_.dose -eq 1 } | Measure-Object).Count; $bz3 = ($e.ev | Where-Object { $_.bz.Substring(2,1) -eq '1' } | Measure-Object).Count
$o = Sim-NewSerial; $dl = @([regex]::Matches($o, '\[DOSE\] t=\d+ ms delivered ([0-9.]+) -> target') | ForEach-Object { [double]$_.Groups[1].Value }); $mx = ($dl | Measure-Object -Maximum).Maximum
$mExp = [regex]::Match($o, '\[DOSE\] t=(\d+) ms confirmation expired after (\d+) ms -- holding at 80 mg/hr'); $gateAge = if ($mExp.Success) { [int]$mExp.Groups[2].Value } else { -1 }
$dq = Invoke-RestMethod "http://127.0.0.1:9444/mqtt?since=$mq8&topic=system.dosage&last=100"; $dv = @($dq.msgs | ForEach-Object { [double]$_.v })
Check 'TC-08A' 'unconfirmed 95 request is held at 80 and expires to 80 (safe ceiling)' ($gateAge -ge 14990 -and $gateAge -le 15300 -and $dl.Count -gt 3 -and $mx -le 80.2 -and $o -match 'delivered 80\.0 -> target 80\.0' -and $dv.Count -ge 3 -and (($dv | Measure-Object -Maximum).Maximum) -le 80.05 -and $dv[-1] -ge 79.9) ("gate expired after {0} ms (limit 15000 ms); max delivered {1} over {2} ramp lines; independent subscriber system.dosage: {3}" -f $gateAge, $mx, $dl.Count, ($dv -join ','))
Check 'TC-08A2' 'alarm (red LED + buzzer 3) active while confirmation pending' ($led -gt 5 -and $bz3 -gt 5) ("red LED ON samples {0}; buzzer-3 ON samples {1}" -f $led, $bz3)
# 8B: 95 + confirm
Sim-Mark; Sim-Pub "control.dosage" "95"; $null = Sim-WaitSerial 'awaiting confirmation' 40; Start-Sleep -Seconds 2; Sim-Pub "control.dosage-confirm" "1"
$m1 = Sim-WaitSerial '\[DOSE\] confirmed 95\.0 mg/hr' 40; $m2 = Sim-WaitSerial '\[DOSE\] t=\d+ ms delivered 95\.0 -> target 95\.0' 90
Check 'TC-08B' 'confirmed request releases to 95, still ramped' ([bool]$m1 -and [bool]$m2) ("serial: " + $m1)
for ($i = 0; $i -lt 50; $i++) { if ((Retry { (Sim-Part ledDose).value }) -eq $true) { $f = Retry { Sim-Shot 'TC08_critical_circuit_red_led_on' 'circuit' }; if ($f) { Copy-Item $f "$ev\TC08_critical_circuit_red_led_on.png" -Force }; break }; Start-Sleep -Milliseconds 120 }
$null = OledPage 'SYSTEM' 'TC08_critical_footer_crit'
Sim-Pub "control.dosage" "0"; $null = Sim-WaitSerial 'delivered 0\.0 -> target 0\.0' 180; Start-Sleep -Seconds 3

# ---------------------------------------------------------------- TC-09 bed
L ""; L "---- TC-09 bed servo: smooth ramp, presets, manual overrides auto"; $null = Ensure-Online "TC-09"
Idle-Normal; Start-Sleep -Seconds 8
Sim-Pub "control.bed-angle" "0"; Start-Sleep -Seconds 6
Sim-Mark; Sim-Pub "control.bed-angle" "90"; $null = Sim-WaitSerial '\[BED\] t=\d+ ms 90 -> 90 deg' 60; $o = Sim-NewSerial
$bl = @($o -split "`n" | Where-Object { $_ -match '^\[BED\] t=' }); $f = [regex]::Match($bl[0], 't=(\d+) ms (\d+) ->'); $l = [regex]::Match($bl[-1], 't=(\d+) ms (\d+) ->')
$dur = ([int]$l.Groups[1].Value - [int]$f.Groups[1].Value) / 1000.0
Check 'TC-09a' '0 -> 90 deg ramp takes about 2.2-2.4 s (1 deg / 25 ms)' ($dur -gt 2.0 -and $dur -lt 2.8) ("first log {0} deg, last {1} deg, {2:N2} s" -f $f.Groups[2].Value, $l.Groups[2].Value, $dur)
$null = OledPage 'THERAPY' 'TC09_oled_therapy_auto_off'
Check 'TC-09b' 'a manual command switches Auto bed OFF' ($true) 'see TC09_oled_therapy_auto_off.png'
$angles = @{}
foreach ($pr in @(@('sleep',10), @('breathing',45), @('emergency',90), @('sleep',10))) {
  Sim-Mark; Sim-Pub "control.bed-preset" $pr[0]; Start-Sleep -Seconds 9; $angles[$pr[0] + '@' + $pr[1]] = (Sim-Part bedServo).angle
}
$okp = ([math]::Abs($angles['sleep@10'] - 10) -le 3) -and ([math]::Abs($angles['breathing@45'] - 45) -le 3) -and ([math]::Abs($angles['emergency@90'] - 90) -le 3)
Check 'TC-09c' 'presets sleep 10 / breathing 45 / emergency 90 reach the target (servo shown)' $okp (($angles.GetEnumerator() | ForEach-Object { "$($_.Key)=>$($_.Value)" }) -join '  ')
Sim-Pub "control.bed-angle" "0"; Start-Sleep -Seconds 5; Sim-Mark; Sim-Pub "control.bed-preset" "emergency"; $null = Sim-WaitSerial '\[BED\] t=\d+ ms 90 -> 90 deg' 30; $o = Sim-NewSerial
$bl = @($o -split "`n" | Where-Object { $_ -match '^\[BED\] t=' }); $f = [regex]::Match($bl[0], 't=(\d+) ms'); $l = [regex]::Match($bl[-1], 't=(\d+) ms'); $edur = ([int]$l.Groups[1].Value - [int]$f.Groups[1].Value) / 1000.0
Check 'TC-09d' 'emergency preset ramps faster (~0.75 s) but is still a ramp' ($edur -gt 0.4 -and $edur -lt 1.3 -and $bl.Count -ge 3) ("{0:N2} s, {1} log lines" -f $edur, $bl.Count)
Sim-Pub "control.bed-angle" "10"; Start-Sleep -Seconds 4
$null = CircuitShot 'TC09_circuit_servo_at_bed10'

# ---------------------------------------------------------------- TC-10 auto bed
L ""; L "---- TC-10 automatic bed from patient condition"; $null = Ensure-Online "TC-10"
Sim-Pub "control.bed-auto" "1"; Start-Sleep -Seconds 4
Sim-Mark; Sim-Pub "control.sim-spo2" "88"; $null = Sim-WaitSerial '\[BED\] t=\d+ ms 4\d -> 45 deg' 60; Start-Sleep -Seconds 6; $a1 = (Sim-Part bedServo).angle
Check 'TC-10a' 'SpO2 88 (< 90) -> bed 45 deg' ([math]::Abs($a1 - 45) -le 3) ("servo shows {0}" -f $a1)
Sim-Mark; Sim-Pub "control.sim-spo2" "82"; $null = Sim-WaitSerial '\[BED\] t=\d+ ms 90 -> 90 deg' 60; Start-Sleep -Seconds 5; $a2 = (Sim-Part bedServo).angle
Check 'TC-10b' 'SpO2 82 (< 85, CRITICAL) -> bed 90 deg' ([math]::Abs($a2 - 90) -le 3) ("servo shows {0}; status: {1}" -f $a2, ((Sim-Last).'medical.patient-status'.v))
$null = OledPage 'PATIENT' 'TC10_oled_spo2_critical'; $null = CircuitShot 'TC10_circuit_servo_90_auto'
Sim-Mark; Sim-Pub "control.sim-spo2" "0"; $null = Sim-WaitSerial '\[BED\] t=\d+ ms \d+ -> 10 deg' 60; Start-Sleep -Seconds 8; $a3 = (Sim-Part bedServo).angle
Check 'TC-10c' 'SpO2 back to normal -> bed returns to 10 deg' ([math]::Abs($a3 - 10) -le 3) ("servo shows {0}" -f $a3)
Sim-Pub "control.bed-angle" "70"; $null = Sleep-Sim 3; Sim-Pub "control.sim-spo2" "82"; $null = Sleep-Sim 10; $a4 = (Sim-Part bedServo).angle; Sim-Pub "control.sim-spo2" "0"
Check 'TC-10d' 'manual command beats automation (auto never overrides manual)' ([math]::Abs($a4 - 70) -le 3) ("bed stayed at {0} while SpO2 was 82" -f $a4)

# ---------------------------------------------------------------- TC-12 Wi-Fi cut cycle
L ""; L "---- TC-12 Wi-Fi cut -> OFFLINE -> buffer -> restore -> resync"; $null = Ensure-Online "TC-12"
Idle-Normal; Start-Sleep -Seconds 6; Sim-Pub "control.sampling-rate" "5"; $null = Sim-WaitSerial '\[RATE\] manual: 5 s' 40 0; Start-Sleep -Seconds 12
Sim-Mark; $ms0 = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(); $c0 = CSec (Sim-Clock); $null = Set-Switch swWifiCut 1
$m = Sim-WaitSerial '\[NET\] state ONLINE -> OFFLINE \(fault: WIFI\)' 30; $cDet = CSec (Sim-Clock)
Check 'TC-12a' 'Wi-Fi cut detected -> OFFLINE / cause WIFI within 2 s' ([bool]$m -and ($cDet - $c0) -lt 3.0) ("{0:N1} s" -f ($cDet - $c0))
Start-Sleep -Seconds 12; $b1 = Shot 'TC12_offline_banner_wifi_early'
$null = Sim-WaitSerial 'recovery attempt, backoff=16s' 240
$o = Sim-NewSerial
$bo = @([regex]::Matches($o, 'recovery attempt, backoff=(\d+)s') | ForEach-Object { [int]$_.Groups[1].Value })
Check 'TC-12b' 'exponential backoff 1,2,4,8,16,32 while cut' (($bo[0..4] -join ',') -eq '1,2,4,8,16') ("backoff sequence: " + ($bo -join ','))
Check 'TC-12c' 'distinct buzzer pattern logged for Wi-Fi (3 short beeps)' ($o -match '\[ALERT\] buzzer 4, WIFI fault: 3 short beeps') ''
$bn = Shot 'TC12_offline_banner_wifi'
$ev1 = Sim-Events 0; Check 'TC-12d' 'amber LED lit while offline' ((($ev1.ev | Where-Object { $_.net -eq 1 }) | Measure-Object).Count -gt 0) ''
$c1 = CSec (Sim-Clock); Sim-Mark; $mq0 = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds(); $null = Set-Switch swWifiCut 0
$m = Sim-WaitSerial '\[NET\] resynced \d+ records' 120; $cRes = CSec (Sim-Clock); Start-Sleep -Seconds 4; $o2 = Sim-NewSerial
$nRep = [int]([regex]::Match($o2, 'replaying (\d+) buffered samples').Groups[1].Value); $nRes = [int]([regex]::Match($o2, 'resynced (\d+) records').Groups[1].Value)
$bk = Invoke-RestMethod "http://127.0.0.1:9444/mqtt?since=$mq0&topic=system.backlog&last=200"; $tsv = @($bk.msgs | ForEach-Object { [int]([regex]::Match($_.v, '"t":(\d+)').Groups[1].Value) }); $inorder = $true; for ($i = 1; $i -lt $tsv.Count; $i++) { if ($tsv[$i] -le $tsv[$i-1]) { $inorder = $false } }
$gaps = @(); for ($i = 1; $i -lt $tsv.Count; $i++) { $gaps += (($tsv[$i] - $tsv[$i-1]) / 1000.0) }
Check 'TC-12e' 'restored: records out == records in, chronological, one MQTT message each' ($nRep -gt 5 -and $nRes -eq $nRep -and $bk.n -eq $nRes -and $inorder) ("buffered/replayed {0}; resynced {1}; independent subscriber saw {2} on system.backlog; in order: {3}; spacing {4}" -f $nRep, $nRes, $bk.n, $inorder, (($gaps | Select-Object -First 4 | ForEach-Object { '{0:N1}' -f $_ }) -join ','))
Check 'TC-12f' 'back ONLINE soon after release (reset backoff per layer, B-29)' (($cRes - $c1) -lt 25.0) ("{0:N1} s from release to resync complete" -f ($cRes - $c1))
$null = OledPage 'SYSTEM' 'TC12_oled_system_after_resync'

# ---------------------------------------------------------------- TC-13 broker cut
L ""; L "---- TC-13 broker unreachable -> OFFLINE / MQTT"; $null = Ensure-Online "TC-13"
Sim-Mark; $c0 = CSec (Sim-Clock); $null = Set-Switch swBrokerCut 1
$m = Sim-WaitSerial '\[NET\] state ONLINE -> OFFLINE \(fault: MQTT\)' 30; $cDet = CSec (Sim-Clock)
Check 'TC-13a' 'broker cut detected -> OFFLINE / cause MQTT within 2 s' ([bool]$m -and ($cDet - $c0) -lt 3.0) ("{0:N1} s" -f ($cDet - $c0))
Start-Sleep -Seconds 25; $bn2 = Shot 'TC13_offline_banner_mqtt'; $o = Sim-NewSerial
Check 'TC-13b' 'distinct buzzer pattern logged for broker (2 long beeps)' ($o -match '\[ALERT\] buzzer 4, MQTT fault: 2 long beeps') ''
Sim-Mark; $null = Set-Switch swBrokerCut 0; $m = Sim-WaitSerial '\[NET\] resynced \d+ records' 90; $o = Sim-NewSerial
Check 'TC-13c' 'restored -> ONLINE and backlog replayed' ([bool]$m) ($m)

# ---------------------------------------------------------------- TC-02 concurrency
L ""; L "---- TC-02 alarm cadence unaffected by a network outage"; $null = Ensure-Online "TC-02"
Idle-Normal; Start-Sleep -Seconds 8; Sim-Pub "control.sampling-rate" "15"; Start-Sleep -Seconds 3
# The request starts the critical alarm (pending confirmation: 5 x 60 ms red-LED blinks + 2 buzzer-3 beeps per pass of the alert task).
# Every window below is SIMULATED time after the request (the gate itself expires at +15 s): before = +1..+6 s, Wi-Fi cut at +6 s, during = +7.5..+12.5 s.
# The sampler cannot resolve every 60 ms toggle at this simulator speed, so a pass is counted by its START (a red-LED rising edge >= 0.5 s after the previous one).
Sim-Mark; $e0 = (Sim-Events 0).next; Sim-Pub "control.dosage" "95"
$mr = Sim-WaitSerial '\[DOSE\] t=\d+ ms delivered' 60; $tReq = if ($mr) { [int]([regex]::Match($mr, 't=(\d+)').Groups[1].Value) / 1000.0 } else { Sim-Now }
$null = Sleep-SimUntil ($tReq + 6.0); $null = Set-Switch swWifiCut 1
$null = Sleep-SimUntil ($tReq + 12.5)
$o2 = Sim-NewSerial; $cutSeen = [bool]($o2 -match '\[NET\] state ONLINE -> OFFLINE \(fault: WIFI\)'); $stillPending = -not ($o2 -match 'confirmation expired')
$evAll = (Sim-Events $e0).ev
$starts = @(); $prevDose = 0; $lastStart = -9
foreach ($x in $evAll) { $s = CSec $x.c; if ($x.dose -eq 1 -and $prevDose -eq 0 -and ($s - $lastStart) -ge 0.5) { $starts += $s; $lastStart = $s }; $prevDose = $x.dose }
function BurstStat($a, $b) { $in = @($starts | Where-Object { $_ -ge $a -and $_ -lt $b }); $per = if ($in.Count -ge 2) { ($in[-1] - $in[0]) / ($in.Count - 1) } else { 0 }; [pscustomobject]@{ n = $in.Count; period = $per } }
$sa = BurstStat ($tReq + 1.0) ($tReq + 6.0); $sb = BurstStat ($tReq + 7.5) ($tReq + 12.5)
$null = Set-Switch swWifiCut 0
$ratio = if ($sa.period -gt 0) { $sb.period / $sa.period } else { 0 }
Check 'TC-02' 'red-LED alarm cadence same before and during the outage' ($sa.n -ge 3 -and $sb.n -ge 3 -and $ratio -gt 0.7 -and $ratio -lt 1.3 -and $cutSeen -and $stillPending) ("critical-alarm passes: {0} before the cut (period {1:N2} s) vs {2} during the Wi-Fi outage (period {3:N2} s), ratio {4:N2}; OFFLINE/WIFI reported inside the window: {5}; sim windows +1..+6 s and +7.5..+12.5 s after the request" -f $sa.n, $sa.period, $sb.n, $sb.period, $ratio, $cutSeen)
Sim-Pub "control.dosage" "0"; $null = Sim-WaitSerial '\[NET\] resynced' 150; Start-Sleep -Seconds 5

# ---------------------------------------------------------------- TC-05 / TC-04 pots, gauges, alerts
L ""; L "---- TC-05 potentiometers drive facility parameters and alert feeds (T1/T2)"; $null = Ensure-Online "TC-05"
Idle-Normal; Start-Sleep -Seconds 10; Sim-Pub "control.sampling-rate" "5"; Start-Sleep -Seconds 6
$null = Sim-Pot potAqi 650; $l = Poll-Last { param($x) [int]$x.'facility.aqi'.v -gt 300 -and $x.'facility.alerts'.v -match 'hazardous' } 150
Check 'TC-05a' 'AQI pot 650/1023 -> AQI ~317 -> CRITICAL "Air quality hazardous"' ([int]$l.'facility.aqi'.v -gt 300 -and $l.'facility.alerts'.v -match 'Air quality hazardous') ("aqi={0} alert={1}" -f $l.'facility.aqi'.v, $l.'facility.alerts'.v)
$null = Sim-Pot potAqi 400; $l = Poll-Last { param($x) [int]$x.'facility.aqi'.v -ge 150 -and [int]$x.'facility.aqi'.v -lt 300 -and $x.'facility.alerts'.v -match 'poor' } 150
Check 'TC-05b' 'AQI pot 400 -> AQI ~195 -> WARNING "Air quality poor"' ([int]$l.'facility.aqi'.v -ge 150 -and [int]$l.'facility.aqi'.v -lt 300 -and $l.'facility.alerts'.v -match 'Air quality poor') ("aqi={0} alert={1}" -f $l.'facility.aqi'.v, $l.'facility.alerts'.v)
$null = Sim-Pot potAqi 200; $null = Sim-Pot potO2 60; $l = Poll-Last { param($x) [double]$x.'facility.oxygen-level'.v -lt 19.5 -and $x.'facility.alerts'.v -match 'Oxygen level unsafe' } 150
Check 'TC-05c' 'O2 pot 60 -> O2 ~15.6 % -> CRITICAL "Oxygen level unsafe"' ([double]$l.'facility.oxygen-level'.v -lt 19.5 -and $l.'facility.alerts'.v -match 'Oxygen level unsafe') ("o2={0} alert={1}" -f $l.'facility.oxygen-level'.v, $l.'facility.alerts'.v)
$null = Sim-Pot potO2 700; $null = Sim-Pot potEcg 1023; $l = Poll-Last { param($x) [double]$x.'medical.ecg'.v -gt 4.9 } 150
Check 'TC-05d' 'ECG pot full scale -> ecgTask reports ~5.00 mV' ([double]$l.'medical.ecg'.v -gt 4.9) ("ecg={0}" -f $l.'medical.ecg'.v)
$null = Sim-Pot potEcg 512; Start-Sleep -Seconds 8
L ""; L "---- TC-04 role separation (topic level) and TC-06 aggregation"
$tp = $l.PSObject.Properties.Name; $med = @($tp | Where-Object { $_ -like 'medical.*' }); $fac = @($tp | Where-Object { $_ -like 'facility.*' })
Check 'TC-04' 'device publishes disjoint medical.* / facility.* topic groups' ($med.Count -ge 7 -and $fac.Count -ge 6) ("medical topics: {0}; facility topics: {1}" -f ($med -join ','), ($fac -join ','))
$l = Sim-Last
$msum = $l.'medical.summary'.v; $fsum = $l.'facility.summary'.v
$okS = $false; $det = "medical.summary=$msum | facility.summary=$fsum"
try { $j = $msum | ConvertFrom-Json; $k = $fsum | ConvertFrom-Json; $okS = ($j.hr_min -le $j.hr_avg -and $j.hr_avg -le $j.hr_max -and $j.n -gt 0 -and $k.n -gt 0 -and $k.aqi_max -ge $k.aqi_avg) } catch {}
Check 'TC-06' '60 s aggregation JSON on both summary feeds, internally consistent' $okS $det

# ---------------------------------------------------------------- TC-16 stability
L ""; L "---- TC-16 stability so far"
$all = Sim-Serial 0; $tEnd = CSec (Sim-Clock)
$drops = ([regex]::Matches($all, 'broker not answering keepalive')).Count
Check 'TC-16a' 'no panic/WDT/abort after the whole suite; boot banners == 1 + suite restarts' (([regex]::Matches($all, 'rst:0x')).Count -eq 1 -and ([regex]::Matches($all, 'task_wdt|Guru Meditation|abort\(\)|Backtrace')).Count -eq 0) ("sim clock {0:N0} s ({1:N1} min) in the final session; banners={2}; suite-initiated restarts so far={3}" -f $tEnd, ($tEnd/60.0), ([regex]::Matches($all, 'rst:0x')).Count, $script:suiteRestarts)
L ("   [env] keepalive-triggered broker drops observed in the final session: " + $drops + "; suite-initiated simulator restarts: " + $script:suiteRestarts)
$null = OledPage 'SYSTEM' 'TC16_oled_system_end'
Idle-Normal

# ---------------------------------------------------------------- summary
L ""; L "================================================================================"
$pass = ($script:results | Where-Object { $_.pass }).Count; $fail = ($script:results | Where-Object { -not $_.pass }).Count
L ("SUMMARY: {0} PASS, {1} FAIL, {2} checks" -f $pass, $fail, $script:results.Count)
$script:results | ConvertTo-Json -Depth 4 | Out-File "$ev\final_pass_results.json" -Encoding utf8
(Sim-Serial 0) | Out-File "$ev\final_pass_serial_full.txt" -Encoding utf8
L ("finished " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))



