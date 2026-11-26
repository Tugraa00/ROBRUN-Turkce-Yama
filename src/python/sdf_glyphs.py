"""
ROB RUN Türkçe yama - font yaması
TMP SDF fontlarına eksik Türkçe harfleri (ç Ç ö Ö ü Ü ş Ş ğ Ğ ı İ) fontun
kendi gliflerinden birleştirerek ekler. ğ'nin kavisi (breve) çizilerek üretilir.
"""
import numpy as np
from PIL import Image

TARGET = "çÇöÖüÜşŞğĞıİ"
# Hedef font -> eksik aksanları ödünç alacağı aynı aileden font
DONOR = {
    "TT_Bluescreens SDF Outline": "TT_Bluescreens SDF",
    "RobotoBoldCondensedOutline": "RobotoBoldCondensed",
}
SKIP = {"IconsFont"}


class Part:
    """SDF parçası. a[0] = en alt satır. (x0, y0) = a[0,0] pikselinin glif uzayındaki konumu."""

    def __init__(self, a, x0, y0):
        self.a = a.astype(np.float32)
        self.x0 = x0
        self.y0 = y0

    def copy(self):
        return Part(self.a.copy(), self.x0, self.y0)

    def keep_y(self, lo=-1e9, hi=1e9):
        p = self.copy()
        ys = p.y0 + np.arange(p.a.shape[0])
        p.a[(ys < lo) | (ys >= hi)] = 0
        return p

    def shift(self, dx, dy):
        p = self.copy()
        p.x0 += dx
        p.y0 += dy
        return p

    def ink_bbox(self):
        ys, xs = np.nonzero(self.a >= 128)
        if len(xs) == 0:
            return None
        return (self.x0 + xs.min(), self.x0 + xs.max() + 1, self.y0 + ys.min(), self.y0 + ys.max() + 1)

    def ink_rows(self):
        return self.y0 + np.nonzero((self.a >= 128).any(axis=1))[0]


def union(parts):
    """Birden fazla SDF parçasını maksimum değerle birleştirir."""
    x0 = min(p.x0 for p in parts)
    y0 = min(p.y0 for p in parts)
    x1 = max(p.x0 + p.a.shape[1] for p in parts)
    y1 = max(p.y0 + p.a.shape[0] for p in parts)
    c = np.zeros((y1 - y0, x1 - x0), np.float32)
    for p in parts:
        oy, ox = p.y0 - y0, p.x0 - x0
        h, w = p.a.shape
        np.maximum(c[oy:oy + h, ox:ox + w], p.a, out=c[oy:oy + h, ox:ox + w])
    return Part(c, x0, y0)


def gap_cut(part):
    """Üstteki ayrık parça (nokta/iki nokta) ile gövde arasındaki boşluğun ortasını bulur."""
    rows = np.sort(part.ink_rows())[::-1]
    for i in range(len(rows) - 1):
        if rows[i] - rows[i + 1] > 1:
            return (rows[i] + rows[i + 1] + 1) / 2
    return None


def slope(part):
    """SDF değerinin piksel başına değişimi (kenar çevresindeki eğim)."""
    gy, gx = np.gradient(part.a)
    g = np.hypot(gx, gy)
    sel = (part.a > 90) & (part.a < 166)
    vals = g[sel]
    return float(np.median(vals)) if len(vals) else 1.0


