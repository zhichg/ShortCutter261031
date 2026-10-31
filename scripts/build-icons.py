"""Package the selected SC artwork into Windows multi-size icon resources.

Requires Pillow. Run from any directory; all inputs/outputs stay in the project.
The disabled artwork was generated with image_gen from the selected color icon.
"""

from pathlib import Path

from PIL import Image


PROJECT = Path(__file__).resolve().parents[1]
ARTWORK = PROJECT / "assets" / "icons"
SIZES = [(size, size) for size in (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)]


def main():
    for source, destination in (
        ("03-sc-monogram.png", "app.ico"),
        ("03-sc-monogram-disabled.png", "app_gray.ico"),
    ):
        with Image.open(ARTWORK / source) as image:
            image.convert("RGBA").save(
                PROJECT / "src" / destination, format="ICO", sizes=SIZES
            )
        with Image.open(PROJECT / "src" / destination) as icon:
            actual = icon.ico.sizes()
            if actual != set(SIZES):
                raise RuntimeError(f"Unexpected icon sizes in {destination}: {actual}")
        print(f"Packaged src/{destination}: {len(SIZES)} sizes, 16-256 px")


if __name__ == "__main__":
    main()
