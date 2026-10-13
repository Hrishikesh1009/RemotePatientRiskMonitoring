# Helpers for driving the simulation controller (simserver.js) from PowerShell.
$script:Base = "http://127.0.0.1:9444"
$script:SerialPos = 0

function Sim-Status   { Invoke-RestMethod "$Base/status" -TimeoutSec 30 }
function Sim-Clock    { (Sim-Status).clock }
function Sim-Serial   { param([int]$From = 0) ((Invoke-RestMethod "$Base/serial?from=$From" -TimeoutSec 30) -replace "`r", "") }
function Sim-SerialLen { (Sim-Status).serialLen }

# Return only the serial text produced since the last call (tracks position).
function Sim-NewSerial {
  $t = Sim-Serial 0
  $new = if ($t.Length -gt $script:SerialPos) { $t.Substring($script:SerialPos) } else { "" }
  $script:SerialPos = $t.Length
  return $new
}

# Mark "now" as the baseline for Sim-NewSerial.
function Sim-Mark { $script:SerialPos = (Sim-Serial 0).Length }

function Sim-Pub  { param([string]$Feed, [string]$Value) Invoke-RestMethod "$Base/pub?feed=$Feed&value=$([uri]::EscapeDataString($Value))" -TimeoutSec 30 | Out-Null }
function Sim-Switch { param([string]$Id, [string]$State = "toggle") Invoke-RestMethod "$Base/switch?id=$Id&state=$State" -TimeoutSec 30 }
function Sim-Pot  { param([string]$Id, [double]$Value) Invoke-RestMethod "$Base/pot?id=$Id&value=$Value" -TimeoutSec 30 }
function Sim-Oled { param([string]$Name, [int]$Scale = 6) (Invoke-RestMethod "$Base/oled?name=$Name&scale=$Scale" -TimeoutSec 30).file }
function Sim-Shot { param([string]$Name, [string]$Region = "circuit") (Invoke-RestMethod "$Base/shot?name=$Name&region=$Region" -TimeoutSec 60).file }
function Sim-Last { Invoke-RestMethod "$Base/mqtt/last" -TimeoutSec 30 }
function Sim-Events { param([int]$Since = 0) Invoke-RestMethod "$Base/events?since=$Since" -TimeoutSec 30 }

# Wait (wall clock) until the serial output contains a regex match, or the timeout expires.
function Sim-WaitSerial {
  param([string]$Pattern, [int]$TimeoutSec = 120, [int]$From = -1)
  if ($From -lt 0) { $From = $script:SerialPos }
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $t = Sim-Serial 0
    $tail = if ($t.Length -gt $From) { $t.Substring($From) } else { "" }
    $m = [regex]::Match($tail, $Pattern)
    if ($m.Success) { return $m.Value }
    Start-Sleep -Seconds 2
  }
  return $null
}

function Sim-Part { param([string]$Id) Invoke-RestMethod "$Base/part?id=$Id" -TimeoutSec 20 }

# Hash of the OLED title band (rows 0-8) -> which page is on screen (map built from the TC-01 frames).
$script:PageSigs = Get-Content (Join-Path $PSScriptRoot 'page_sigs.json') -Raw | ConvertFrom-Json
function Sim-OledTitleHash {
  $js = "(() => { const e = document.querySelector('wokwi-ssd1306'); const c = e.shadowRoot.querySelector('canvas'); const d = c.getContext('2d').getImageData(0, 0, c.width, 9).data; let h = 0; for (let i = 0; i < d.length; i += 4) h = (h * 31 + d[i]) | 0; return String(h); })()"
  (Invoke-RestMethod -Uri "$Base/eval" -Method Post -Body $js -ContentType "text/plain" -TimeoutSec 20).result
}
function Sim-OledPageName { $h = Sim-OledTitleHash; $n = $script:PageSigs.$h; if ($n) { $n } else { "OTHER($h)" } }
# Wait until the requested page (FACILITY|THERAPY|SYSTEM|PATIENT|EXTENDED) is showing, then capture it.
function Sim-OledPage {
  param([string]$Page, [string]$Name, [int]$Scale = 6, [int]$TimeoutSec = 40)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ((Sim-OledPageName) -eq $Page) { return (Sim-Oled $Name $Scale) }
    Start-Sleep -Milliseconds 400
  }
  return $null
}