class Font:
    """TMP Font Asset sarmalayıcısı."""

    def __init__(self, tree, atlas_bu):
        self.t = tree
        self.atlas = atlas_bu  # atlas_bu: alttan-üste satırlar
        self.p = tree["m_AtlasPadding"]
        self.face = tree["m_FaceInfo"]
        self.glyphs = {g["m_Index"]: g for g in tree["m_GlyphTable"]}
        self.cmap = {c["m_Unicode"]: c["m_GlyphIndex"] for c in tree["m_CharacterTable"]}
        self._k = None
        self.grown = False

    def has(self, ch):
        return ord(ch) in self.cmap and self.cmap[ord(ch)] in self.glyphs

    def glyph(self, ch):
        return self.glyphs[self.cmap[ord(ch)]]

    def part(self, ch):
        g = self.glyph(ch)
        r, m, p = g["m_GlyphRect"], g["m_Metrics"], self.p
        y0_r = max(0, r["m_Y"] - p)
        y1_r = min(self.atlas.shape[0], r["m_Y"] + r["m_Height"] + p)
        x0_r = max(0, r["m_X"] - p)
        x1_r = min(self.atlas.shape[1], r["m_X"] + r["m_Width"] + p)
        a = self.atlas[y0_r:y1_r, x0_r:x1_r]
        x0 = round(m["m_HorizontalBearingX"]) - p
        y0 = round(m["m_HorizontalBearingY"]) - r["m_Height"] - p
        return Part(a, x0, y0)

    @property
    def k(self):
        if self._k is None:
            ch = "I" if self.has("I") else ("l" if self.has("l") else None)
            self._k = slope(self.part(ch)) if ch else 1.0
        return self._k

    def center(self, ch):
        b = self.part(ch).ink_bbox()
        return (b[0] + b[1]) / 2 if b else 0.0


def scaled(part, s, k_from, k_to):
    """SDF parçasını ölçekler ve kenar eğimini hedef fonta uyarlar."""
    h, w = part.a.shape
    nw, nh = max(1, round(w * s)), max(1, round(h * s))
    img = Image.fromarray(part.a).resize((nw, nh), Image.BILINEAR)
    a = np.asarray(img, np.float32)
    a = np.clip(128 + (a - 128) * (k_to * s / k_from), 0, 255)
    return Part(a, round(part.x0 * s), round(part.y0 * s))


def accents(src, dst):
    """src fontundan aksan parçalarını çıkarıp dst ölçeğine getirir (src==dst olabilir)."""
    s = dst.face["m_PointSize"] / src.face["m_PointSize"]
    conv = (lambda q: q) if src is dst else (lambda q: scaled(q, s, src.k, dst.k))
    acc = {}
    for name, acc_ch in (("dots", "ü"), ("DOTS", "Ü")):
        if src.has(acc_ch):
            pa = src.part(acc_ch)
            cut = gap_cut(pa)
            if cut is not None:
                acc[name] = (conv(pa.keep_y(lo=cut)), src.center(acc_ch) * s)
    for name, acc_ch, base in (("ced", "ç", "c"), ("CED", "Ç", "C")):
        if src.has(acc_ch) and src.has(base):
            g = src.glyph(base)["m_Metrics"]
            bottom = g["m_HorizontalBearingY"] - g["m_Height"]
            acc[name] = (conv(src.part(acc_ch).keep_y(hi=bottom)), src.center(acc_ch) * s)
    return acc


def place(acc_part, acc_center, x_center, dy=0):
    return acc_part.shift(round(x_center - acc_center), round(dy))


def breve(dst, band, cx, thick_ratio):
    """band = iki noktanın kaplama kutusu (x0,x1,y0,y1). Bu kutuya oturan kavis çizer."""
    x0, x1, y0, y1 = band
    bw, bh = (x1 - x0) * 0.85, (y1 - y0) * 1.25
    t = max(1.5, (y1 - y0) * thick_ratio)
    a, b = (bw - t) / 2, bh - t
    yc = y0 + bh - t / 2 - (bh - (y1 - y0)) / 2
    th = np.linspace(np.pi, 2 * np.pi, 400)
    px, py = cx + a * np.cos(th), yc + b * np.sin(th)
    pad = dst.p + 2
    gx0, gy0 = int(np.floor(cx - bw / 2)) - pad, int(np.floor(yc - b - t)) - pad
    W, H = int(bw) + 2 * pad + 2, int(bh) + 2 * pad + 2
    ys, xs = np.mgrid[0:H, 0:W]
    X, Y = gx0 + xs + 0.5, gy0 + ys + 0.5
    d = np.full(X.shape, 1e9, np.float32)
    for i in range(0, 400, 50):
        dd = np.sqrt((X[..., None] - px[i:i + 50]) ** 2 + (Y[..., None] - py[i:i + 50]) ** 2).min(-1)
        d = np.minimum(d, dd)
    v = np.clip(128 - dst.k * (d - t / 2), 0, 255)
    return Part(v, gx0, gy0)


