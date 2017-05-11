#ifndef MUPDF_DRAW_IMP_H
#define MUPDF_DRAW_IMP_H

#define BBOX_MIN -(1<<20)
#define BBOX_MAX (1<<20)

/* divide and floor towards -inf */
static inline int fz_idiv(int a, int b)
{
	return a < 0 ? (a - b + 1) / b : a / b;
}

/* divide and ceil towards inf */
static inline int fz_idiv_up(int a, int b)
{
	return a < 0 ? a / b : (a + b - 1) / b;
}

#ifdef AA_BITS

#define fz_aa_scale 0

#if AA_BITS > 6
#define AA_SCALE(s, x) (x)
#define fz_aa_hscale 17
#define fz_aa_vscale 15
#define fz_aa_bits 8
#define fz_aa_text_bits 8

#elif AA_BITS > 4
#define AA_SCALE(s, x) ((x * 255) >> 6)
#define fz_aa_hscale 8
#define fz_aa_vscale 8
#define fz_aa_bits 6
#define fz_aa_text_bits 6

#elif AA_BITS > 2
#define AA_SCALE(s, x) (x * 17)
#define fz_aa_hscale 5
#define fz_aa_vscale 3
#define fz_aa_bits 4
#define fz_aa_text_bits 4

#elif AA_BITS > 0
#define AA_SCALE(s, x) ((x * 255) >> 2)
#define fz_aa_hscale 2
#define fz_aa_vscale 2
#define fz_aa_bits 2
#define fz_aa_text_bits 2

#else
#define AA_SCALE(s, x) (x * 255)
#define fz_aa_hscale 1
#define fz_aa_vscale 1
#define fz_aa_bits 0
#define fz_aa_text_bits 0

#endif
#else

#define AA_SCALE(scale, x) ((x * scale) >> 8)
#define fz_aa_hscale (ctx->aa->hscale)
#define fz_aa_vscale (ctx->aa->vscale)
#define fz_aa_scale (ctx->aa->scale)
#define fz_aa_bits (ctx->aa->bits)
#define fz_aa_text_bits (ctx->aa->text_bits)

#endif

/* If AA_BITS is defined, then we assume constant N bits of antialiasing. We
 * will attempt to provide at least that number of bits of accuracy in the
 * antialiasing (to a maximum of 8). If it is defined to be 0 then no
 * antialiasing is done. If it is undefined to we will leave the antialiasing
 * accuracy as a run time choice.
 */
struct fz_aa_context_s
{
	int hscale;
	int vscale;
	int scale;
	int bits;
	int text_bits;
	float min_line_width;
};

/*
 * Scan converter
 */

typedef struct fz_rasterizer_s fz_rasterizer;

typedef void (fz_rasterizer_drop_fn)(fz_context *ctx, fz_rasterizer *r);
typedef int (fz_rasterizer_reset_fn)(fz_context *ctx, fz_rasterizer *r);
typedef void (fz_rasterizer_postindex_fn)(fz_context *ctx, fz_rasterizer *r);
typedef void (fz_rasterizer_insert_fn)(fz_context *ctx, fz_rasterizer *r, float x0, float y0, float x1, float y1, int rev);
typedef void (fz_rasterizer_insert_rect_fn)(fz_context *ctx, fz_rasterizer *r, float fx0, float fy0, float fx1, float fy1);
typedef void (fz_rasterizer_gap_fn)(fz_context *ctx, fz_rasterizer *r);
typedef fz_irect *(fz_rasterizer_bound_fn)(fz_context *ctx, const fz_rasterizer *r, fz_irect *bbox);
typedef void (fz_rasterizer_fn)(fz_context *ctx, fz_rasterizer *r, int eofill, const fz_irect *clip, fz_pixmap *pix, unsigned char *colorbv);
typedef int (fz_rasterizer_is_rect_fn)(fz_context *ctx, fz_rasterizer *r);

typedef struct
{
	fz_rasterizer_drop_fn *drop;
	fz_rasterizer_reset_fn *reset;
	fz_rasterizer_postindex_fn *postindex;
	fz_rasterizer_insert_fn *insert;
	fz_rasterizer_insert_rect_fn *rect;
	fz_rasterizer_gap_fn *gap;
	fz_rasterizer_fn *convert;
	fz_rasterizer_is_rect_fn *is_rect;
	int reusable;
} fz_rasterizer_fns;

struct fz_rasterizer_s
{
	fz_rasterizer_fns fns;
	fz_irect clip; /* Specified clip rectangle */
	fz_irect bbox; /* Measured bbox of path while stroking/filling */
};

