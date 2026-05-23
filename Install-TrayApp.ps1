# Install-TrayApp.ps1 - TrayApp Service Installer/Uninstaller
param([switch]$Uninstall)

$ServiceName = "TrayAppService"
$InstallPath = "C:\TrayApp"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: Administrator privileges required!" -ForegroundColor Red
    exit 1
}

function Install-TrayApp {
    Write-Host "=== TrayApp Service Installation ===" -ForegroundColor Green
    
    $sourcePath = Split-Path $MyInvocation.MyCommand.Path -Parent
    
    # Create install directory
    if (-not (Test-Path $InstallPath)) {
        New-Item -ItemType Directory -Path $InstallPath -Force | Out-Null
    }
    
    # Copy files
    $files = @("TrayService.exe", "TrayApp.exe", "av_database.bin", "av_database.bin.bak", "public_key_cryptoapi.pem")
    foreach ($file in $files) {
        $src = Join-Path $sourcePath $file
        $dst = Join-Path $InstallPath $file
        if (Test-Path $src) {
            Copy-Item $src $dst -Force
            Write-Host "  [OK] $file" -ForegroundColor Green
        } else {
            Write-Host "  [WARN] $file not found" -ForegroundColor Yellow
        }
    }
    
    # Register service
    $svc = Get-Service $ServiceName -ErrorAction SilentlyContinue
    if ($svc) {
        sc.exe stop $ServiceName 2>$null
        Start-Sleep 3
        sc.exe delete $ServiceName 2>$null
        Start-Sleep 2
    }
    
    $binPath = Join-Path $InstallPath "TrayService.exe"
    sc.exe create $ServiceName binPath= "`"$binPath`"" start= auto DisplayName= "TrayApp Service"
    sc.exe description $ServiceName "TrayApp antivirus monitoring and scanning service" 2>$null
    sc.exe start $ServiceName
    
    Write-Host "`nInstallation complete! Service: $ServiceName" -ForegroundColor Green
}

function Uninstall-TrayApp {
    Write-Host "=== TrayApp Service Uninstallation ===" -ForegroundColor Yellow
    
    $svc = Get-Service $ServiceName -ErrorAction SilentlyContinue
    if ($svc) {
        sc.exe stop $ServiceName 2>$null
        Start-Sleep 5
        taskkill /F /IM TrayService.exe 2>$null
        taskkill /F /IM TrayApp.exe 2>$null
        sc.exe delete $ServiceName 2>$null
        Write-Host "  [OK] Service removed" -ForegroundColor Green
    }
    
    if (Test-Path $InstallPath) {
        Remove-Item $InstallPath -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "  [OK] Files removed" -ForegroundColor Green
    }
    
    if (Test-Path "C:\TrayService.log") {
        Remove-Item "C:\TrayService.log" -Force
        Write-Host "  [OK] Logs removed" -ForegroundColor Green
    }
    
    Write-Host "`nUninstallation complete!" -ForegroundColor Green
}

if ($Uninstall) { Uninstall-TrayApp } else { Install-TrayApp }
