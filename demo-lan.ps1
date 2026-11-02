#!/usr/bin/env pwsh
# =============================================================================
#  demo-lan.ps1 — LAN Demo Startup Script
#  Starts C++ TCP Chat Server, WebSocket Bridge, and Vite Frontend.
#  Run from the repo root: .\demo-lan.ps1
#
#  Requirements: Node.js, npm packages installed in bridge/ and frontend/
#  C++ server already compiled to build\chat_server.exe
# =============================================================================

param (
    [int]   $TcpPort    = 8080,   # C++ server TCP port
    [int]   $BridgePort = 8081,   # WebSocket bridge port (internal only)
    [int]   $FrontPort  = 5173,   # Vite dev server port (LAN-visible)
    [string]$DbFile     = "chat_history.db"  # SQLite path for the server
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot   # repo root

# ── 1. Detect LAN IP ───────────────────────────────────────────────────────
$LanIp = (Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.*' -and
                   $_.PrefixOrigin -in @('Dhcp','Manual') } |
    Sort-Object InterfaceMetric |
    Select-Object -First 1).IPAddress

if (-not $LanIp) {
    Write-Warning "Could not auto-detect LAN IP. Falling back to 10.244.213.83"
    $LanIp = "10.244.213.83"
}

Write-Host ""
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "  TCP CHAT SYSTEM — LAN DEMO LAUNCHER" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "  LAN IP detected  : $LanIp" -ForegroundColor Green
Write-Host "  C++ server       : 0.0.0.0:$TcpPort"
Write-Host "  WebSocket bridge : 0.0.0.0:$BridgePort  (internal)"
Write-Host "  Frontend (Vite)  : http://${LanIp}:${FrontPort}"
Write-Host "  Remote clients   : http://${LanIp}:${FrontPort}"
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

# ── 2. Pre-flight checks ───────────────────────────────────────────────────
$ServerExe = Join-Path $Root "build\chat_server.exe"
if (-not (Test-Path $ServerExe)) {
    Write-Error "chat_server.exe not found at: $ServerExe`nRun: cmake --build build --config Release"
}

$BridgeIndex = Join-Path $Root "bridge\src\index.ts"
if (-not (Test-Path $BridgeIndex)) {
    Write-Error "Bridge source not found at: $BridgeIndex"
}

$FrontendDir = Join-Path $Root "frontend"
if (-not (Test-Path (Join-Path $FrontendDir "node_modules"))) {
    Write-Error "Frontend node_modules missing. Run: cd frontend && npm install"
}

# ── 3. Start C++ server ────────────────────────────────────────────────────
Write-Host "[1/3] Starting C++ TCP Chat Server on port $TcpPort ..." -ForegroundColor Yellow
$DbPath = Join-Path $Root $DbFile
$ServerProc = Start-Process -FilePath $ServerExe `
    -ArgumentList "--port", $TcpPort, "--db", $DbPath `
    -PassThru -NoNewWindow
Write-Host "      PID $($ServerProc.Id)  (ctrl+c in its window to stop)"
Start-Sleep -Milliseconds 800

# Verify it's listening
$listening = $false
for ($i = 0; $i -lt 8; $i++) {
    $conn = Test-NetConnection -ComputerName 127.0.0.1 -Port $TcpPort -InformationLevel Quiet -WarningAction SilentlyContinue
    if ($conn) { $listening = $true; break }
    Start-Sleep -Milliseconds 500
}
if ($listening) {
    Write-Host "      ✓ C++ server listening on :$TcpPort" -ForegroundColor Green
} else {
    Write-Warning "C++ server does not appear to be listening on :$TcpPort yet. Continuing anyway."
}

# ── 4. Start WebSocket bridge ──────────────────────────────────────────────
Write-Host "[2/3] Starting WebSocket Bridge on port $BridgePort ..." -ForegroundColor Yellow
$TsxPath = Join-Path $Root "bridge\node_modules\tsx\dist\cli.mjs"
$BridgeProc = Start-Process -FilePath "node" `
    -ArgumentList $TsxPath, "src\index.ts",
                  "--host", "0.0.0.0",
                  "--port", $BridgePort,
                  "--tcp-host", "127.0.0.1",
                  "--tcp-port", $TcpPort `
    -WorkingDirectory (Join-Path $Root "bridge") `
    -PassThru -NoNewWindow
Write-Host "      PID $($BridgeProc.Id)"
Start-Sleep -Milliseconds 1200

$bridgeOk = $false
for ($i = 0; $i -lt 8; $i++) {
    $conn = Test-NetConnection -ComputerName 127.0.0.1 -Port $BridgePort -InformationLevel Quiet -WarningAction SilentlyContinue
    if ($conn) { $bridgeOk = $true; break }
    Start-Sleep -Milliseconds 500
}
if ($bridgeOk) {
    Write-Host "      ✓ Bridge listening on :$BridgePort" -ForegroundColor Green
} else {
    Write-Warning "Bridge does not appear to be listening on :$BridgePort yet."
}

# ── 5. Start Vite frontend ─────────────────────────────────────────────────
Write-Host "[3/3] Starting Vite Dev Server on port $FrontPort (all interfaces) ..." -ForegroundColor Yellow
$ViteProc = Start-Process -FilePath "npm" `
    -ArgumentList "run", "dev" `
    -WorkingDirectory $FrontendDir `
    -PassThru -NoNewWindow
Write-Host "      PID $($ViteProc.Id)"
Start-Sleep -Seconds 3

# ── 6. Summary ─────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "=====================================================" -ForegroundColor Green
Write-Host "  ALL SERVICES RUNNING" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Green
Write-Host "  Local browser  : http://localhost:$FrontPort" -ForegroundColor White
Write-Host "  Other devices  : http://${LanIp}:${FrontPort}" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Bridge URL shown in ChatDesk login:" -ForegroundColor White
Write-Host "    ws://<device-ip>:$FrontPort/ws  (auto-filled)" -ForegroundColor White
Write-Host ""
Write-Host "  PIDs: Server=$($ServerProc.Id)  Bridge=$($BridgeProc.Id)  Vite=$($ViteProc.Id)"
Write-Host "  Press any key to STOP ALL services ..." -ForegroundColor Yellow
Write-Host "=====================================================" -ForegroundColor Green

# ── 7. Wait for keypress then tear down ────────────────────────────────────
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")

Write-Host "`nStopping services..." -ForegroundColor Yellow
Stop-Process -Id $ViteProc.Id    -Force -ErrorAction SilentlyContinue
Stop-Process -Id $BridgeProc.Id  -Force -ErrorAction SilentlyContinue
Stop-Process -Id $ServerProc.Id  -Force -ErrorAction SilentlyContinue
Write-Host "Done." -ForegroundColor Green