def build_glyphs(dst, donor):
    """Eklenecek her harf için (Part, advance) sözlüğü döndürür."""
    own = accents(dst, dst)
    don = accents(donor, dst) if donor else {}
    acc = {**don, **own}
    out = {}
    face = dst.face
    lift = face["m_CapLine"] - face["m_MeanLine"]

    def dots_for(upper):
        if upper and "DOTS" in acc:
            return acc["DOTS"]
        if "dots" in acc:
            p, c = acc["dots"]
            return (p.shift(0, round(lift * 0.85)), c) if upper else (p, c)
        return None

    base_map = {
        "ç": "c", "Ç": "C", "ö": "o", "Ö": "O", "ü": "u", "Ü": "U",
        "ş": "s", "Ş": "S", "ğ": "g", "Ğ": "G", "ı": "i", "İ": "I",
    }
    for ch in TARGET:
        if dst.has(ch):
            continue
        upper = ch.isupper()
        base = base_map[ch]
        if not dst.has(base):
            continue
        bp = dst.part(base)
        adv = dst.glyph(base)["m_Metrics"]["m_HorizontalAdvance"]
        cx = dst.center(base)
        g = None
        if ch in "çÇşŞ":
            key = "CED" if upper and "CED" in acc else "ced"
            if key not in acc:
                continue
            p, c = acc[key]
            g = union([bp, place(p, c, cx)])
        elif ch in "öÖüÜ":
            d = dots_for(upper)
            if not d:
                continue
            g = union([bp, place(d[0], d[1], cx)])
        elif ch in "ğĞ":
            d = dots_for(upper)
            if not d:
                continue
            band = place(d[0], d[1], cx).ink_bbox()
            if not band:
                continue
            if upper:
                bp_box = bp.ink_bbox()
                if bp_box and band[2] - bp_box[3] < 2:
                    band = (band[0], band[1], band[2] + 2, band[3] + 2)
            thick = 0.38 if "Libertinus" in dst.t["m_Name"] else 0.5
            g = union([bp, breve(dst, band, cx, thick)])
        elif ch == "ı":
            ip = dst.part("i")
            cut = gap_cut(ip)
            if cut is None:
                continue
            g = ip.keep_y(hi=cut)
        elif ch == "İ":
            ip = dst.part("i")
            cut = gap_cut(ip)
            if cut is None:
                continue
            dot = ip.keep_y(lo=cut)
            g = union([bp, place(dot, dst.center("i"), cx, lift)])
        if g is not None:
            out[ch] = (g, adv)
    return out


def subtract(free, u):
    """Kullanılan dikdörtgeni serbest atlas dikdörtgenlerinden çıkarır."""
    res = []
    ux0, uy0, ux1, uy1 = u
    for (x, y, w, h) in free:
        x1, y1 = x + w, y + h
        if ux0 >= x1 or ux1 <= x or uy0 >= y1 or uy1 <= y:
            res.append((x, y, w, h))
            continue
        if ux0 > x:
            res.append((x, y, ux0 - x, h))
        if ux1 < x1:
            res.append((ux1, y, x1 - ux1, h))
        if uy0 > y:
            res.append((x, y, w, uy0 - y))
        if uy1 < y1:
            res.append((x, uy1, w, y1 - uy1))
    res = [r for r in res if r[2] > 0 and r[3] > 0]
    pruned = []
    for i, a in enumerate(res):
        if not any(
            j != i
            and b[0] <= a[0]
            and b[1] <= a[1]
            and b[0] + b[2] >= a[0] + a[2]
            and b[1] + b[3] >= a[1] + a[3]
            and (b != a or j < i)
            for j, b in enumerate(res)
        ):
            pruned.append(a)
    return pruned


