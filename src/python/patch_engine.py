"""
ROB RUN Türkçe Yama — Çekirdek Yama Motoru
by Tuğra A.
"""
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import traceback
import webbrowser

PATCH_VERSION = "1.0.2"
STEAM_APPID = "4262460"
BUNDLE_DIR = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(BUNDLE_DIR, "data")
LOG_PATH = os.path.join(os.environ.get("TEMP", os.path.expanduser("~")), "robrun_tr_installer.log")
STATE_FILE = "tr_patch_state.json"
SHORTCUT_NAME = "Rob Run Türkçe.lnk"

EN_BUNDLE = "localization-string-tables-english(unitedstates)(en-us)_assets_all.bundle"
SHARED_BUNDLE = "localization-assets-shared_assets_all.bundle"
INB_BUNDLE = "inbuild_assets_all.bundle"
TABLES = ("StaticStrings", "RealmNames", "TimeFormatterAbbreviations")

PLAIN = str.maketrans("ıİşŞğĞ", "iIsSgG")
UPPER_KEYS = {
    "early_access", "main_menu_button_play", "attention_screen_title", "cancel", "confirm", "ok",
    "error_title", "quit_header", "reconnect_title", "reconnect_confirm", "restart_server_title",
    "settings_game_support", "side_menu_leave_game", "start_download_again",
    "hardware_warning_dialog_continue", "playtest_waiting_for_start_title",
}
UPPER = str.maketrans("abcçdefgğhıijklmnoöprsştuüvyzqwx", "ABCÇDEFGĞHIIJKLMNOÖPRSŞTUÜVYZQWX")


def builtin_safe(key, text):
    """Gömülü fontta bulunmayan Türkçe karakterleri güvenli ASCII karşılıklarına çevirir."""
    if key in UPPER_KEYS:
        parts = re.split(r"(<[^>]*>|\{[^}]*\})", text)
        text = "".join(p if p.startswith(("<", "{")) else p.translate(UPPER) for p in parts)
    return text.translate(PLAIN)


def cache_dir():
    """Oyunun Unity Addressables önbellek klasörünü döndürür."""
    base = os.environ.get("USERPROFILE") or os.path.expanduser("~")
    return os.path.join(base, "AppData", "LocalLow", "Kefir", "ROB RUN", "com.unity.addressables")


