# SPDX-License-Identifier: AGPL-3.0-or-later
"""terrain2wt - turn any elevation model into Bitwig/Surge .wt wavetables.

Source (pick one)
  --dem FILE.tif            any GeoTIFF elevation model (drone DSM, national lidar, ...)
  --copernicus              fetch Copernicus GLO-30 (30 m, worldwide, free) for the region

Region (pick one)
  --bbox W S E N            lon/lat box
  --center LAT LON --size WIDTH_M HEIGHT_M [--angle DEG]
                            rotated rectangle in metres; angle = direction of the wave
                            (0 = waves run west->east, 90 = south->north)

Each frame is one straight line across the region in the wave direction; frames step
across the region perpendicular to it. One global scale per table, so real
relative heights survive.

Examples
  python terrain2wt.py --copernicus --bbox 25.03 39.78 25.45 40.03 --name Lemnos
  python terrain2wt.py --copernicus --center 36.40 25.40 --size 14000 14000 --name Santorini
  python terrain2wt.py --dem ../deliverable_1/1_orthophoto_dem/dsm.tif --center 39.9 25.2 --size 300 100 --angle 30 --name Beach

Copernicus DEM attribution: produced using Copernicus WorldDEM-30 (c) DLR e.V. 2010-2014 and
(c) Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA.
"""
import argparse, math, struct, shutil, urllib.request
from pathlib import Path
import numpy as np
import rasterio
from rasterio.merge import merge
from rasterio.warp import transform as warp_transform
from rasterio.windows import from_bounds
from rasterio.enums import Resampling
from scipy.ndimage import map_coordinates, median_filter, gaussian_filter

HERE = Path(__file__).parent
N = 2048
COP_URL = "https://copernicus-dem-30m.s3.amazonaws.com/{n}/{n}.tif"


# ---------------------------------------------------------------- data sources
def copernicus_tiles(w, s, e, n, cache):
    cache.mkdir(parents=True, exist_ok=True)
    paths = []
    for lat in range(math.floor(s), math.floor(n) + 1):
        for lon in range(math.floor(w), math.floor(e) + 1):
            name = (f"Copernicus_DSM_COG_10_{'N' if lat >= 0 else 'S'}{abs(lat):02d}_00_"
                    f"{'E' if lon >= 0 else 'W'}{abs(lon):03d}_00_DEM")
            p = cache / f"{name}.tif"
            if not p.exists():
                print("downloading", name)
                try:
                    urllib.request.urlretrieve(COP_URL.format(n=name), p)
                except Exception as ex:          # open-ocean tiles don't exist
                    print("  skipped:", ex); p.unlink(missing_ok=True); continue
            paths.append(p)
    if not paths:
        raise SystemExit("no Copernicus tiles for this region")
    return paths


def region_geometry(a):
    """Return centre lat/lon, width, height (m) and wave angle."""
    if a.bbox:
        w, s, e, n = a.bbox
        lat, lon = (s + n) / 2, (w + e) / 2
        width = (e - w) * 111320 * math.cos(math.radians(lat))
        height = (n - s) * 110540
        return lat, lon, width, height, a.angle
    lat, lon = a.center
    return lat, lon, a.size[0], a.size[1], a.angle


def sample_grid(a):
    lat, lon, width, height, angle = region_geometry(a)
    # a local metric frame (azimuthal equidistant) centred on the region
    aeqd = f"+proj=aeqd +lat_0={lat} +lon_0={lon} +datum=WGS84 +units=m"
    th = math.radians(angle)
    u = np.array([math.cos(th), math.sin(th)])         # along the wave
    v = np.array([-math.sin(th), math.cos(th)])        # frame-to-frame direction
    if a.copernicus:
        # lon/lat box that contains the rotated rectangle
        corners = np.array([[sx * width / 2, sy * height / 2] for sx in (-1, 1) for sy in (-1, 1)])
        cxy = corners @ np.vstack([u, v])
        clon, clat = warp_transform(aeqd, "EPSG:4326", cxy[:, 0].tolist(), cxy[:, 1].tolist())
        files = copernicus_tiles(min(clon), min(clat), max(clon), max(clat), HERE / "dem_cache")
    else:
        files = [Path(a.dem)]
    return lat, lon, width, height, aeqd, u, v, files


