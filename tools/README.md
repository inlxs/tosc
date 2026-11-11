# Command-line tools

`terrain2wt.py` turns any elevation model into Bitwig/Surge `.wt` wavetables, without the app.

    pip install -r requirements.txt

    # anywhere on Earth, Copernicus GLO-30 (30 m, downloaded automatically)
    python terrain2wt.py --copernicus --bbox 25.03 39.78 25.45 40.03 --floor 0 --name Lemnos --bitwig

    # your own GeoTIFF: drone DSM (e.g. OpenDroneMap dsm.tif), national lidar, ...
    python terrain2wt.py --dem dsm.tif --center 39.8902 25.3593 --size 450 120 --angle -27 --floor -2.4 --name Beach

Options: --frames, --angle (wave direction), --floor (flat sea), --gamma (height curve),
--land-step (coast step), --smooth, --despike, --scale abs|ctr|both, --bitwig (copy to the Bitwig library).
Each run also writes a top-down map preview and a Bitwig-style 3D preview PNG.
