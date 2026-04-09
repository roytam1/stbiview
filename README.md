# Win32s Retro Image Viewer

A high-performance, lightweight image viewer specifically engineered for **Windows 3.1x (via Win32s)** and **Windows NT 4.0**. This project bypasses modern GDI overhead to provide a smooth experience on **Intel 486SX** and early Pentium hardware.

## 🚀 Key Features

* **Fixed-Point Dithering:** Custom **Floyd-Steinberg** algorithm with [Amanvir Parhar weights](https://amanvir.com/blog/writing-my-own-dithering-algorithm-in-racket) optimized for processors without an FPU (Floating Point Unit). 
* **Color Cube LUT:** Uses a pre-computed 32x32x32 Look-Up Table to map colors instantly to VGA (16) or Web-Safe (216) palettes, avoiding expensive distance calculations.
* **Custom Generic 256 Color Palette:** Based on ["win8" palette](https://web.archive.org/web/20250227072942/https://eisbox.net/downloads/palettes/win-8.txt) with some modifications on both palette and `FindClosestColor` function.
* **Universal Format Support:**
    * **Modern:** QOI (Quite OK Image), WebP, JPG, PNG, GIF (without animation), BMP.
    * **Retro/Unix:** PCX, TGA, PNM, PGM, PPM, XBM (X-BitMap) and XPM (X-PixMap).
* **Smooth Drag-to-Scroll:** An "Acrobat-style" Hand Tool for panning large images, utilizing `SetCapture` and `ScrollWindowEx` for tear-free movement.
* **Architecture-Aware Rendering:**
    * **Win32s:** Direct-to-screen `StretchDIBits` to stay within 16-bit GDI resource heaps.
    * **NT4/9x:** Double-buffered `BitBlt` to eliminate flickering.

## 🛠 Technical Architecture

### The "486SX" Pipeline
On a 486SX, every clock cycle counts. The image processing pipeline follows these strict rules:
1.  **Integer Only:** All scaling, dithering, and color conversion use bit-shifting (`>>`) instead of division.
2.  **Bottom-Up DIBs:** Images are stored in memory in the native Windows Bottom-Up format to ensure compatibility with the strictest Win32s display drivers.
3.  **Palette Realization:** Full support for `WM_QUERYNEWPALETTE` and `WM_PALETTECHANGED` to ensure color accuracy on 256-color (8-bit) SVGA displays.

### Performance Comparisons
| Format | Decoding Speed (486SX) | Why? |
| :--- | :--- | :--- |
| **QOI** | ⚡ Extremely Fast | Byte-matching opcodes, no complex math. |
| **XBM** | ⚡ Fast | Simple hex string parsing. |
| **BMP** | ✅ Fast | Zero processing required. |
| **JPG** , **WebP** | 🐢 Slow | Heavy IDCT math (emulated on SX). |

## ⌨️ Controls

| Key/Mouse | Action |
| :--- | :--- |
| **Left Click + Drag** | Pan/Scroll image (Hand Tool) |
| **Mouse wheel** | Scroll image (on OS that supports Mouse Wheel, for example, NT4 SP3 or later), Hold `Ctrl` key for horizontal scrolling |
| **Drag file from winfile/explorer** | Open image |
| **Arrow Keys** | Viewport movements |
| **Home/Eng/PgUp/PgDn** | Fast Vertical Viewport movements, Hold `Ctrl` key for horizontal movements |
| **'O'** | Open image |
| **'S'** | Save current view as 24bpp BMP |
| **'D'** | Change Dither Mode (0=No Dithering, 1=Dithering depends on display bitdepth, 2=Force Mono Dithering, 3=Force 16-colors Dithering, 4=Force 256-colors Dithering) |

## 🏗 Building

1.  **Compiler:** Recommended MSVC 2.2, 4.0 for authentic Win32s compatibility.
2.  **Memory Model:** Must be compiled as a **Win32 Target**.
3.  **Dependencies:** * `stb_image.h` (for JPG/PNG support)
    * `simplewebp.h`
    * `GDI32.lib`, `USER32.lib`, `COMDLG32.lib`

## 📝 Limitations & Notes

* **Memory:** Loading very large images (e.g., 4K resolution) on Win32s may fail due to the 64KB segment limits inherent in the underlying 16-bit Windows 3.1 architecture.
* **Flicker:** While NT4 is 100% flicker-free, Win32s users may see minor flickering during rapid pans due to the direct-to-device rendering required for stability.

---

*Created for the love of 1990s systems engineering.*
