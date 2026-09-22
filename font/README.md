# Scalable Unicode text

Draws UTF-8 into the four-mode [`video/scanout.c`](../video/scanout.c) framebuffer using Noto Sans Regular (OFL) and `stb_truetype`. Horizontal scale follows the HIL fill millimeters so an em is square on the glass.

```c
font_init();
font_set_size(16.0f);
font_draw_utf8(24, 48, "café αβγ Привет", PIXEL_NORMAL);
```

`y` is the baseline. Unmapped codepoints use the face `.notdef`. CJK is not in this face.
