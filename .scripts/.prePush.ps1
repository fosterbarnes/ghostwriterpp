. "$PSScriptRoot\scriptHelper.ps1"
Write-Host "Running pre-push tasks..." -ForegroundColor Yellow
& "$scripts\build.ps1" -Preset release
& "$scripts\buildUpdater.ps1"
& "$scripts\buildInstaller.ps1"
& "$scripts\updateReadme.ps1"
Write-Host "`nPre-push tasks completed." -ForegroundColor Green
