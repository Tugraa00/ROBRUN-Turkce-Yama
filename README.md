<p align="center">
  <img src="banner.png" alt="ROB RUN Türkçe Yama" width="100%" />
</p>

<h1 align="center">ROB RUN — Türkçe Yama</h1>

<p align="center">
  Steam'deki <strong>ROB RUN</strong> oyunu için geliştirilmiş, orijinal oyun dosyalarına dokunmadan çalışan tek dosya Türkçe yama aracı.<br/>
  <strong>C++ (Dear ImGui + DirectX 11)</strong> arayüz ve gömülü <strong>Python</strong> yama motoru ile hazırlanmıştır.
</p>

<p align="center">
  <a href="https://discord.gg/WAB4JcntbH"><img src="https://img.shields.io/badge/Discord-Sunucuya%20Kat%C4%B1l-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord Server" /></a>
</p>

---

## Özellikler

- **Orijinal Dosyalara Dokunmaz:** Steam oyun klasöründeki orijinal dosyaları değiştirmez; yamayı Unity Addressables önbelleğine uygular.
- **4.787 Satır Türkçe Çeviri:** Oyun içi menüler, arayüz ve metinler (`src/data/tr.json`).
- **Türkçe SDF Font Desteği:** Oyun fontlarına `ç, ğ, ı, İ, ö, ş, ü` karakterleri otomatik olarak işlenir (`src/sdf_glyphs.py`).
- **C++ ImGui + DX11 Arayüzü:** Harici Python veya ekstra kütüphane kurulumu gerektirmeyen tek `.exe` deneyimi.
- **Otomatik Oyun Kontrolü:** PC açılışında oyunun güncellenip güncellenmediğini kontrol eder; gerekirse yamayı otomatik yeniler.
- **Uzaktan Bakım Modu:** Firebase Realtime Database üzerinden bakım durumu kontrol edilir.
- **Tek Tıkla Kaldırma:** İstediğiniz zaman yamayı kaldırıp oyunu orijinal İngilizce haline döndürebilirsiniz.

---

## Kurulum & Kullanım

1. Steam'de **ROB RUN** oyununun kurulu olduğundan ve oyun içi dil ayarının **English (İngilizce)** seçili olduğundan emin olun.
2. `RobRunTurkceYama.exe` dosyasını çalıştırın ve ilk açılıştaki bilgilendirme ekranını onaylayın.
3. **YAMAYI KUR** butonuna tıklayın. Yama kurulduktan sonra oyun otomatik olarak başlatılacaktır.

---

## Proje Yapısı

```text
robrun/
├── banner.png                  # Proje banner görseli
├── logo.png                    # Proje logosu
├── README.md
├── src/
│   ├── patch_engine.py         # Unity Addressables yama / durum / kaldırma motoru
│   ├── sdf_glyphs.py           # Türkçe karakter SDF font yaması
│   ├── installer_ui.py         # Opsiyonel Python/Tkinter arayüz
│   ├── data/
│   │   ├── tr.json             # 4.787 satır Türkçe çeviri verisi
│   │   └── builtin_keys.json   # Dahili çeviri anahtarları
│   └── cpp_menu/
│       ├── App.h               # Uygulama durumu ve tanımlamalar
│       ├── main.cpp            # Win32 + DX11 + Firebase + Engine IPC
│       ├── menu.h / menu.cpp   # Dear ImGui menü tasarımı
│       └── resource.rc         # Gömülü fontlar ve engine.exe kaynağı
└── web/
    ├── index.html              # Tanıtım ve Google Drive indirme sayfası
    └── database.rules.json     # Firebase Realtime Database kuralları
```

---

## Kaynaktan Derleme (Geliştiriciler İçin)

1. **Python Yama Motoru (`engine.exe`):**
   ```powershell
   pyinstaller --onefile --console --collect-all archspec --add-data "src/data;data" --name engine src/patch_engine.py
   ```
   Oluşan `engine.exe` dosyasını `cpp_menu/assets/engine.exe` konumuna yerleştirin.

2. **C++ Arayüzü (`RobRunTurkceYama.exe` - MinGW / w64devkit):**
   ```powershell
   g++ -std=c++17 -O2 -finput-charset=UTF-8 -fexec-charset=UTF-8 -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN `
     -I"src" -I"third_party/imgui" -I"third_party/imgui/backends" `
     third_party/imgui/*.cpp third_party/imgui/backends/imgui_impl_win32.cpp third_party/imgui/backends/imgui_impl_dx11.cpp `
     src/main.cpp src/menu.cpp resource.res `
     -o RobRunTurkceYama.exe -mwindows -municode -static -static-libgcc -static-libstdc++ `
     -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lole32 -lshell32 -luuid -lwinhttp
   ```

---

## Sorumluluk ve Güvenlik

- Yama işlemi risklidir; ban yerseniz sorumluluk kullanıcıya aittir.
- Uygulama açık kaynaklıdır; kesinlikle virüs, trojan veya kullanıcı verisi toplayan zararlı bir kod içermez.
- Hiçbir şifre, Steam hesap bilgisi veya kişisel dosya okunmaz ya da paylaşılmaz.

---

**Geliştirici:** Tuğra A. • **GitHub:** https://github.com/Tugra0069/ROBRUN-Turkce-Yama • **Discord:** https://discord.gg/WAB4JcntbH