def grow(dst):
    """Atlası yukarı doğru iki katına çıkarır; mevcut glif koordinatları değişmez."""
    H, W = dst.atlas.shape
    dst.atlas = np.vstack([dst.atlas, np.zeros((H, W), np.uint8)])
    dst.t["m_AtlasHeight"] = 2 * H
    dst.grown = True


def find_space(occ, bw, bh):
    """Atlas üzerinde bw x bh boyutunda tamamen boş bir alan bulur (1 px emniyet payıyla)."""
    m = occ.copy()
    m[1:] |= occ[:-1]
    m[:-1] |= occ[1:]
    m[:, 1:] |= occ[:, :-1]
    m[:, :-1] |= occ[:, 1:]
    S = np.pad(m.astype(np.int32).cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    H, W = m.shape
    if bh > H or bw > W:
        return None
    tot = S[bh:, bw:] - S[:-bh, bw:] - S[bh:, :-bw] + S[:-bh, :-bw]
    ys, xs = np.nonzero(tot == 0)
    if len(ys) == 0:
        return None
    i = np.lexsort((xs, ys))[0]
    return int(xs[i]), int(ys[i])


def add_to_font(dst, new):
    """Üretilen glifleri font atlasına ve tablolarına ekler."""
    t, p = dst.t, dst.p
    if not t.get("m_GlyphTable") or not t.get("m_CharacterTable"):
        return []
    free = [(r["m_X"], r["m_Y"], r["m_Width"], r["m_Height"]) for r in t.get("m_FreeGlyphRects", [])]
    next_idx = max(dst.glyphs) + 1
    tmpl = dict(t["m_GlyphTable"][0])
    occ = np.zeros(dst.atlas.shape, bool)
    for r in t.get("m_UsedGlyphRects", []):
        occ[r["m_Y"]:r["m_Y"] + r["m_Height"], r["m_X"]:r["m_X"] + r["m_Width"]] = True
    occ |= dst.atlas > 0

    added = []
    for ch, (g, adv) in sorted(new.items(), key=lambda kv: -kv[1][0].a.size):
        b = g.ink_bbox()
        if not b:
            continue
        W, H = b[1] - b[0], b[3] - b[2]
        bw, bh = W + 2 * p, H + 2 * p
        spot = find_space(occ, bw, bh)
        if spot is None and dst.atlas.shape[0] < 4096:
            grow(dst)
            occ = np.vstack([occ, np.zeros_like(occ)])
            spot = find_space(occ, bw, bh)
        if spot is None:
            print(f"    ! {t['m_Name']}: '{ch}' için atlasta yer yok")
            continue
        fx, fy = spot
        occ[fy:fy + bh, fx:fx + bw] = True
        canvas = union([g, Part(np.zeros((bh, bw)), b[0] - p, b[2] - p)])
        oy, ox = (b[2] - p) - canvas.y0, (b[0] - p) - canvas.x0
        dst.atlas[fy:fy + bh, fx:fx + bw] = np.clip(canvas.a[oy:oy + bh, ox:ox + bw], 0, 255).astype(np.uint8)
        free = subtract(free, (fx, fy, fx + bw, fy + bh))

        glyph = dict(tmpl)
        glyph.update({
            "m_Index": next_idx,
            "m_Metrics": {
                "m_Width": float(W),
                "m_Height": float(H),
                "m_HorizontalBearingX": float(b[0]),
                "m_HorizontalBearingY": float(b[3]),
                "m_HorizontalAdvance": float(adv),
            },
            "m_GlyphRect": {"m_X": fx + p, "m_Y": fy + p, "m_Width": W, "m_Height": H},
            "m_Scale": 1.0,
            "m_AtlasIndex": 0,
        })
        t["m_GlyphTable"].append(glyph)
        t["m_CharacterTable"].append({**t["m_CharacterTable"][0], "m_Unicode": ord(ch), "m_GlyphIndex": next_idx})
        t["m_UsedGlyphRects"].append({"m_X": fx, "m_Y": fy, "m_Width": bw, "m_Height": bh})
        dst.glyphs[next_idx] = glyph
        dst.cmap[ord(ch)] = next_idx
        next_idx += 1
        added.append(ch)

    t["m_FreeGlyphRects"] = [{"m_X": x, "m_Y": y, "m_Width": w, "m_Height": h} for x, y, w, h in free]
    return added


def patch_font_bundle(env, log=print, preview_dir=None):
    """Bundle içindeki tüm TMP SDF fontlarına Türkçe karakterleri ekler."""
    fonts = {}
    for o in env.objects:
        if o.type.name != "MonoBehaviour":
            continue
        try:
            t = o.read_typetree()
        except Exception:
            continue
        if "m_GlyphTable" in t and "m_CharacterTable" in t:
            fonts[t["m_Name"]] = (o, t)

    objs = {o.path_id: o for o in env.objects}
    loaded = {}
    for name, (o, t) in fonts.items():
        atlas_refs = t.get("m_AtlasTextures") or []
        if not atlas_refs:
            continue
        tex_obj = objs.get(atlas_refs[0]["m_PathID"])
        if not tex_obj:
            continue
        tex = tex_obj.read()
        a = np.flipud(np.asarray(tex.image.convert("RGBA"))[..., 3]).copy()
        loaded[name] = (o, Font(t, a), tex)

    total = 0
    for name, (o, f, tex) in loaded.items():
        if name in SKIP:
            continue
        donor = loaded[DONOR[name]][1] if name in DONOR and DONOR[name] in loaded else None
        new = build_glyphs(f, donor)
        added = add_to_font(f, new)
        if not added:
            continue
        o.save_typetree(f.t)
        if f.grown:
            tex_id = f.t["m_AtlasTextures"][0]["m_PathID"]
            mat_id = f.t.get("m_Material", {}).get("m_PathID")
            for mo in env.objects:
                if mo.type.name != "Material":
                    continue
                mt = mo.read_typetree()
                props = mt["m_SavedProperties"]
                uses = any(k == "_MainTex" and v["m_Texture"]["m_PathID"] == tex_id for k, v in props["m_TexEnvs"])
                if uses or mo.path_id == mat_id:
                    props["m_Floats"] = [
                        (k, float(f.atlas.shape[0]) if k == "_TextureHeight" else v)
                        for k, v in props["m_Floats"]
                    ]
                    mo.save_typetree(mt)

        a = np.flipud(f.atlas)
        rgba = np.dstack([np.full_like(a, 255)] * 3 + [a])
        tex.set_image(Image.fromarray(rgba, "RGBA"), target_format=tex.m_TextureFormat)
        tex.save()
        total += len(added)
        log(f"    {name}: {''.join(added)}")
        if preview_dir:
            preview(f, added, f"{preview_dir}/{name}.png")
    return total


def preview(f, chars, path):
    """Test: 'base + yeni' harfleri SDF eşiğiyle çizip yan yana koyar."""
    tiles = []
    for ch in "sçuüoöIiİıgğGĞSŞCÇOÖUÜ":
        if not f.has(ch):
            continue
        pa = f.part(ch)
        img = (pa.a >= 128).astype(np.uint8) * 255
        tiles.append((np.flipud(img), pa))
    if not tiles:
        return
    asc = max(p.y0 + p.a.shape[0] for _, p in tiles)
    desc = min(p.y0 for _, p in tiles)
    H = asc - desc + 4
    row = np.zeros((H, sum(t.shape[1] + 6 for t, _ in tiles)), np.uint8)
    x = 0
    for img, p in tiles:
        top = asc - (p.y0 + p.a.shape[0]) + 2
        row[top:top + img.shape[0], x:x + img.shape[1]] = img
        row[asc + 2, x:x + img.shape[1]] = 90
        x += img.shape[1] + 6
    Image.fromarray(row).save(path)