/*
	When rasterizing a shape, we first create a rasterizer then
	run through the edges of the shape, feeding them in.

	For a fill, this is easy as we just run along the path, feeding
	edges as we go.

	For a stroke, this is trickier, as we feed in edges from
	alternate sides of the stroke as we proceed along it. It is only
	when we reach the end of a subpath that we know whether we need
	an initial cap, or whether the list of edges match up.

	To identify whether a given edge fed in is forward or reverse,
	we tag it with a 'rev' value.

	Consider the following simplified example:

	Consider a simple path A, B, C, D, close.

	+------->-------+	The outside edge of this shape is the
	| A           B |	forward edge. This is fed into the rasterizer
	|   +---<---+   |	in order, with rev=0.
	|   |       |   |
	^   v       ^   v	The inside edge of this shape is the reverse
	|   |       |   |	edge. These edges are generated as we step
	|   +--->---+   |	through the path in clockwise order, but
	| D           C |	conceptually the path runs the other way.
	+-------<-------+	These are fed into the rasterizer in clockwise
				order, with rev=1.

	Consider another path, this time an open one: A,B,C,D

	+--->-------+	The outside edge of this shape is again the
	* A       B |	forward edge. This is fed into the rasterizer
	+---<---+   |	in order, with rev=0.
		|   |
		^   v	The inside edge of this shape is the reverse
		|   |	edge. These edges are generated as we step
	+--->---+   |	through the path in clockwise order, but
	^ D       C |	conceptually the path runs the other way.
	+---<-------+	These are fed into the rasterizer in clockwise
			order, with rev=1.

	At the end of the path, we realise that this is an open path, and we
	therefore have to put caps on. The cap at 'D' is easy, because it's
	a simple continuation of the rev=0 edge list that joins to the end
	of the rev=1 edge list.

	The cap at 'A' is trickier; it either needs to be (an) edge(s) prepended
	to the rev=0 list or the rev=1 list. We signal this special case by
	sending them with the special value rev=2.

	The "edge" rasterizer ignores these values. The "edgebuffer" rasterizer
	needs to use them to ensure that edges are correctly joined together
	to allow for any part of a pixel operation.
*/

/*
	fz_new_rasterizer: Create a new rasterizer instance.
	This encapsulates a scan converter.

	A single rasterizer instance can be used to scan convert many
	things.
*/
fz_rasterizer *fz_new_rasterizer(fz_context *ctx);

/*
	fz_drop_rasterizer: Dispose of a rasterizer once
	finished with.
*/
static inline void fz_drop_rasterizer(fz_context *ctx, fz_rasterizer *r)
{
	if (r)
		r->fns.drop(ctx, r);
}

/*
	fz_reset_rasterizer: Reset a rasterizer, ready to scan convert
	a new shape.

	clip: A pointer to a (device space) clipping rectangle.

	Returns 1 if a indexing pass is required, or 0 if not.

	After this, the edges should be 'inserted' into the rasterizer.
*/
int fz_reset_rasterizer(fz_context *ctx, fz_rasterizer *r, const fz_irect *clip);

/*
	fz_insert_rasterizer: Insert an edge into a rasterizer.

	x0, y0: Initial point

	x1, y1: Final point

	rev: 'reverse' value, 0, 1 or 2. See above.
*/
static inline void fz_insert_rasterizer(fz_context *ctx, fz_rasterizer *r, float x0, float y0, float x1, float y1, int rev)
{
	r->fns.insert(ctx, r, x0, y0, x1, y1, rev);
}

/*
	fz_insert_rasterizer: Insert a rectangle into a rasterizer.

	x0, y0: One corner of the rectangle.

	x1, y1: The opposite corner of the rectangle.

	The rectangle inserted is conceptually:
		(x0,y0)->(x1,y0)->(x1,y1)->(x0,y1)->(x0,y0).

	This method is only used for axis aligned rectangles,
	and enables rasterizers to perform special 'anti-dropout'
	processing to ensure that horizontal artifacts aren't
	lost.
*/
static inline void fz_insert_rasterizer_rect(fz_context *ctx, fz_rasterizer *r, float x0, float y0, float x1, float y1)
{
	r->fns.rect(ctx, r, x0, y0, x1, y1);
}

/*
	fz_gap_rasterizer: Called to indicate that there is a gap
	in the lists of edges fed into the rasterizer (i.e. when
	a path hits a move).
*/
static inline void fz_gap_rasterizer(fz_context *ctx, fz_rasterizer *r)
{
	if (r->fns.gap)
		r->fns.gap(ctx, r);
}

/*
	fz_antidropout_rasterizer: Detect whether antidropout
	behaviour is required with this rasterizer.

	Returns 1 if required, 0 otherwise.
*/
static inline int fz_antidropout_rasterizer(fz_context *ctx, fz_rasterizer *r)
{
	return r->fns.rect != NULL;
}

/*
	fz_postindex_rasterizer: Called to signify the end of the
	indexing phase.

	After this has been called, the edges should be inserted
	again.
*/
static inline void fz_postindex_rasterizer(fz_context *ctx, fz_rasterizer *r)
{
	if (r->fns.postindex)
		r->fns.postindex(ctx, r);
}

