# Native renderer Stage 2 reconnaissance (Aşama 2.0)

Okuma yalnızca: SDK kaynakları (`_verify/rexglue-sdk`, CI'nin pin ettiği
`f5337cd` ile aynı ağaç), bizim plugin (`src/native/*`) ve host (`src/mcla_app.cpp`).
SDK'ya ve patches/'e dokunulmadı. Dosya:satır referansları SDK yerel kopyasına göredir;
satır numaraları pin edilen revizyondadır.

## a) Kare presenter'a nasıl gidiyor

- `GraphicsSystem::SetupPresentation` (`src/graphics/graphics_system.cpp`) provider'ı
  ve presenter'ı kurar; bizim `MclaNativeGraphicsSystem::CreateProvider`
  (`src/native/graphics_system.cpp:24`) zaten `VulkanProvider::Create(true, with_presentation)`
  ile aynısını yapıyor. Presenter, guest çıktıyı `RefreshGuestOutput` ile alır.
- Xenos plugin'in akışı (`src/graphics/vulkan/command_processor.cpp:2298-2956`,
  `VulkanCommandProcessor::IssueSwap`):
  1. `BeginSubmission(true)` (2298-2316).
  2. `texture_cache_->RequestSwapTexture(...)` (2341) — guest frontbuffer'ını
     (adres+format) host texture'a çeker, RB swap ihtiyacını döndürür.
  3. `presenter->RefreshGuestOutput(w, h, display_w, display_h, refresher)` (2437):
     refresher callback'i presenter'ın guest-output image'ını verir:
     `VulkanPresenter::VulkanGuestOutputRefreshContext` (`include/rex/ui/vulkan/presenter.h:86-103`)
     — `image()`, `image_view()`, `image_version()`, `image_ever_written_previously()`.
  4. Refresher içinde swap kaynağı gamma/FXAA compute ya da graphics pass ile guest
     output image'a çizilir; sonunda `EndSubmission(true)` (2949).
- Guest output image **biçimi sabittir**: `VulkanPresenter::kGuestOutputFormat =
  VK_FORMAT_A2B10G10R10_UNORM_PACK32` (`include/rex/ui/vulkan/presenter.h:71`),
  color attachment + sampled + transfer-src kullanımı destekli (h:96-98). Release
  durumunda `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`
  (`kGuestOutputInternalLayout`, h:82), acquire'da `UNDEFINED`'dan geçilebilir (h:72-78).
- **Bizim yol için pratik sonuç:** Stage 2.1'de `IssueSwap`'ta
  `BeginSubmission` → `RefreshGuestOutput` → refresher içinde tek bir fullscreen
  clear/draw → `EndSubmission` sırası yeterli; presenter bağlantısını sıfır shader
  çizimiyle test edebiliriz (aşağıda 2.1 önerisi).
- `IssueCopy` → `VulkanRenderTargetCache::Resolve` çağrısı
  (`src/graphics/vulkan/command_processor.cpp:4413-4430`); resolve/copy aslında
  shared memory'ye compute shader ile yazılır
  (`src/graphics/vulkan/render_target_cache.cpp:1127-1330`), sonrasında
  TextureCache resolve aralığını invalidate eder. Yani Xenos'ta "resolve hedefi"
  shared-memory baytlarıdır; bizim tasarımımızda (host texture başına RT) bu
  akışı **kendi** RT yöneticimizle kuracağız — SDK'nin EDRAM/shared-memory
  tabanlı resolve makinesini aynen devralmayacağız.

## b) SDK'da hazır olan bileşenler

Hedef organizasyon: `rexgpu-xenos` tüm bunları tek .so'da derler
(`src/graphics/CMakeLists.txt:7-55`). Bizim plugin ayrı bir .so; kullanmak
istediğimiz TÜ'leri kendi hedefimize eklememiz gerekir (bizim CMake'te zaten
`REXGPU_MCLA_NATIVE_SOURCES` ile yapıyoruz, `mcla-recomp/CMakeLists.txt:50-68`).