def _desktop_dir():
    """Masaüstü klasörünü döndürür."""
    try:
        r = subprocess.run(
            ["powershell", "-NoProfile", "-Command", "[Environment]::GetFolderPath('Desktop')"],
            capture_output=True, text=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        p = (r.stdout or "").strip()
        if p and os.path.isdir(p):
            return p
    except Exception:
        pass
    return os.path.join(os.environ.get("USERPROFILE") or os.path.expanduser("~"), "Desktop")


def find_game_dir():
    """Steam kütüphanelerinden ROB RUN kurulum klasörünü bulur."""
    candidates = []
    if os.environ.get("ROBRUN_DIR"):
        candidates.append(os.environ["ROBRUN_DIR"])

    steam_roots = []
    try:
        import winreg
        for hive, key, val in (
            (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam", "InstallPath"),
        ):
            try:
                with winreg.OpenKey(hive, key) as k:
                    p = winreg.QueryValueEx(k, val)[0]
                    if p:
                        steam_roots.append(p)
            except OSError:
                pass
    except ImportError:
        pass

    for root in list(steam_roots):
        vdf = os.path.join(root, "steamapps", "libraryfolders.vdf")
        if os.path.exists(vdf):
            try:
                with open(vdf, encoding="utf-8", errors="ignore") as f:
                    txt = f.read()
                for p in re.findall(r'"path"\s+"([^"]+)"', txt):
                    steam_roots.append(p.replace("\\\\", "\\"))
                for p in re.findall(r'"\d+"\s+"([A-Za-z]:\\\\[^"]+)"', txt):
                    steam_roots.append(p.replace("\\\\", "\\"))
            except OSError:
                pass

    steam_roots += [
        r"C:\Program Files (x86)\Steam",
        r"C:\SteamLibrary",
        r"D:\Steam",
        r"D:\SteamLibrary",
        r"E:\SteamLibrary",
        r"F:\SteamLibrary",
    ]

    seen = set()
    for lib in steam_roots:
        norm = os.path.normcase(os.path.abspath(lib))
        if norm in seen:
            continue
        seen.add(norm)
        candidates.append(os.path.join(lib, "steamapps", "common", "Rob Run"))

    for g in candidates:
        if g and os.path.exists(os.path.join(g, "ROB RUN.exe")):
            return g
    raise RuntimeError("Rob Run bulunamadı. Oyunun Steam'de kurulu olduğundan emin ol.")


def md5(path):
    """Dosyanın MD5 özetini hesaplar."""
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def padded_name(original_path_len, prefix, stem):
    """Katalogdaki yol ile birebir aynı bayt uzunluğunda yeni dosya adı üretir."""
    need = original_path_len - len(prefix.encode("utf-8")) - len(b".bundle")
    if need <= 0:
        raise RuntimeError("Katalog dosya adı uzunluğu yetersiz.")
    tag = "_tr_patch"
    raw = (stem + tag).encode("utf-8")
    if len(raw) >= need:
        body_bytes = raw[:need]
    else:
        body_bytes = raw + b"_" * (need - len(raw))
    body = body_bytes.decode("utf-8", errors="ignore")
    while len(body.encode("utf-8")) < need:
        body += "_"
    if len(body.encode("utf-8")) != need:
        raise RuntimeError("Dosya adı uzunluğu ayarlanamadı.")
    return body + ".bundle"


class Patcher:
    """ROB RUN Türkçe yama kurulum ve kaldırma yöneticisi."""

    def __init__(self, log):
        self.log = log
        self.game = find_game_dir()
        self.aa = os.path.join(self.game, "ROB RUN_Data", "StreamingAssets", "aa")
        self.sa = os.path.join(self.aa, "StandaloneWindows64")
        self.cache = cache_dir()

        settings_path = os.path.join(self.aa, "settings.json")
        if not os.path.exists(settings_path):
            raise RuntimeError("Oyunun Addressables settings.json dosyası bulunamadı.")
        with open(settings_path, encoding="utf-8") as f:
            settings = json.load(f)

        hash_name = None
        for loc in settings.get("m_CatalogLocations", []):
            if "AddressablesMainContentCatalogCacheHash" in loc.get("m_Keys", []):
                hash_name = os.path.basename(loc["m_InternalId"].replace("\\", "/"))
                break
        if not hash_name or not hash_name.endswith(".hash"):
            raise RuntimeError("Oyunun katalog yapısı değişmiş, yama bu sürüme uymuyor.")

        self.hash_name = hash_name
        self.bin_name = hash_name[:-5] + ".bin"

        for req in (
            os.path.join(self.sa, EN_BUNDLE),
            os.path.join(self.sa, SHARED_BUNDLE),
            os.path.join(self.sa, INB_BUNDLE),
            os.path.join(self.sa, self.bin_name),
            os.path.join(self.sa, self.hash_name),
            os.path.join(DATA_DIR, "tr.json"),
        ):
            if not os.path.exists(req):
                raise RuntimeError(f"Gerekli dosya eksik: {os.path.basename(req)}")

    def fingerprint(self):
        return {
            "version": PATCH_VERSION,
            "catalog": self.bin_name,
            "en": md5(os.path.join(self.sa, EN_BUNDLE)),
            "inbuild": md5(os.path.join(self.sa, INB_BUNDLE)),
            "tr": md5(os.path.join(DATA_DIR, "tr.json")),
        }

    def installed(self):
        try:
            with open(os.path.join(self.cache, STATE_FILE), encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return None

    def up_to_date(self):
        m = self.installed()
        if not m:
            return False
        fp = self.fingerprint()
        files = m.get("files", [])
        return all(m.get(k) == v for k, v in fp.items()) and all(
            os.path.exists(os.path.join(self.cache, fn)) for fn in files
        )

    def builtin_keys(self, id_to_key):
        """Oyunun gömülü (bundle dışı) dosyalarında kullanılan metin anahtarlarını bulur."""
        found = set()
        try:
            import UnityPy
            data_dir = os.path.join(self.game, "ROB RUN_Data")
            files = [
                os.path.join(data_dir, f)
                for f in ("level0", "resources.assets", "sharedassets0.assets", "globalgamemanagers.assets")
                if os.path.exists(os.path.join(data_dir, f))
            ]
            ids = {int(k): v for k, v in id_to_key.items()}
            for f in files:
                env = UnityPy.load(f)
                for o in env.objects:
                    if o.type.name != "MonoBehaviour":
                        continue
                    raw = o.get_raw_data()
                    for i in range(0, len(raw) - 7, 4):
                        v = struct.unpack_from("<q", raw, i)[0]
                        if v in ids:
                            found.add(ids[v])
        except Exception as e:
            self.log(f"Gömülü metin taraması atlandı: {e}")

        bk_path = os.path.join(DATA_DIR, "builtin_keys.json")
        if os.path.exists(bk_path):
            try:
                with open(bk_path, encoding="utf-8") as f:
                    found |= set(json.load(f))
            except Exception:
                pass
        self.log(f"Gömülü fontlu metin: {len(found)}")
        return found

    def build_strings(self, UnityPy, out_path):
        """İngilizce metin tablosunu tr.json ile Türkçeye çevirip bundle olarak kaydeder."""
        with open(os.path.join(DATA_DIR, "tr.json"), encoding="utf-8") as f:
            tr = json.load(f)

        shared_env = UnityPy.load(os.path.join(self.sa, SHARED_BUNDLE))
        keys = {}
        for o in shared_env.objects:
            if o.type.name == "MonoBehaviour":
                t = o.read_typetree()
                if "m_Entries" in t:
                    keys[t["m_Name"].split(" Shared")[0]] = {str(e["m_Id"]): e["m_Key"] for e in t["m_Entries"]}

        builtin = self.builtin_keys(keys.get("StaticStrings", {}))
        env = UnityPy.load(os.path.join(self.sa, EN_BUNDLE))
        done = total = 0
        for o in env.objects:
            if o.type.name != "MonoBehaviour":
                continue
            t = o.read_typetree()
            if "m_TableData" not in t:
                continue
            table = next((p for p in TABLES if t["m_Name"].startswith(p)), None)
            if not table:
                continue
            kmap = keys.get(table, {})
            for e in t["m_TableData"]:
                key = kmap.get(str(e["m_Id"]), str(e["m_Id"]))
                total += 1
                text = tr.get(f"{table}::{key}")
                if not text:
                    continue
                e["m_Localized"] = builtin_safe(key, text) if key in builtin and table == "StaticStrings" else text
                done += 1
            o.save_typetree(t)

        with open(out_path, "wb") as f:
            f.write(env.file.save(packer="lz4"))
        self.log(f"Metinler: {done}/{total} satır Türkçe")

    def build_fonts(self, UnityPy, out_path):
        """Oyun içi SDF fontlara eksik Türkçe harfleri ekler."""
        import sdf_glyphs
        env = UnityPy.load(os.path.join(self.sa, INB_BUNDLE))
        n = sdf_glyphs.patch_font_bundle(env, log=lambda s: None)
        with open(out_path, "wb") as f:
            f.write(env.file.save(packer="lz4"))
        self.log(f"Fontlar: {n} Türkçe harf eklendi")

    def install(self, progress=lambda p: None):
        """Yamayı önbellek klasörüne kurar."""
        import UnityPy
        os.makedirs(self.cache, exist_ok=True)
        self.uninstall(quiet=True, remove_shortcut=False)
        progress(0.1)

        rt_prefix = "{UnityEngine.AddressableAssets.Addressables.RuntimePath}\\StandaloneWindows64\\"
        pd_prefix = "{UnityEngine.Application.persistentDataPath}/com.unity.addressables/"
        redirects, files = [], []

        for src, builder in ((EN_BUNDLE, self.build_strings), (INB_BUNDLE, self.build_fonts)):
            old = rt_prefix + src
            new_name = padded_name(len(old.encode("utf-8")), pd_prefix, src[:-len(".bundle")])
            builder(UnityPy, os.path.join(self.cache, new_name))
            redirects.append((old, pd_prefix + new_name))
            files.append(new_name)
            progress(0.1 + 0.4 * len(files))

        with open(os.path.join(self.sa, self.bin_name), "rb") as f:
            raw = f.read()
        patched = raw
        for old, new in redirects:
            ob, nb = old.encode("utf-8"), new.encode("utf-8")
            if len(ob) != len(nb):
                raise RuntimeError("Katalog yönlendirme bayt uzunluğu eşleşmedi.")
            if raw.count(ob) != 1:
                raise RuntimeError("Katalogda beklenen adres yok — oyun yapısı değişmiş.")
            patched = patched.replace(ob, nb)
        if len(patched) != len(raw):
            raise RuntimeError("Katalog boyutu değişti.")

        with open(os.path.join(self.cache, self.bin_name), "wb") as f:
            f.write(patched)
        with open(os.path.join(self.sa, self.hash_name), encoding="utf-8") as f:
            hash_val = f.read().strip()
        with open(os.path.join(self.cache, self.hash_name), "w", encoding="utf-8") as f:
            f.write(hash_val)

        files += [self.bin_name, self.hash_name]
        marker = dict(
            self.fingerprint(),
            files=files,
            installed=time.strftime("%Y-%m-%d %H:%M"),
        )
        with open(os.path.join(self.cache, STATE_FILE), "w", encoding="utf-8") as f:
            json.dump(marker, f, indent=1, ensure_ascii=False)
        progress(1.0)
        self.log("Yama kuruldu.")

    def uninstall(self, quiet=False, remove_shortcut=True):
        """Yamayı önbellekten ve masaüstü kısayolundan kaldırır."""
        removed_any = False
        for state_name in (STATE_FILE, "robrun_tr.json", "civilim_tr.json"):
            sp = os.path.join(self.cache, state_name)
            if not os.path.exists(sp):
                continue
            try:
                with open(sp, encoding="utf-8") as f:
                    m = json.load(f)
                for fn in m.get("files", []):
                    fp = os.path.join(self.cache, fn)
                    if os.path.exists(fp):
                        os.remove(fp)
                        removed_any = True
            except Exception:
                pass
            try:
                os.remove(sp)
                removed_any = True
            except OSError:
                pass

        # Kalıntı yama bundle'ları veya yönlendirilmiş katalog varsa onları da temizle
        if os.path.isdir(self.cache):
            for fn in os.listdir(self.cache):
                if any(tag in fn for tag in ("_tr_patch", "_robrun_t", "_civilim_tr")):
                    try:
                        os.remove(os.path.join(self.cache, fn))
                        removed_any = True
                    except OSError:
                        pass
            if removed_any:
                for cat_fn in (self.bin_name, self.hash_name):
                    cp = os.path.join(self.cache, cat_fn)
                    if os.path.exists(cp):
                        try:
                            os.remove(cp)
                        except OSError:
                            pass

        if remove_shortcut:
            try:
                lnk = os.path.join(_desktop_dir(), SHORTCUT_NAME)
                if os.path.exists(lnk):
                    os.remove(lnk)
            except OSError:
                pass

        if not quiet:
            self.log("Yama kaldırıldı, oyun İngilizceye döndü." if removed_any else "Kurulu yama bulunamadı.")
        return removed_any


def launch_game():
    """Oyunu Steam üzerinden başlatır."""
    try:
        os.startfile(f"steam://rungameid/{STEAM_APPID}")
    except Exception:
        webbrowser.open(f"steam://rungameid/{STEAM_APPID}")


def app_home():
    """Yamanın kalıcı kopyası: kısayol buraya bağlanır."""
    return os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "RobRunTurkce")


def make_shortcut():
    """Masaüstüne 'Rob Run Türkçe' kısayolu oluşturur."""
    import base64
    dst_dir = app_home()
    os.makedirs(dst_dir, exist_ok=True)

    if getattr(sys, "frozen", False):
        exe_name = os.path.basename(sys.executable)
        target_exe = os.path.join(dst_dir, exe_name)
        if os.path.normcase(os.path.abspath(sys.executable)) != os.path.normcase(os.path.abspath(target_exe)):
            shutil.copy2(sys.executable, target_exe)
        target_path = target_exe
        target_args = "--baslat"
        work_dir = dst_dir
    else:
        pyw = os.path.join(os.path.dirname(sys.executable), "pythonw.exe")
        if not os.path.exists(pyw):
            pyw = sys.executable
        target_path = pyw
        target_args = f'"{os.path.abspath(__file__)}" --baslat'
        work_dir = os.path.dirname(os.path.abspath(__file__))

    ico = os.path.join(find_game_dir(), "ROB RUN.exe")
    q = lambda p: p.replace("'", "''")
    ps = (
        "$d=[Environment]::GetFolderPath('Desktop');"
        f"$s=(New-Object -ComObject WScript.Shell).CreateShortcut((Join-Path $d '{SHORTCUT_NAME}'));"
        f"$s.TargetPath='{q(target_path)}';"
        f"$s.Arguments='{q(target_args)}';"
        f"$s.WorkingDirectory='{q(work_dir)}';"
        f"$s.IconLocation='{q(ico)},0';$s.Description='ROB RUN Türkçe Yama';$s.Save()"
    )
    enc = base64.b64encode(ps.encode("utf-16-le")).decode()
    r = subprocess.run(
        ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", enc],
        capture_output=True, text=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    if r.returncode != 0:
        raise RuntimeError("Kısayol oluşturulamadı: " + (r.stderr or "").strip()[:200])


def main():
    args = sys.argv[1:]
    if sys.stdout is None:
        sys.stdout = sys.stderr = open(LOG_PATH, "w", encoding="utf-8", buffering=1)
    if "--konsol" in args:
        p = Patcher(print)
        if "--kaldir" in args:
            p.uninstall()
        else:
            p.install()
        return
    from installer_ui import InstallerWindow
    InstallerWindow(args).run()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        with open(LOG_PATH, "a", encoding="utf-8") as f:
            f.write(traceback.format_exc())
        raise
