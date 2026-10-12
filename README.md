# Midnight Club: Los Angeles (MCLA) Android Port

Bu proje, [mcla-recomp](https://github.com/holdmysocks/mcla-recomp) ve [LibertyRecompAndroid](https://github.com/vaduur/LibertyRecompAndroid) depolarının tersine mühendislik ile birleştirilerek oluşturulmuş **Android ARM64 native Vulkan portudur**.

---

## Özellikler

1. **Özel Vulkan Sürücüsü (Turnip / Mesa Adreno) Desteği:**
   * `native/driver_proxy.cpp` ve `libadrenotools` entegrasyonu sayesinde sistem sürücüsü haricinde harici **Turnip `.zip` sürücüleri** veya gömülü Turnip paketleri dinamik olarak yüklenebilir.
   * Cihazın Adreno GPU mimarisine göre istediğiniz sürücüyü seçebilirsiniz.
2. **Doğrudan Android Vulkan Yüzeyi (`ANativeWindow` / SDL3):**
   * Emülasyonsuz, doğrudan ARM64 cihazlarda native çalışır.
3. **Dokunmatik Ekran ve Gamepad Desteği:**
   * Dahili dokunmatik arayüz kontrolleri ve Bluetooth/USB kontrolcü desteği.
4. **GitHub Actions CI/CD Entegrasyonu:**
   * `.github/workflows/build-android.yml` ile projenin GitHub'a yüklenmesi halinde otomatik APK derleme süreci tanımlanmıştır.

---

## Proje Yapısı

* **`app/`**: Android Gradle modülü, Launcher ve MclaActivity.
* **`native/`**: `driver_proxy.cpp` ve `libadrenotools` (Turnip zip yükleyici kancası ve Vulkan loader).
* **`mcla-recomp/`**: MCLA oyun mantığı, ReXGlue bağlayıcıları ve yapılandırma dosyaları.
* **`scripts/`**:
  * `build_driver_proxy.sh`: `libvulkan.so`, `libmain_hook.so` ve `libhook_impl.so` kütüphanelerini derler.
  * `build_native.sh`: MCLA native motorunu Android NDK ile derler ve `jniLibs/` altına taşır.
  * `build_apk.sh`: Gradle üzerinden APK paketini oluşturur.
* **`.github/workflows/build-android.yml`**: GitHub Actions üzerinde NDK ve Gradle ile otomatik derleme sağlayan iş akışı.

---

## GitHub Üzerinde Derleme Adımları

1. Bu klasörün içeriğini yeni bir GitHub deposuna yükleyin:
   ```bash
   git init
   git add .
   git commit -m "Initial commit for MCLA Android Port with Vulkan & Turnip support"
   git branch -M main
   git remote add origin https://github.com/KULLANICI_ADINIZ/mcla-android.git
   git push -u origin main
   ```
2. GitHub deponuzun **Actions** sekmesine gidin.
3. `Build MCLA Android APK (Vulkan & Turnip Driver Support)` iş akışını çalıştırın (veya push sonrası otomatik çalışacaktır).
4. Derleme tamamlandığında **Artifacts** bölümünden derlenen APK dosyasını indirebilirsiniz.