def load_dem(files, aeqd, xy_bounds, max_px=6000):
    srcs = [rasterio.open(f) for f in files]
    crs = srcs[0].crs
    xs, ys = warp_transform(aeqd, crs, *xy_bounds)
    bounds = (min(xs), min(ys), max(xs), max(ys))
    if len(srcs) > 1:
        arr, tr = merge(srcs, bounds=bounds)
        z = arr[0].astype(np.float32); nod = srcs[0].nodata
    else:
        s = srcs[0]
        win = from_bounds(*bounds, s.transform).round_offsets().round_lengths()
        scale = max(1, max(win.width, win.height) / max_px)
        shape = (max(2, int(win.height / scale)), max(2, int(win.width / scale)))
        z = s.read(1, window=win, out_shape=shape, boundless=True, fill_value=s.nodata or -9999,
                   resampling=Resampling.average).astype(np.float32)
        tr = s.window_transform(win) * rasterio.Affine.scale(win.width / shape[1], win.height / shape[0])
        nod = s.nodata
    if nod is not None:
        z[z == nod] = np.nan
    z[z < -1000] = np.nan
    for s in srcs:
        s.close()
    return z, tr, crs


# ---------------------------------------------------------------- wavetable
def build(a):
    lat, lon, width, height, aeqd, u, v, files = sample_grid(a)
    ss = np.linspace(-width / 2, width / 2, N)
    tt = np.linspace(-height / 2, height / 2, a.frames * 4)       # oversample, then pick frames
    X = ss[None, :] * u[0] + tt[:, None] * v[0]
    Y = ss[None, :] * u[1] + tt[:, None] * v[1]
    pad = 0.02 * max(width, height)
    corners = ([X.min() - pad, X.max() + pad, X.min() - pad, X.max() + pad],
               [Y.min() - pad, Y.min() - pad, Y.max() + pad, Y.max() + pad])
    z, tr, crs = load_dem(files, aeqd, corners)
    px, py = warp_transform(aeqd, crs, X.ravel().tolist(), Y.ravel().tolist())
    col, row = ~tr * (np.array(px), np.array(py))
    G = map_coordinates(z, [row - 0.5, col - 0.5], order=1, cval=np.nan, prefilter=False).reshape(X.shape)
    if np.isnan(G).all():
        raise SystemExit("region has no data in this DEM")
    G = np.where(np.isnan(G), np.nanmin(G), G)
    sea = None
    if a.floor is not None:
        sea = G <= a.floor
        G = np.maximum(G, a.floor)
    if a.despike:
        G = median_filter(G, a.despike)
    if a.smooth:
        G = gaussian_filter(G, a.smooth)
    pick = np.linspace(0, len(G) - 1, a.frames).astype(int)
    F = G[pick]
    sea = sea[pick] if sea is not None else None
    if a.reverse:
        F = F[::-1]; sea = sea[::-1] if sea is not None else None
    print(f"{a.name}: {width/1000:.2f} x {height/1000:.2f} km, height {F.min():.1f} .. {F.max():.1f} m, "
          f"{width/N:.1f} m/sample")
    return F, sea, (lat, lon, width, height, u, v)


def shape_heights(F, sea, gamma, land_step):
    """0..1 relief: sea = 0, land starts at land_step, heights compressed by gamma."""
    lo = F[~sea].min() if sea is not None and (~sea).any() else F.min()
    h = np.clip((F - lo) / max(1e-9, F.max() - lo), 0, 1) ** gamma
    if sea is None:
        return h
    return np.where(sea, 0.0, land_step + (1 - land_step) * h)


def write_wt(path, F):
    pcm = np.round(np.clip(F, -1, 1) * 0.98 * 32767).astype("<i2").tobytes()
    path.write_bytes(b"vawt" + struct.pack("<IHH", N, len(F), 0x0C) + pcm)


