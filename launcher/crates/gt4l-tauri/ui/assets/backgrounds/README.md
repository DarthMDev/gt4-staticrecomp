# Home background slideshow

The home page shows six full-screen images that crossfade and pan slowly from right to left (see `css/app.css`,
`bg-show` and `bg-pan`). The slots are listed in `ui/js/config.js` (`GT4.config.backgrounds`), in order:
`bg-01.webp` to `bg-06.webp`, CSS classes `bg-slide-1` to `bg-slide-6`.

The images are GT4 scenes the owner prepared (AI-upscaled, 2026-10-20). They were converted to WebP at 2560 px
wide, quality 82, 140-300 KB each.

## Replacing or adding images

1. Use 16:9 images, at least 1920x1080. Convert them to WebP at about 2560 px wide, quality around 82.
2. Put them in this folder and list them in `ui/js/config.js`; the order is the slideshow order.
3. If the count changes, update the slot classes and the cycle in `css/app.css`: 12 s per slot, cycle = 12 s x
   slots, and the `bg-show`/`bg-pan` keyframe percentages.
4. Check the home page at 1280x720 and 1920x1080 in the dev mock (`ui/dev/index.html#/home`). The dark gradient
   keeps the logo and Play readable; keep important parts of each image away from the bottom left.

Under `prefers-reduced-motion: reduce` only the first image is shown, without the pan.
