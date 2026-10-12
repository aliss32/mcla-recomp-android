#!/usr/bin/env bash
set -e

REMOTE_URL="https://github.com/aliss32/mcla-recomp-android.git"

echo "=========================================================="
echo "MCLA Android Port -> GitHub Push Script"
echo "Hedef Repo: $REMOTE_URL"
echo "=========================================================="

# 1. Git Init
if [ ! -d ".git" ]; then
    echo "[1/5] Git deposu başlatılıyor..."
    git init
else
    echo "[1/5] Git deposu hazır."
fi

# 2. Remote Origin
echo "[2/5] Remote ayarlanıyor..."
if git remote | grep -q "^origin$"; then
    git remote set-url origin "$REMOTE_URL"
else
    git remote add origin "$REMOTE_URL"
fi

# 3. Add
echo "[3/5] Dosyalar ekleniyor..."
git add .

# 4. Commit
echo "[4/5] Commit oluşturuluyor..."
if [ -n "$(git status --porcelain)" ]; then
    git commit -m "feat: Initial MCLA Android port with Vulkan & Turnip driver support"
else
    echo "Zaten commit edilmiş."
fi

# 5. Push
echo "[5/5] Push işlemi yapılıyor (main)..."
git branch -M main
git push -u origin main

echo "BAŞARILI! Proje yüklendi: https://github.com/aliss32/mcla-recomp-android"
echo "Actions takibi: https://github.com/aliss32/mcla-recomp-android/actions"
