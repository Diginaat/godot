# Engine performance: research notes

- **HZB at half resolution (used in OD2).**
  - Karis, Stubbe, Wihlidal, "A Deep Dive into Nanite Virtualized
    Geometry" (SIGGRAPH 2021): the HZB is built from the depth buffer at
    half resolution, rounded to a power of two.
  - Wihlidal, "Optimizing the Graphics Pipeline with Compute" (GDC 2016):
    boxes are tested on the mip where they cover at most 2 x 2 texels.
- **Buffer growth (used in OD1).** Many standard libraries grow
  geometrically by less than 2 (often 1.5), trading a few more
  reallocations for less slack. For GPU buffers each reallocation means a
  whole re-upload. OD1 therefore uses powers of two for small buffers, and
  above that an eighth of headroom in fixed steps of 16,384 elements. That
  keeps both the slack and the number of re-uploads small.
