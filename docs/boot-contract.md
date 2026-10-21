# UEFI → XNU açılış sözleşmesi

## Doğrulanan mevcut sınır

`beyond2lte-uefi` deposunun `BUILD.md` dosyası UEFI'nin `boot` bölümüne yazıldığını, `sboot` bölümüne yazılmadığını belirtiyor. `EbsTest` EFI uygulaması GOP ve `ExitBootServices` yolunu sınamak için mevcut. Bu nedenle XNU yolu, firmware'i yeniden yazmak yerine mevcut UEFI içinden yüklenir. Fiziksel zincirde Samsung'un ilk aşaması yerinde kalır.

Mevcut UEFI bellek haritası Windows için korumalıdır. Kodun `MemoryMapLib.c` dosyasında UEFI FD `0xC9000000..0xC9400000`, stack `0xC9400000..0xC9440000`, framebuffer `0xCA000000..0xCB400000`, DXE heap `0xCB400000..0xCF000000` ve genel amaçlı üst RAM `0x900000000..0x980000000` olarak listeleniyor. Bunlar çalışma zamanı ölçümü yerine geçmez. XNU yükleyicisi EFI bellek haritasından gerçek kullanılabilir sayfaları seçmeli ve korunan aralıklara yazmamalıdır.

## XNU 12377'nin girişi

`pexpert/pexpert/arm64/boot.h` ARM64 `boot_args` yapısını tanımlar: `virtBase`, `physBase`, `memSize`, `topOfKernelData`, video, `deviceTreeP`, komut satırı ve diğer alanlar. `osfmk/arm64/start.s` içindeki `start_first_cpu`, x0'dan bu yapıyı okur, MMU tablolarını kurar ve `arm_init` aşamasına geçer. Giriş EFI uygulaması değildir. Mach-O segment yerleşimi, adres kaydırması ve fiziksel bellek aralığı birlikte doğrulanmadan bu adrese dallanılmaz.

`pexpert/pexpert/arm64/board_config.h` içinde Exynos 9820 tanımı yoktur. `VMAPPLE` tanımı Apple sanal platformu, GICv3 ve Apple'a özgü işlemci özellikleri içerir; doğrudan beyond2lte yapılandırması olarak kullanılamaz. Cihaz için ayrı yapılandırma gerekir.

## EFI yükleyicinin kabul koşulları

- Çalışan UEFI'nin exception level'ı, GOP biçimi ve kullanılabilir fiziksel bellek aralıkları aygıtta ölçülmüş olmalı.
- Yüklenmiş ARM64 Mach-O'nun tüm segmentleri ayrılan fiziksel RAM'e sığmalı; taşma ve korunan alan çakışması reddedilmeli.
- XNU'ya verilecek Apple biçimindeki aygıt ağacı, framebuffer ve bellek bilgisi oluşturulmalı.
- `ExitBootServices` çağrısı güncel memory-map key ile yapılmalı; başarısızsa harita yenilenmeli.
- EBS sonrası UEFI hizmeti kullanılmamalı; giriş EL1 ve x0 `boot_args` önkoşulları karşılanmalı.
- İlk gerçek aygıt denemesinde sonuç, ekrandaki/UART'daki XNU erken çıktı ile doğrulanmalı.
