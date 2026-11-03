#ifndef CHESSPLATE_SVG_H
#define CHESSPLATE_SVG_H

#include <stdbool.h>
#include <stddef.h>

#include "raster.h"

/* Renders a (small, static) SVG document into a freshly allocated image of
 * width x height pixels, scaling the viewBox to fill it.
 *
 * Supported: svg, g, path (all commands incl. arcs), rect (rx/ry), circle,
 * ellipse, line, polyline, polygon; fill/stroke colors, opacity attributes,
 * fill-rule, stroke width/caps/joins/miter limit, transforms, inherited
 * presentation attributes and style="", linearGradient with stops.
 * Filters, text, clipping and masks are ignored. */
bool svg_render(const char *data, size_t len, int width, int height, Image *out);

#endif
