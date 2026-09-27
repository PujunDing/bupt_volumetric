<#
.SYNOPSIS
One-shot launcher for the bupt_volumetric three-process localhost loopback:
signaling server, receiver, sender.

.DESCRIPTION
Starts (or connects to) the signaling server, launches the receiver first so it
joins before the sender's offer is routed, then runs the sender. The sender exits
after sending its frames; the receiver's latency window stays open — close it to
stop the demo and tear down the server.

.EXAMPLE
.\run_loopback.ps1
.\run_loopback.ps1 -Role Server
.\run_loopback.ps1 -SignalingPort 39000 -RunId demo1 -Seed 42
#>
[CmdletBinding()]
param(
  [ValidateSet('Both', 'Server', 'Sender', 'Receiver')]
  [string]$Role = 'Both',
  [string]$RunId = 'loopback',
  [string]$SignalingHost = '127.0.0.1',
  [int]$SignalingPort = 9090,
  [long]$Seed = 7,
  [string]$BuildDir = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($BuildDir)) {
  $BuildDir = Join-Path $projectRoot '..\out\bupt_volumetric\clang-runtime'
}
$BuildDir = (Resolve-Path $BuildDir).Path

$serverExe = Join-Path $BuildDir 'bupt_volumetric_signaling_server.exe'
$senderExe = Join-Path $BuildDir 'bupt_volumetric_sender.exe'
$receiverExe = Join-Path $BuildDir 'bupt_volumetric_receiver.exe'
$logDir = Join-Path $BuildDir 'loopback-logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Resolve-Required([string]$path) {
  if (-not (Test-Path $path)) {
    throw "Missing application binary: $path (build the windows-clang-runtime preset first)"
  }
  return $path
}
$serverExe = Resolve-Required $serverExe
$senderExe = Resolve-Required $senderExe
$receiverExe = Resolve-Required $receiverExe

function Build-CommonArgs {
  $args = @()
  $args += ('--run-id=' + $RunId)
  $args += ('--result-dir=' + (Join-Path $logDir 'results'))
  $args += ('--signaling-host=' + $SignalingHost)
  $args += ('--signaling-port=' + [string]$SignalingPort)
  $args += ('--seed=' + [string]$Seed)
  return $args
}

switch ($Role) {
  'Server' {
    Write-Host "Starting signaling server on ${SignalingHost}:${SignalingPort} ..."
    & $serverExe @(Build-CommonArgs) @('--role=signaling')
    return
  }

  'Sender' {
    Write-Host "Starting sender (connecting to ${SignalingHost}:${SignalingPort}) ..."
    & $senderExe @(Build-CommonArgs) @('--role=sender', '--peer-id=sender')
    return
  }

  'Receiver' {
    Write-Host "Starting receiver (connecting to ${SignalingHost}:${SignalingPort}) ..."
    & $receiverExe @(Build-CommonArgs) @('--role=receiver', '--peer-id=receiver')
    return
  }

  'Both' {
    $serverStdout = Join-Path $logDir 'server.out.log'
    $serverStderr = Join-Path $logDir 'server.err.log'
    $serverArgs = (Build-CommonArgs) + @('--role=signaling')
    $server = Start-Process -FilePath $serverExe -ArgumentList $serverArgs `
      -PassThru -WindowStyle Hidden `
      -RedirectStandardOutput $serverStdout -RedirectStandardError $serverStderr
    Write-Host "Signaling server started (PID $($server.Id))."
    Start-Sleep -Seconds 1

    $receiverErr = Join-Path $logDir 'receiver.err.log'
    $receiverArgs = (Build-CommonArgs) + @('--role=receiver', '--peer-id=receiver')
    $receiver = Start-Process -FilePath $receiverExe -ArgumentList $receiverArgs `
      -PassThru -RedirectStandardError $receiverErr
    Write-Host "Receiver window started (PID $($receiver.Id)); giving it time to join ..."
    Start-Sleep -Seconds 2

    $senderErr = Join-Path $logDir 'sender.err.log'
    $senderArgs = (Build-CommonArgs) + @('--role=sender', '--peer-id=sender')
    $sender = Start-Process -FilePath $senderExe -ArgumentList $senderArgs `
      -PassThru -RedirectStandardError $senderErr
    Write-Host "Sender window started (PID $($sender.Id)). Close both windows to stop."

    # Both the receiver and sender keep their windows open after the run; wait
    # until the user closes both.
    while ((-not $receiver.HasExited) -or (-not $sender.HasExited)) {
      Start-Sleep -Milliseconds 500
    }

    Write-Host "Sender summary (see $senderErr):"
    if (Test-Path $senderErr) { Get-Content $senderErr | ForEach-Object { Write-Host $_ } }

    Write-Host "Windows closed; stopping the signaling server."
    if (-not $server.HasExited) { Stop-Process -Id $server.Id -Force }
    Write-Host "Finished. Logs are in $logDir"
  }
}
