# Win32s Retro Image Viewer

A high-performance, lightweight image viewer specifically engineered for **Windows 3.1x (via Win32s)** and **Windows NT 3.10**. This project bypasses modern GDI overhead to provide a smooth experience on **Intel 486SX** and early Pentium hardware.

## 🚀 Key Features

* **Fixed-Point Dithering:** Custom **Floyd-Steinberg** algorithm with [Amanvir Parhar weights](https://amanvir.com/blog/writing-my-own-dithering-algorithm-in-racket) optimized for processors without an FPU (Floating Point Unit). 
* **Color Cube LUT:** Uses a pre-computed 32x32x32 Look-Up Table to map colors instantly to VGA (16) or Win8-based (256) palettes, avoiding expensive distance calculations.
* **Custom Generic 256 Color Palette:** Based on ["win8" palette](https://web.archive.org/web/20250227072942/https://eisbox.net/downloads/palettes/win-8.txt) with some modifications on both palette and `FindClosestColor` function.
* **Universal Format Support:**
    * **Modern:** QOI (Quite OK Image), WebP, AVIF (not all features are supported), JPEG XL, JPG, PNG, GIF (without animation), JBIG2 (first page only), BMP, Y4M (including monochrome `Cmono`/`C400`).
    * **Retro/Unix:** PCX, TGA, TIFF (Classic TIFF, BigTIFF and some encodings are not supported), MAG, PIC2 (.p2), MSP, PBM (P4 binary), PNM, PGM, PPM, PAM (P7, 8-bit up to RGBA), JBIG, XBM (X-BitMap) and XPM (X-PixMap).
* **Explicit Fit-to-Window:** Press `-` for a one-shot aspect-preserving shrink-to-fit (shrink only, never upscale); `0` returns to 100%. Nothing re-renders on window resize afterwards, keeping 486-class machines responsive.
* **Pristine + View Pipeline:** The decoded image is kept untouched in memory while display and saving work on a derived view, so re-fitting or changing dither mode never needs a reload from disk.
* **Smooth Drag-to-Scroll:** An "Acrobat-style" Hand Tool for panning large images, utilizing `SetCapture` and `ScrollWindowEx` for tear-free movement.
* **Architecture-Aware Rendering:**
    * **Win32s:** Direct-to-screen `StretchDIBits` to stay within 16-bit GDI resource heaps.
    * **NT4/9x:** Double-buffered `BitBlt` to eliminate flickering.

## 🛠 Technical Architecture

### The "486SX" Pipeline
On a 486SX, every clock cycle counts. The image processing pipeline follows these strict rules:
1.  **Integer Only:** All scaling, dithering, and color conversion use bit-shifting (`>>`) instead of division. (Not applicable to external libraries like j40)
2.  **Bottom-Up DIBs:** Images are stored in memory in the native Windows Bottom-Up format to ensure compatibility with the strictest Win32s display drivers.
3.  **Palette Realization:** Full support for `WM_QUERYNEWPALETTE` and `WM_PALETTECHANGED` to ensure color accuracy on 256-color (8-bit) SVGA displays.
4.  **Resize First, Dither Last:** Resampling always happens in full color; Floyd-Steinberg dithering runs once at final display size, since dither patterns are resolution-dependent. Resizing uses an integer-only (Bresenham + 8-bit weights) bilinear filter — no FPU required.
5.  **Explicit Heavy Operations:** Fitting and (re-)dithering run only on explicit key presses or file loads — never on `WM_SIZE` or repaint — so painting stays a cheap 1:1 blit on slow hardware.

### Performance Comparisons
| Format | Decoding Speed (486SX) | Why? | Powered by |
| :--- | :--- | :--- | :--- |
| **QOI** | ⚡ Extremely Fast | Byte-matching opcodes, no complex math. | Internal function |
| **XBM** | ⚡ Fast | Simple hex string parsing. | Internal function |
| **BMP** | ✅ Fast | Zero processing required. | stb_image |
| **PCX** | ✅ Fast | Run Length Encoding can be processed instantly. | dr_pcx |
| **MAG** | ✅ Fast | Flags based Run Length Encoding circular buffer. | stb_mag (as internal function) |
| **TIFF** | ✅ Moderate | Depends on actual encoding. Fast with Raw, Moderate with LZW, Deflate, PackBits, JBIG, and Slow with JPEG. | minitiff, stb_image, stb_jbig |
| **PIC2** | ✅ Moderate | Depends on actual encoding. Fast with Raw, Moderate with compressed methods. | stb_pic2 |
| **JBIG** | ✅ Moderate | QM-coder should be quite capable to decode in 486. | stb_jbig |
| **JBIG2** | ✅ Moderate | JBIG2 could be a bit slower than JBIG because of Glyph Restorations. | stb_jbig2 |
| **PNG** | 🐢 Slow | Complex zlib decompression and PNG filters. | stb_image |
| **JPG** , **WebP** | 🐢 Slow | Heavy IDCT math (emulated on SX). | stb_image, simplewebp |
| **JPEG XL** | 🐢 Slow | Lots of floating point operations. Lossless JPEG XL can be decoded faster than lossy JPEG XL. | j40 |
| **AVIF** | 🐢 Slow | AV1 uses very complicated algorithm to restore image | stb_avif |

## ⌨️ Controls

| Key/Mouse | Action |
| :--- | :--- |
| **Left Click + Drag** | Pan/Scroll image (Hand Tool) |
| **Mouse wheel** | Scroll image (on OS that supports Mouse Wheel, for example, NT4 SP3 or later), Hold `Ctrl` key for horizontal scrolling |
| **Drag file from winfile/explorer** | Open image |
| **Arrow Keys** | Viewport movements |
| **Home/Eng/PgUp/PgDn** | Fast Vertical Viewport movements, Hold `Ctrl` key for horizontal movements |
| **'O'** | Open image |
| **'S'** | Save current view as 24bpp BMP (`-` then `S` saves the fitted-and-dithered image, `0` then `S` saves full-resolution) |
| **'D'** | Change Dither Mode, re-applied live to the current view (0=No Dithering, 1=Dithering depends on display bitdepth, 2=Force Mono Dithering, 3=Force 16-colors Dithering, 4=Force 256-colors Dithering) |
| **'-'** (or numpad `-`) | One-shot shrink-to-fit: resize display image to the client area, aspect-preserving, shrink only |
| **'0'** (or numpad `0`) | Return to 100% size |
| **Esc** | Quit |

## 🏗 Building

### MSVC (authentic Win32s builds)
1.  **Compiler:** Recommended MSVC 4.0, 2.2 (AVIF not working with MSVC 2.2 x86) for authentic Win32s compatibility.
2.  **Memory Model:** Must be compiled as a **Win32 Target**.

### GCC (MinGW-w64, for development/testing)
1.  Add the toolchain to `PATH`, e.g. `C:\msys64\mingw64\bin`.
2.  Run `mingw32-make` (produces `picview-gcc.exe`, `make clean` removes build outputs). The `Makefile` auto-detects 32-bit vs 64-bit targets (`$(CC) -dumpmachine`) to select the correct `pthread_once` linkage.

### Dependencies (common to both toolchains)
* `stb_image.h` (for JPG/PNG support)
* `dr_pcx.h`
* `simplewebp.h`
* `j40.h`
* `minitiff.h`
* `stb_pic2.h`
* `stb_avif.h`
* `stb_jbig.h`
* `stb_jbig2.h`
* `GDI32.lib`, `USER32.lib`, `COMDLG32.lib`

## 📝 Limitations & Notes

* **Memory:** Loading very large images (e.g., 4K resolution) on Win32s may fail due to the 64KB segment limits inherent in the underlying 16-bit Windows 3.1 architecture. Images over ~357 megapixels are rejected outright so the work, error-diffusion, and DIB buffers always fit a 32-bit address space.
* **No auto-fit by design:** Fit-to-window is a deliberate one-shot keypress (`-`), never automatic — continuous re-dithering during window resizing would stall 486-class CPUs.
* **Flicker:** While NT4 is 100% flicker-free, Win32s users may see minor flickering during rapid pans due to the direct-to-device rendering required for stability.

---

*Created for the love of 1990s systems engineering.*