| Bileşen | Dosyalar | Hedef | Plugin'e eklenebilir mi |
|---|---|---|---|
| Ucode çözümleme (analiz, interpolator mask, memexport bitleri) | `pipeline/shader/shader.cpp`, `include/rex/graphics/pipeline/shader/shader.h` | rexgpu-xenos (core bölüm) | Evet — saf, cvar yok |
| Ucode disasm + interpreter | `pipeline/shader/translator_disasm.cpp`, `interpreter.cpp` | rexgpu-xenos | Evet (opsiyonel) |
| SPIR-V çevirici (ucode→SPIR-V) | `pipeline/shader/spirv_builder.cpp`, `spirv.cpp`, `spirv_translator*.cpp` (`CMakeLists.txt:34-42`) | rexgpu-xenos (REXGLUE_USE_VULKAN) | Evet — Features yapısı cihaz özelliklerini alır (`vulkan/pipeline_cache.cpp:294-299`), cvar bağlantısı yok |
| GLSL→SPIR-V (host yardımcı shader'lar için) | `vulkan/command_processor.cpp:143` (`CompileGlslToSpirv`) | rexgpu-xenos | Sadece komut satırı yardımı; kendi basit pass'lerimizi kendimiz yazarız |
| PrimitiveProcessor | `primitive_processor.cpp` (core) + `vulkan/primitive_processor.cpp` | rexgpu-xenos | Evet — index tampon okuma, fan→list dönüşümü; cvar yok |
| SharedMemory | `shared_memory.cpp` (core) + `vulkan/shared_memory.cpp` | rexgpu-xenos | Kısmen — bizim modelimizde RT'ler host texture olduğu için shared memory'ye yalnız vfetch/memexport için gerekebilir |
| TextureCache | `pipeline/texture/cache.cpp` (core) + `vulkan/texture_cache.cpp` | rexgpu-xenos | BÜYÜK ama tam: tiled decode + host format tabloları + resolve invalidate; cvar'ları var (bkz. c) |
| Format/decode tabloları | `format/ucode.cpp` (FormatInfo), `pipeline/texture/conversion.cpp` (unswizzle) | **rexcore** (NOT: "Xenos format layer is compiled into rexcore", `CMakeLists.txt:5`) | Zaten linkli (rex::runtime üzerinden) — ek iş yok |
| Resolve yardımcıları (register→ResolveInfo) | `util/draw.cpp` (`draw_util::GetResolveInfo`, `AddMemExportRanges`) — bizim hedefimizde zaten derleniyor | — | Zaten var |
| PipelineCache (Vulkan) | `vulkan/pipeline_cache.cpp` | rexgpu-xenos | Çekirdek amacımız; SpirvShaderTranslator ile birlikte eklenebilir |
| RenderTargetCache (Vulkan) | `vulkan/render_target_cache.cpp` | rexgpu-xenos | **Çatallı/kısmi**: FSI vs host-RT path mantığı ve EDRAM buffer'ı içeriyor. Bizim "her RT host texture" modeli `Path::kHostRenderTargets` ile aynı felsefe ama Resolve hedefi farklı (shared memory). Bu dosyayı olduğu gibi almak yerine kendi RT yöneticimizi yazıp gerekli parçaları (GetColorVulkanFormat, GetDepthVulkanFormat, MSAA dönüşümü) bu dosyadaki eşlemelerle aynı yapmalıyız |

## c) Cvar/flags çakışma riski

- SDK'nin GPU cvar tanımları `src/graphics/flags.cpp`'te
  (`REXCVAR_DEFINE_...`, ~40 tanım; `include/rex/graphics/flags.h`).
- Bizim hedefimiz `flags.cpp`'i derlemiyor (stage 1 kaynak listesi yalnız
  draw/register yardımcıları). Eğer flags.cpp'yi bizim .so'ya da eklersek:
  runtime cvar kaydı process-wide'dır; Xenos plugin .so'daki kopya ile bizim
  kopyamız AYNI adlara iki REXCVAR_DEFINE üretir → çift kayıt riski (assert /
  çakışan override). flags.cpp'yi **asla** bizim hedefe eklememeliyiz.
- Buna karşılık `vulkan/texture_cache.cpp`, `vulkan/pipeline_cache.cpp`,
  `vulkan/render_target_cache.cpp` gibi dosyalar `REXCVAR_GET(...)`
  **okur** (ör. `render_target_cache.cpp:209,247,373`,
  `command_processor.cpp:2318`). Okuma sorun değil — ama okunan cvar'ların
  tanımları flags.cpp'te olduğundan, o dosyaları kendi .so'ya eklemek
  linkte tanımsız simge üretir. Çözüm: (1) okudukları cvar adlarını bizim
  hedefte `REXCVAR_DEFINE` ile aynı varsayılanlarla tekrar tanımlamak (host'ta
  zaten yaptığımız `mcla_gpu_backend` modeli) — ama bu çift tanım riskini
  xenos plugin yüklüyse yeniden getirir; ya da (2) **kısmi kopyalama**:
  ihtiyacımız olan sınıfları cvar okumalarını sabitleyeceğimiz bir ince
  sarmalayıcıyla eklemek. Rapor önerisi 2.0'ın sonunda.