/*
	fz_bound_rasterizer: Once a set of edges has been fed into a
	rasterizer, the (device space) bounding box can be retrieved.
*/
fz_irect *fz_bound_rasterizer(fz_context *ctx, const fz_rasterizer *rast, fz_irect *bbox);

/*
	fz_scissor_rasterizer: Retrieve the clipping box with which the
	rasterizer was reset.
*/
fz_rect *fz_scissor_rasterizer(fz_context *ctx, const fz_rasterizer *rast, fz_rect *r);

/*
	fz_convert_rasterizer: Convert the set of edges that have
	been fed in, into pixels within the pixmap.

	eofill: Fill rule; True for even odd, false for non zero.

	pix: The pixmap to fill into.

	colorbv: The color components corresponding to the pixmap.
*/
void fz_convert_rasterizer(fz_context *ctx, fz_rasterizer *r, int eofill, fz_pixmap *pix, unsigned char *colorbv);

/*
	fz_is_rect_rasterizer: Detect if the edges fed into a
	rasterizer make up a simple rectangle.
*/
static inline int fz_is_rect_rasterizer(fz_context *ctx, fz_rasterizer *r)
{
	return r->fns.is_rect(ctx, r);
}

void *fz_new_rasterizer_of_size(fz_context *ctx, int size, const fz_rasterizer_fns *fns);

#define fz_new_derived_rasterizer(C,M,F) \
	((M*)Memento_label(fz_new_rasterizer_of_size(C, sizeof(M), F), #M))

fz_rasterizer *fz_new_gel(fz_context *ctx);

typedef enum
{
	FZ_EDGEBUFFER_ANY_PART_OF_PIXEL,
	FZ_EDGEBUFFER_CENTER_OF_PIXEL
} fz_edgebuffer_rule;

fz_rasterizer *fz_new_edgebuffer(fz_context *ctx, fz_edgebuffer_rule rule);

int fz_flatten_fill_path(fz_context *ctx, fz_rasterizer *rast, const fz_path *path, const fz_matrix *ctm, float flatness, const fz_irect *irect, fz_irect *bounds);
int fz_flatten_stroke_path(fz_context *ctx, fz_rasterizer *rast, const fz_path *path, const fz_stroke_state *stroke, const fz_matrix *ctm, float flatness, float linewidth, const fz_irect *irect, fz_irect *bounds);

fz_irect *fz_bound_path_accurate(fz_context *ctx, fz_irect *bbox, const fz_irect *scissor, const fz_path *path, const fz_stroke_state *stroke, const fz_matrix *ctm, float flatness, float linewidth);

/*
 * Plotting functions.
 */

typedef void (fz_solid_color_painter_t)(unsigned char * restrict dp, int n, int w, const unsigned char * restrict color, int da);

typedef void (fz_span_painter_t)(unsigned char * restrict dp, int da, const unsigned char * restrict sp, int sa, int n, int w, int alpha);
typedef void (fz_span_color_painter_t)(unsigned char * restrict dp, const unsigned char * restrict mp, int n, int w, const unsigned char * restrict color, int da);

fz_solid_color_painter_t *fz_get_solid_color_painter(int n, const unsigned char * restrict color, int da);
fz_span_painter_t *fz_get_span_painter(int da, int sa, int n, int alpha);
fz_span_color_painter_t *fz_get_span_color_painter(int n, int da, const unsigned char * restrict color);

void fz_paint_image(fz_pixmap * restrict dst, const fz_irect * restrict scissor, fz_pixmap * restrict shape, const fz_pixmap * restrict img, const fz_matrix * restrict ctm, int alpha, int lerp_allowed, int gridfit_as_tiled);
void fz_paint_image_with_color(fz_pixmap * restrict dst, const fz_irect * restrict scissor, fz_pixmap *restrict shape, const fz_pixmap * restrict img, const fz_matrix * restrict ctm, const unsigned char * restrict colorbv, int lerp_allowed, int gridfit_as_tiled);

void fz_paint_pixmap(fz_pixmap * restrict dst, const fz_pixmap * restrict src, int alpha);
void fz_paint_pixmap_with_mask(fz_pixmap * restrict dst, const fz_pixmap * restrict src, const fz_pixmap * restrict msk);
void fz_paint_pixmap_with_bbox(fz_pixmap * restrict dst, const fz_pixmap * restrict src, int alpha, fz_irect bbox);

void fz_blend_pixmap(fz_pixmap * restrict dst, fz_pixmap * restrict src, int alpha, int blendmode, int isolated, const fz_pixmap * restrict shape);
void fz_blend_pixel(unsigned char dp[3], unsigned char bp[3], unsigned char sp[3], int blendmode);

void fz_paint_glyph(const unsigned char * restrict colorbv, fz_pixmap * restrict dst, unsigned char * restrict dp, const fz_glyph * restrict glyph, int w, int h, int skip_x, int skip_y);

#endif