def preview(path, F, title):
    import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(10, 6), facecolor="#1e1e1e"); ax.set_facecolor("#1e1e1e")
    nf = len(F); x = np.linspace(0, 1, N); step = max(1, nf // 64)
    for i in range(nf - 1, -1, -step):
        dx, dy = 0.35 * i / nf, 1.6 * i / nf
        ax.fill_between(x + dx, -1.2 + dy, F[i] * 0.5 + dy, color="#1e1e1e", lw=0)
        ax.plot(x + dx, F[i] * 0.5 + dy, color=plt.cm.YlOrBr(0.35 + 0.6 * (1 - i / nf)), lw=0.7)
    ax.set_title(title, color="w"); ax.axis("off")
    fig.savefig(path, dpi=80, bbox_inches="tight"); plt.close(fig)


def map_preview(path, R, title):
    """Top-down view of the table: frame 0 at the bottom. Should look like the map."""
    import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(10, 10 * len(R) / N * 4 if len(R) < N / 4 else 6))
    ax.imshow(R, cmap="gist_earth", origin="lower", aspect="auto", vmin=-0.05, vmax=1.05,
              interpolation="nearest")
    ax.set_title(f"{title} - top view (rows = frames)"); ax.set_xticks([]); ax.set_ylabel("frame")
    fig.savefig(path, dpi=80, bbox_inches="tight"); plt.close(fig)


def bitwig_library():
    home = Path.home()
    for d in (home / "OneDrive/Documents/Bitwig Studio/Library", home / "Documents/Bitwig Studio/Library",
              home / "Documents/Bitwig Studio/Library"):
        if d.exists():
            return d / "Wavetables"
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--dem"); src.add_argument("--copernicus", action="store_true")
    reg = p.add_mutually_exclusive_group(required=True)
    reg.add_argument("--bbox", nargs=4, type=float, metavar=("W", "S", "E", "N"))
    reg.add_argument("--center", nargs=2, type=float, metavar=("LAT", "LON"))
    p.add_argument("--size", nargs=2, type=float, metavar=("WIDTH_M", "HEIGHT_M"))
    p.add_argument("--angle", type=float, default=0, help="wave direction, degrees from east, counter-clockwise")
    p.add_argument("--frames", type=int, default=64)
    p.add_argument("--floor", type=float, help="clip everything below this height (e.g. 0 = flat sea)")
    p.add_argument("--despike", type=int, default=3, help="median filter size, 0 = off")
    p.add_argument("--smooth", type=float, default=0.7, help="gaussian smoothing (samples), 0 = off")
    p.add_argument("--reverse", action="store_true", help="reverse frame order")
    p.add_argument("--gamma", type=float, default=0.5,
                   help="height curve: 1 = linear, <1 lifts lowlands (0.5 = square root)")
    p.add_argument("--land-step", type=float, default=0.2,
                   help="with --floor: land starts this far (0..1) above the sea, so the coastline shows")
    p.add_argument("--scale", choices=["abs", "ctr", "both"], default="both")
    p.add_argument("--name", required=True)
    p.add_argument("--out", default=str(HERE / "terrain_tables"))
    p.add_argument("--bitwig", action="store_true", help="also copy into the Bitwig user library")
    a = p.parse_args()
    if a.center and not a.size:
        p.error("--center needs --size")

    F, sea, geom = build(a)
    R = shape_heights(F, sea, a.gamma, a.land_step)     # 0..1 relief
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    tables = {}
    if a.scale in ("abs", "both"):        # sea/lowest = -1, highest = +1: looks like the terrain
        tables["abs"] = R * 2 - 1
    if a.scale in ("ctr", "both"):        # same shape, every frame centred: no DC offset
        C = R - R.mean(axis=1, keepdims=True)
        tables["ctr"] = C / max(1e-9, np.abs(C).max())
    map_preview(out / f"{a.name}_map_{a.frames}.png", R, a.name)
    lib = bitwig_library() if a.bitwig else None
    for k, T in tables.items():
        name = f"{a.name}_{k}_{a.frames}"
        write_wt(out / f"{name}.wt", T)
        preview(out / f"{name}.png", T, name)
        print("wrote", out / f"{name}.wt")
        if lib:
            (lib / a.name).mkdir(parents=True, exist_ok=True)
            shutil.copy(out / f"{name}.wt", lib / a.name / f"{name}.wt")
    if lib:
        print("copied to", lib / a.name)


if __name__ == "__main__":
    main()