- Bizim mevcut cvar'larımız (`mcla_gpu_backend`, `mcla_native_census`,
  `mcla_native_dump_frame` eklenecek) host'ta tanımlı (`src/mcla_app.cpp:14-24`)
  ve plugin `REXCVAR_QUERY` ile okuyor (`src/native/command_processor.cpp:22-24`)
  — bu desen korunmalı; yeni stage-2 cvar'ları da host'ta tanımlanmalı.

## d) Memexport desteği

- Evet, çeviri katmanı memexport'u biliyor:
  `pipeline/shader/spirv_translator_memexport.cpp` ayrı bir TU; `Shader`
  analizinde `memexport_eM_written()` maskesi var
  (`vulkan/command_processor.cpp:3659, 3699`), `draw_util::AddMemExportRanges`
  yazma aralıklarını hesaplıyor (`util/draw.cpp`), ve cihazda
  `vertexPipelineStoresAndAtomics` / `fragmentStoresAndAtomics` gerektiriyor
  (3659-3666, 3699-3706). Sonuçlar readback buffer'ından guest belleğe
  kopyalanıyor (`command_processor.cpp:4395-4411`).
- Adreno 7c Gen3: `fragmentStoresAndAtomics` var (Vulkan 1.3 core değil ama
  genelde destekli); stage 2.3'te şüpheli draw'ları atlayıp sayaca yazacağımız
  için ilk etapta memexport zorunlu değil. Sınır: memexport draw'ları
  pipeline gerektirir; atlayacaksak draw_failure olarak saymalı, sessiz
  bırakmamalıyız.

## e) Host format eşleme tabloları (SDK'da hazır, xenos.h ile doğrulandı)

- Renk RT (`ColorRenderTargetFormat`, `include/rex/graphics/xenos.h:298-318`):
  `k_8_8_8_8=0`, `k_8_8_8_8_GAMMA=1`, `k_2_10_10_10=2`, `k_2_10_10_10_FLOAT=3`
  (7e3 RGB + unorm A — Doggett sunumu kaynak gösterilmiş), `k_16_16=4`,
  `k_16_16_16_16=5`, `k_16_16_FLOAT=6`, `k_16_16_16_16_FLOAT=7`,
  `k_2_10_10_10_FLOAT_AS_16_16=12`, `k_32_FLOAT=14`, `k_32_32_FLOAT=15`.
- Derinlik RT (`DepthRenderTargetFormat`, `xenos.h` — `kD24S8=0`,
  `kD24FS8=1` [20e4, 0..2)).
- TextureFormat numaraları census'taki dfmt değerleriyle birebir:
  `k_8_8_8_8=6`(ColorFormat aynı numara), `k_16_16_16_16_FLOAT=32`,
  `k_32_FLOAT=36` (`xenos.h:502,508,512` ve ColorFormat tablosu `xenos.h:543-565`).
  Census'taki "dfmt 6 / 32 / 36" = 8_8_8_8 / 16_16_16_16_FLOAT / 32_FLOAT — doğrulandı.
