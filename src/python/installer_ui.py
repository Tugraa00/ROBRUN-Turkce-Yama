"""Kurulum ve kaldırma arayüzü — by Tuğra A."""
import os
import queue
import threading
import traceback
import tkinter as tk
from tkinter import ttk

import patch_engine

BG, PANEL, FG, MUTED = "#0e0f14", "#171922", "#f2f2f5", "#8b8fa3"
ACCENT, RED = "#ffb21e", "#ff5a5a"


class InstallerWindow:
    """ROB RUN Türkçe yama kurulum ve kaldırma penceresi."""

    def __init__(self, args):
        self.args = args
        self.q = queue.Queue()
        self.patcher = None

        r = self.root = tk.Tk()
        r.title("ROB RUN Türkçe Yama")
        r.configure(bg=BG)
        r.resizable(False, False)
        w, h = 500, 310
        r.geometry(f"{w}x{h}+{(r.winfo_screenwidth() - w) // 2}+{(r.winfo_screenheight() - h) // 2}")

        tk.Label(r, text="ROB RUN", font=("Segoe UI Black", 30), fg=ACCENT, bg=BG).pack(pady=(24, 0))
        tk.Label(r, text="TÜRKÇE YAMA", font=("Segoe UI", 13, "bold"), fg=FG, bg=BG).pack()
        tk.Label(r, text="by Tuğra A.", font=("Segoe UI", 10, "italic"), fg=MUTED, bg=BG).pack(pady=(2, 14))

        self.status = tk.Label(r, text="Hazırlanıyor…", font=("Segoe UI", 10), fg=FG, bg=BG, wraplength=460)
        self.status.pack()

        st = ttk.Style(r)
        st.theme_use("clam")
        st.configure(
            "c.Horizontal.TProgressbar",
            troughcolor=PANEL,
            background=ACCENT,
            bordercolor=PANEL,
            lightcolor=ACCENT,
            darkcolor=ACCENT,
        )
        self.bar = ttk.Progressbar(
            r, length=400, mode="determinate", maximum=1.0, style="c.Horizontal.TProgressbar"
        )
        self.bar.pack(pady=(10, 18))

        row = tk.Frame(r, bg=BG)
        row.pack(pady=10)
        self.play = tk.Button(
            row,
            text="▶  Oyunu Başlat",
            font=("Segoe UI", 11, "bold"),
            fg="#111",
            bg=ACCENT,
            activebackground="#e09a10",
            relief="flat",
            padx=16,
            pady=4,
            cursor="hand2",
            state="disabled",
            command=self.start_game,
        )
        self.play.pack(side="left", padx=6)

        self.remove = tk.Button(
            row,
            text="Yamayı Kaldır",
            font=("Segoe UI", 9),
            fg=MUTED,
            bg=BG,
            activebackground=BG,
            relief="flat",
            cursor="hand2",
            state="disabled",
            command=self.do_uninstall,
        )
        self.remove.pack(side="left", padx=6)

        self.log_btn = tk.Button(
            row,
            text="Logu Aç",
            font=("Segoe UI", 9),
            fg=MUTED,
            bg=BG,
            activebackground=BG,
            relief="flat",
            cursor="hand2",
            command=self.open_log,
        )

    def worker(self, job):
        def run():
            try:
                self.patcher = self.patcher or patch_engine.Patcher(lambda s: self.q.put(("status", s)))
                job()
                self.q.put(("done", None))
            except Exception as e:
                traceback.print_exc()
                self.q.put(("error", str(e) or e.__class__.__name__))

        threading.Thread(target=run, daemon=True).start()

    def job_install(self):
        p = self.patcher
        if p.up_to_date():
            self.q.put(("status", "Yama zaten kurulu ve güncel ✓"))
            self.q.put(("progress", 1.0))
            return
        self.q.put(("status", "Oyun güncellenmiş, yama yenileniyor…" if p.installed() else "Türkçe yama kuruluyor…"))
        p.install(progress=lambda v: self.q.put(("progress", v)))
        self.q.put(("status", "Kuruldu! İyi oyunlar ✓"))
        try:
            patch_engine.make_shortcut()
            self.q.put(("status", "Kuruldu! Masaüstüne 'Rob Run Türkçe' kısayolu eklendi ✓"))
        except Exception:
            traceback.print_exc()
            self.q.put(("status", "Yama kuruldu ✓ (kısayol oluşturulamadı, oyunu Steam'den açabilirsin)"))

    def job_uninstall(self):
        removed = self.patcher.uninstall()
        self.q.put(("status", "Yama kaldırıldı, oyun İngilizceye döndü." if removed else "Kurulu yama bulunamadı."))
        self.q.put(("progress", 0.0))
        self.q.put(("removed", None))

    def poll(self):
        try:
            while True:
                kind, val = self.q.get_nowait()
                if kind == "status":
                    self.status.config(text=val, fg=FG)
                elif kind == "progress":
                    self.bar["value"] = val
                elif kind == "error":
                    self.status.config(text=f"Hata: {val}", fg=RED)
                    self.remove.config(state="normal")
                    if os.path.exists(patch_engine.LOG_PATH):
                        self.log_btn.pack(side="left", padx=6)
                elif kind == "removed":
                    self.play.config(state="normal")
                elif kind == "done":
                    self.play.config(state="normal")
                    self.remove.config(state="normal")
                    if "--baslat" in self.args and self.bar["value"] >= 1.0:
                        self.root.after(1200, self.start_game)
        except queue.Empty:
            pass
        self.root.after(80, self.poll)

    def open_log(self):
        if os.path.exists(patch_engine.LOG_PATH):
            os.startfile(patch_engine.LOG_PATH)

    def start_game(self):
        patch_engine.launch_game()
        self.status.config(text="Oyun açılıyor…")
        self.root.after(1500, self.root.destroy)

    def do_uninstall(self):
        self.play.config(state="disabled")
        self.remove.config(state="disabled")
        self.worker(self.job_uninstall)

    def run(self):
        self.worker(self.job_uninstall if "--kaldir" in self.args else self.job_install)
        self.poll()
        self.root.mainloop()
