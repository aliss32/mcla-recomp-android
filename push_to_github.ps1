param(
    [string]$RemoteUrl = "https://github.com/aliss32/mcla-recomp-android.git"
)

$ErrorActionPreference = "Stop"
$projectDir = $PSScriptRoot

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "MCLA Android Port -> GitHub Push Script" -ForegroundColor Cyan
Write-Host "Hedef Repo: $RemoteUrl" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

Set-Location $projectDir

# 1. Git kontrolü ve başlatma
if (-not (Test-Path ".git")) {
    Write-Host "[1/5] Git deposu başlatılıyor..." -ForegroundColor Yellow
    git init
} else {
    Write-Host "[1/5] Mevcut Git deposu algılandı." -ForegroundColor Green
}

# 2. Remote (origin) ayarı
Write-Host "[2/5] Remote (origin) adresi ayarlanıyor..." -ForegroundColor Yellow
$remotes = git remote
if ($remotes -contains "origin") {
    git remote set-url origin $RemoteUrl
} else {
    git remote add origin $RemoteUrl
}

# 3. Dosyaları sahneye alma (add)
Write-Host "[3/5] Dosyalar ekleniyor (git add .)..." -ForegroundColor Yellow
git add .

# 4. Commit oluşturma
Write-Host "[4/5] Commit oluşturuluyor..." -ForegroundColor Yellow
$status = git status --porcelain
if ($status) {
    git commit -m "feat: Initial MCLA Android port with Vulkan & Turnip driver support"
} else {
    Write-Host "Değişiklik yok veya zaten commit edilmiş." -ForegroundColor Green
}

# 5. Branch adı ve Push
Write-Host "[5/5] 'main' branch'ine geçilip GitHub'a yükleniyor..." -ForegroundColor Yellow
git branch -M main

try {
    Write-Host "Remote ile senkronize ediliyor..." -ForegroundColor Yellow
    git pull origin main --rebase --allow-unrelated-histories
    git push -u origin main
    Write-Host ""
    Write-Host "BAŞARILI! Proje başarıyla https://github.com/aliss32/mcla-recomp-android adresine yüklendi." -ForegroundColor Green
    Write-Host "GitHub Actions otomatik olarak tetiklenecektir:" -ForegroundColor Cyan
    Write-Host "https://github.com/aliss32/mcla-recomp-android/actions" -ForegroundColor Cyan
} catch {
    Write-Host "Normal push reddedildi, force push (üzerine yazma) deneniyor..." -ForegroundColor Yellow
    try {
        git push -u origin main --force
        Write-Host ""
        Write-Host "BAŞARILI (Force Push)! Proje başarıyla yüklendi." -ForegroundColor Green
        Write-Host "GitHub Actions: https://github.com/aliss32/mcla-recomp-android/actions" -ForegroundColor Cyan
    } catch {
        Write-Host ""
        Write-Host "DİKKAT: Push işlemi sırasında hata oluştu. Kimlik doğrulama veya yetki sorunu olabilir." -ForegroundColor Red
    }
}
