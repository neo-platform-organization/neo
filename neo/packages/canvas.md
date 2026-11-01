# Minimum UI canvas

`neo.ui.canvas` 0.1.0 exports `canvas`. This is image-owned drawing behavior over
a host-granted READ+WRITE `buffer`: clipped rectangles and an original5×7 bitmap
font. There are no native widgets, font APIs or runtime source-file loads.

Call setup after binding buffer. It caches dimensions and sets clip-left/top=0,
clip-right/bottom to buffer bounds. These define a half-open rectangle within the
buffer. All colour fields red/green/blue/alpha must be bytes; scale must be positive.

| Handler | Inputs and behavior |
| --- | --- |
| text | text, x, y, scale, max-chars; draws at most max-chars bytes with6×8 scaled cell advance |
| rect | x, y, rect-width/height; clips the rectangle before iterating its pixels |
| setup | Refresh dimensions and reset clipping to the complete buffer |

Text draws foreground pixels only. ASCII lowercase uses uppercase glyph shapes;
stored text stays unchanged. Unsupported bytes, including non-ASCII UTF-8 bytes,
draw replacement question marks. This is not Unicode shaping, grapheme editing,
line wrapping, antialiasing or a general text editor. The full font is ordinary
integer-array data in the image. Colour is stored directly; there is no blending.

drawn reports actual written pixels. Calls are sequential and use instance-owned
scratch. Validation/arithmetic/authority failure is explicit; budget/allocation
failure can leave partial output. There is no implicit transaction or redraw loop.

The canvas uses general payload operations text-bytes and text-concat where
needed. They are bounded C substrate operations, not C text rendering. The GUI
prototype contains an independent copy of the verified canvas graph, following
the existing prototype model rather than introducing shared mutable behavior.

Universal tests compare exact A-glyph pixels at scales1/2, clipped rectangle
pixels/counts and denied WRITE. They pass before browser integration is exercised.
