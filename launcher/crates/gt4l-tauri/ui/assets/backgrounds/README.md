# Home background slideshow

The home page shows three full-screen images that crossfade and pan slowly (see `css/app.css`, `bg-show` and
`bg-pan`). The slots are listed in `ui/js/config.js` (`GT4.config.backgrounds`), in order:

| Slot | Current file | CSS class |
|---|---|---|
| 1 | `assets/backgrounds/bg-01.png` | `bg-slide-1` |
| 2 | `assets/backgrounds/bg-02.png` | `bg-slide-2` |
| 3 | `assets/backgrounds/bg-03.png` | `bg-slide-3` |

The current three files are placeholders: copies of the project cover art (`assets/cover-front.png`).

## Dropping in the final images

1. Use 16:9 images, at least 1920x1080, as `.webp` or `.jpg` (`.png` also works).
2. Put them in this folder, for example `bg-01.webp`, `bg-02.webp`, `bg-03.webp`.
3. Edit the three paths in `ui/js/config.js` to the new file names (the order is the slideshow order).
4. Check the home page at 1280x720 and 1920x1080 in the dev mock (`ui/dev/index.html#/home`). The dark gradient
   over the image keeps the logo and Play readable; keep the important parts of each image away from the bottom left.

No code change is needed beyond the file names. The timing (12 s per image, 1.5 s crossfade, 36 s cycle) is in
`css/app.css`. Under `prefers-reduced-motion: reduce` only the first image is shown, without the pan.