- Vulkan eşlemeleri (kopyalanacak doğrular):
  - `GetDepthVulkanFormat` (`vulkan/render_target_cache.cpp:~` "GetDepthVulkanFormat"):
    D24S8 → `VK_FORMAT_D24_UNORM_S8_UINT` (destekleniyorsa) değilse
    `VK_FORMAT_D32_SFLOAT_S8_UINT`; D24FS8 → daima `VK_FORMAT_D32_SFLOAT_S8_UINT`
    (20e4 doğrudan yok; float depth ile saklanır).
  - `GetColorVulkanFormat` (aynı dosya): 8_8_8_8 → `VK_FORMAT_R8G8B8A8_UNORM`
    (R/B swizzle imageView'la), 2_10_10_10_FLOAT → `VK_FORMAT_R16G16B16A16_SFLOAT`
    (7e3→f32 dönüşümü shader tarafında `Float7e3To32` /
    `UnclampedFloat32To7e3` yardımcılarıyla — `spirv_translator_rb.cpp:26`,
    `spirv_translator_memexport` clear sabitleri `xenos::Float7e3To32`),
    16_16_16_16 → R16G16B16A16_SNORM (fallback SFLOAT), 16_16_16_16_FLOAT →
    `VK_FORMAT_R16G16B16A16_SFLOAT`, 32_FLOAT → `VK_FORMAT_R32_SFLOAT`.
  - MSAA: `MsaaSamples` k1X/k2X/k4X (`xenos.h`); 2x→4x host fallback mantığı
    ve `msaa_2x_attachments_supported` tespiti
    (`vulkan/render_target_cache.cpp:372-400, 1763-1769`).
  - Swap (renk) formatı: guest `k_2_10_10_10` PWL gamma,
    `k_8_8_8_8` 256-entry table gamma beklenir
    (`command_processor.cpp:2459-2467`) — MCLA frontbuffer'ı 8_8_8_8, yani
    256-entry gamma rampasıyla sunum yapmalıyız; census'ta frontbuffer dfmt=6.
  - Guest output: `VK_FORMAT_A2B10G10R10_UNORM_PACK32`, RB swizzle gerekirse
    compute/graphics fallback var (`command_processor.cpp:2468-2489, 2905-2921`).

## Yeniden kullan / sıfırdan yaz önerisi

**Yeniden kullan (kendi hedefimize kaynak olarak ekleyip derleyeceğiz):**
1. `pipeline/shader/*` (shader.cpp + spirv* seti) — ucode→SPIR-V tam çözüm,
   elle yazılmaz.
2. `primitive_processor.cpp` (core) — index/prim dönüşümü.
3. `vulkan/pipeline_cache.cpp` — SpirvShaderTranslator kurulumu
   (`pipeline_cache.cpp:288-314`) ve pipeline derleme mantığı.
4. `vulkan/shader.cpp` + `shared_memory.cpp` (vfetch için gerekliyse).
5. Format tabloları ve yardımcılar: `format/ucode.cpp` zaten rexcore'da;
   `GetColorVulkanFormat`/`GetDepthVulkanFormat`/MSAA eşlemelerini kendi RT
   yöneticimize aynı değerlerle gömeceğiz (tablo kopyası, değil SDK dosyası).

**Sıfırdan yaz / çatalla:**
1. **RenderTarget/Resolve yöneticisi**: (edram_base, pitch, msaa, format)
   anahtarlı host RT'ler; `vkCmdClearAttachments`/`vkCmdClearDepthStencilImage`
   (clear registerları: `RB_COLOR_CLEAR`, `RB_COLOR_CLEAR_LO`, `RB_DEPTH_CLEAR`,
   `RB_STENCIL_CLEAR` — `draw_util::ResolveInfo` alanları
   `util/draw.h:511-513`) ve resolve'da `vkCmdResolveImage`/`vkCmdCopyImage`
   ile hedef host texture'a. Xenos'un shared-memory compute kopyasını devralmayacağız.
2. **Sunum**: `IssueSwap`'ta (base'den gelen frontbuffer ptr) resolve edilen
   son RT'yi guest output'a blit eden küçük bir pass + `RefreshGuestOutput`.
   Gamma rampası 2.1-2.2'de düz passthrough; 2.4+'ta 256-entry tablo
   (SDK'nin rampa upload mekaniği benzeri ama basitleştirilmiş).
3. **TextureCache**: sdk `vulkan/texture_cache.cpp`'i eklemek ideal ama cvar
   bağımlılıkları yüzünden önce kendi küçük (2D, base+mip, tiled 1/2/3bpb +
   8bpb) uploader'ımızı yazıp yerinde doğrulayacağız; sonra gerekiyorsa SDK
   cache'ine geçeriz.

**Riskler:**
- `flags.cpp` kopyalanamaz (c) — yeni .so'lara cvar TANIMI koymayacağız.
- `vulkan/render_target_cache.cpp`'i olduğu gibi eklemek Resolve→shared memory
  varsayımını getirir; kullanmıyoruz.
- GLSL/SPIR-V derleyicisi (glslang) plugin .so'ya linklenmeli
  (`CompileGlslToSpirv` için); CMake'te SDK'nin glslang hedefi var mı
  teyit edilecek (2.1 öncesi kontrol).
- Census oracle korunacak; draw sayıları/resolve sayıları her karede
  census ile karşılaştırılabilir log üretilecek.

## Aşama 2.1 planı (ön iz, onaydan sonra uygulanır)

`IssueSwap` (plugin'de) → `BeginSubmission(true)` → `RefreshGuestOutput`
callback'inde guest output image'a `vkCmdClearColorImage` ile kare N'e göre
renk (N%3 gibi) → `EndSubmission(true)`. Log: `stage2.1 frame N color=...`.
Ek SDK kaynağı gerekmez; `BeginSubmission/EndSubmission` base'de sanal
olduğu için plugin hâlen census-only derlenebilir (base'in Vulkan
komut işlemcisi değil, bizim processor'un kendi submission mantığı —
2.1'de gerekirse `VulkanCommandProcessor` tabanını devralıp mi kuruyoruz
yoksa hafif kendi submission'ımızı mı yazıyoruz, bu 2.1'in ilk kod
kararıdır; rapor edilecek).
