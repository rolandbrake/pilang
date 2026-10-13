import os
from PIL import Image, ImageSequence

# Compression settings
SCALE = 0.7       # Resize to 70% of original width and height
COLORS = 128      # Reduce colors (maximum 256)


def compress_gif(input_path, output_path):
    try:
        with Image.open(input_path) as gif:
            frames = []
            durations = [
                frame.info.get("duration", gif.info.get("duration", 100))
                for frame in ImageSequence.Iterator(gif)
            ]

            loop = gif.info.get("loop", 0)

            for frame in ImageSequence.Iterator(gif):
                frame = frame.convert("RGBA")

                # Resize
                width = max(1, int(frame.width * SCALE))
                height = max(1, int(frame.height * SCALE))
                frame = frame.resize(
                    (width, height),
                    Image.Resampling.LANCZOS
                )

                # Reduce colors
                frame = frame.convert("RGB").quantize(colors=COLORS)
                frames.append(frame)

            frames[0].save(
                output_path,
                save_all=True,
                append_images=frames[1:],
                duration=durations,
                loop=loop,
                optimize=True,
                disposal=2
            )

        original_size = os.path.getsize(input_path)
        reduced_size = os.path.getsize(output_path)

        # Keep the output only if it is smaller
        if reduced_size < original_size:
            reduction = (1 - reduced_size / original_size) * 100
            print(
                f"[OK] {os.path.basename(input_path)}: "
                f"{original_size / 1024:.1f} KB -> "
                f"{reduced_size / 1024:.1f} KB "
                f"({reduction:.1f}% smaller)"
            )
        else:
            os.remove(output_path)
            print(
                f"[SKIP] {os.path.basename(input_path)}: "
                "compression did not reduce the size"
            )

    except Exception as e:
        if os.path.exists(output_path):
            os.remove(output_path)
        print(f"[ERROR] {os.path.basename(input_path)}: {e}")


def main():
    directory = os.path.dirname(os.path.abspath(__file__))
    count = 0

    print("Scanning directory for GIF files...\n")

    for filename in os.listdir(directory):
        if not filename.lower().endswith(".gif"):
            continue

        # Ignore previously compressed files
        if filename.lower().endswith("_reduced.gif"):
            continue

        input_path = os.path.join(directory, filename)
        output_path = os.path.join(
            directory,
            os.path.splitext(filename)[0] + "_reduced.gif"
        )

        compress_gif(input_path, output_path)
        count += 1

    print(f"\nFinished! Processed {count} GIF file(s).")


if __name__ == "__main__":
    main()