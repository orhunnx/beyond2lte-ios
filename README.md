# beyond2lte-ios

Samsung Galaxy S10+ SM-G975F (`beyond2lte`, Exynos 9820) üzerinde Apple'ın açık kaynak XNU çekirdeğini başlatma çalışması. İlk hedef, XNU'nun cihazda açılıp erken konsol çıktısı vermesi. iOS kullanıcı alanı ve grafik arayüzü sonraki aşamalardır.

## Açılış zinciri

Cihazın mevcut ilk aşaması, [beyond2lte UEFI](https://github.com/orhunnx/beyond2lte-uefi) uyarlamasını BOOT bölümünden çalıştırır. Bu depo UEFI'den XNU'ya geçişi geliştirir:

`Samsung S-Boot → beyond2lte UEFI → XNU`.

U-Boot kullanılmaz; UEFI deposu bu depoya kopyalanmaz. S-Boot'un BOOT imajını başlatma görevi sürer. Ayrıntılar [açılış sözleşmesinde](docs/boot-contract.md).

## Sabit kaynak

XNU dalı: `rel/xnu-12377`, commit: `ac9718fb1af618d5ce8678d0dc6e8a58f252216f` ([kaynak](https://github.com/apple-oss-distributions/xnu/tree/rel/xnu-12377)). XNU kaynak ağacı ve Apple firmware dosyaları bu depoya eklenmez. Yerel kaynak hazırlığı için `scripts/fetch-xnu.sh` kullanılabilir.

## Şimdiki kod

`uefi/BootSurvey` mevcut UEFI'nin XNU'ya bırakabileceği ortamı aygıt üzerinde ölçen EFI uygulamasıdır: exception level, GOP framebuffer ve EFI bellek haritasındaki kullanılabilir aralıklar. Bu uygulama XNU'yu henüz yüklemez ve `ExitBootServices` çağırmaz. Derlenip aygıtta çalıştırıldığı iddia edilmez. Geçiş kodu ancak bellek yerleşimi ve platform başlangıcı doğrulandıktan sonra eklenecektir.

## İlk açılış için açık işler

1. EDK II ile `BootSurvey.efi` derlemek, mevcut UEFI Shell'de çalıştırıp ekran/EL/bellek çıktısını almak.
2. ARM64 Mach-O kernelcache'i ve Apple `boot_args` ABI'sini işleyen UEFI yükleyicisini yazmak; `ExitBootServices` sonrası `start_first_cpu` için giriş koşullarını doğrulamak.
3. XNU 12377'de Exynos 9820 için ayrı board/platform yapılandırması, Apple aygıt ağacı biçimi, erken konsol, GIC ve mimari zamanlayıcı uyarlaması yapmak.
4. XNU derlemesi ve ilk aygıt açılışında konsol çıktısı ile sonucu doğrulamak.

Hiçbir BOOT/System bölümüne bu depoyu oluşturma aşamasında yazılmaz.
