#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "bwproc.h"

#define SAMPLE_MAX       65535

#define RGB_MULT         2048
#define RGB_SHIFT        11

#define CURVE_NUM     2048
#define CURVE_SHIFT   5

#define GRAIN_BUFFER_SIZE		29947
#define GRAIN_BUFFER_INCREMENT_MAX	32

#define FLOAT_TO_SAMPLE(f)      ((sample_t)((f) * SAMPLE_MAX))
#define SAMPLE_MUL(a,b)		((sample_t)(((uint32_t)(a) * (uint32_t)(b)) >> 16))

static sample_t*
alloc_curve (void)
{
	sample_t *curve = malloc (sizeof (sample_t) * CURVE_NUM);
	assert (curve);
	return curve;
}

static float
logistic (float t)
{
    return 1.0f / (1.0f + expf (-t));
}

static float
logit (float t)
{
	return -logf (1.0f / t - 1.0f);
}

sample_t*
make_logistic_contrast_curve (float v)
{
	sample_t *curve = alloc_curve ();

	if (abs (v) < 0.0001) {
		// linear
		int i;

		for (i = 0; i < CURVE_NUM; ++i)
			curve [i] = FLOAT_TO_SAMPLE ((float)i / (float)(CURVE_NUM - 1));
	} else if (v > 0) {
		// logistic
		float y_crop = logistic (-v);
		float crop_factor = 1.0f / (1.0f - 2.0f * y_crop);
		int i;

		for (i = 0; i < CURVE_NUM; ++i) {
			float t = (float)i / (float)(CURVE_NUM - 1) * v * 2.0f - v;
			float val = (logistic (t) - y_crop) * crop_factor;
			curve [i] = FLOAT_TO_SAMPLE (val);
		}
	} else {
		// logit
		float x_crop = logistic (v);
		int i;

		for (i = 0; i < CURVE_NUM; ++i) {
			float t = (float)i / (float)(CURVE_NUM - 1) * (1 - 2 * x_crop) + x_crop;
			float val = logit (t) / -v / 2 + 0.5f;
			curve [i] = FLOAT_TO_SAMPLE (val);
		}
	}

	return curve;
}

sample_t*
bw_make_inverted_contrast_curve (float min, float max)
{
	sample_t *curve = alloc_curve ();
	int i;

	for (i = 0; i < CURVE_NUM; ++i) {
		float inverted = 1.0 - (float)i / (float)(CURVE_NUM - 1);
		curve [i] = FLOAT_TO_SAMPLE (min + inverted * (max - min));
	}

	return curve;
}

sample_t*
bw_make_gamma_contrast_curve (float gamma)
{
	sample_t *curve = alloc_curve ();
	int i;

	for (i = 0; i < CURVE_NUM; ++i)
		curve [i] = FLOAT_TO_SAMPLE (powf ((float)i / (float)(CURVE_NUM - 1), gamma));

	return curve;
}

sample_t*
bw_make_sinusoidal_vignetting_curve (float start, float z, float exponent)
{
	sample_t *curve = alloc_curve ();
	int i;

	assert (start >= 0.0f && start <= 1.0f);

	start = start * start;

	for (i = 0; i < CURVE_NUM * start; ++i)
		curve [i] = FLOAT_TO_SAMPLE (1.0f);

	for (; i < CURVE_NUM; ++i) {
		float x = sqrt (((float)i / (float)(CURVE_NUM - 1) - start) / (1.0f - start));
		if (x >= z)
			curve [i] = FLOAT_TO_SAMPLE (0.0f);
		else
			curve [i] = FLOAT_TO_SAMPLE (pow (cos (x * (M_PI / 2.0) / z), exponent));
	}

	return curve;
}

static float
randf (void)
{
	return (float) random () / RAND_MAX;
}

sample_t*
bw_make_uniform_grain_buffer (float max)
{
	int i;
	sample_t *buffer;

	assert (max >= 0 && max <= 0.5);

	buffer = malloc (sizeof (sample_t) * GRAIN_BUFFER_SIZE);
	assert (buffer);

	for (i = 0; i < GRAIN_BUFFER_SIZE; ++i)
		buffer [i] = FLOAT_TO_SAMPLE (randf () * max * 2.0f + (0.5f - max));

	return buffer;
}

static float
rand_gauss_unit (void)
{
	float x1, x2, w;

	do {
		x1 = randf () * 2.0f - 1.0f;
		x2 = randf () * 2.0f - 1.0f;
		w = x1 * x1 + x2 * x2;
	} while (w >= 1.0f);

	w = sqrtf ((logf (w) * -2.0f) / w);

	return x1 * w;
}

static float
rand_gauss (float variance)
{
	return rand_gauss_unit () * sqrtf (variance);
}

sample_t*
bw_make_gaussian_grain_buffer (float variance)
{
	int i;
	sample_t *buffer;

	buffer = malloc (sizeof (sample_t) * GRAIN_BUFFER_SIZE);
	assert (buffer);

	for (i = 0; i < GRAIN_BUFFER_SIZE; ++i) {
		float x = rand_gauss (variance);

		if (x < -1)
			x = -1;
		else if (x > 1)
			x = 1;

		buffer [i] = FLOAT_TO_SAMPLE (x * 0.5f + 0.5f);
	}

	return buffer;
}

static void
prepare_mixer (float red, float green, float blue, int32_t *red_factor, int32_t *green_factor, int32_t *blue_factor)
{
	*red_factor = (int32_t)(red * RGB_MULT);
	*green_factor = (int32_t)(green * RGB_MULT);
	*blue_factor = (int32_t)(blue * RGB_MULT);
}

static void
prepare_tint (float tint_hue, float tint_amount, int *i, sample_t *f, sample_t *saturation)
{
	*saturation = FLOAT_TO_SAMPLE (tint_amount);

	tint_hue = tint_hue / 60.0;
	if (tint_hue >= 6.0)
		tint_hue -= 6.0;
	*i = (int)floorf(tint_hue);
	*f = FLOAT_TO_SAMPLE (tint_hue - *i);
}

static sample_t*
prepare_tint_curve (float tint_hue, float tint_amount)
{
	int i;
	int index;
	sample_t f, saturation;
	sample_t *curve;

	prepare_tint (tint_hue, tint_amount, &i, &f, &saturation);

	curve = malloc (sizeof (sample_t) * 3 * CURVE_NUM);
	assert (curve != NULL);

	for (index = 0; index < CURVE_NUM; ++index) {
		sample_t inverted;
		sample_t s, v, p, q, t;
		sample_t r, g, b;
		sample_t grained = index << CURVE_SHIFT;
		assert (grained <= SAMPLE_MAX);

		inverted = SAMPLE_MAX - grained;

		if (inverted < SAMPLE_MAX / 2) {
			s = inverted * 2;
			v = SAMPLE_MAX;
		} else {
			s = SAMPLE_MAX;
			if (inverted == SAMPLE_MAX)
				v = 0;
			else
				v = SAMPLE_MAX - ((int)inverted - SAMPLE_MAX / 2) * 2;
		}

		p = SAMPLE_MUL (v, (SAMPLE_MAX - s));
		q = SAMPLE_MUL (v, (SAMPLE_MAX - (SAMPLE_MUL (s, f))));
		t = SAMPLE_MUL (v, (SAMPLE_MAX - (SAMPLE_MUL (s, (SAMPLE_MAX - f)))));

		switch (i) {
			case 0 : r = v; g = t; b = p; break;
			case 1 : r = q; g = v; b = p; break;
			case 2 : r = p; g = v; b = t; break;
			case 3 : r = p; g = q; b = v; break;
			case 4 : r = t; g = p; b = v; break;
			case 5 : r = v; g = p; b = q; break;

			default :
				assert(0);
		}

		curve [index * 3 + 0] = SAMPLE_MUL (r, saturation) + SAMPLE_MUL ((SAMPLE_MAX - inverted), (SAMPLE_MAX - saturation));
		curve [index * 3 + 1] = SAMPLE_MUL (g, saturation) + SAMPLE_MUL ((SAMPLE_MAX - inverted), (SAMPLE_MAX - saturation));
		curve [index * 3 + 2] = SAMPLE_MUL (b, saturation) + SAMPLE_MUL ((SAMPLE_MAX - inverted), (SAMPLE_MAX - saturation));
	}

	return curve;
}

static sample_t*
prepare_vignetting_squares (int num_pixels)
{
	float middle = num_pixels / 2.0;
	sample_t *squares = malloc (sizeof (sample_t) * num_pixels);
	int i;

	assert (squares);

	for (i = 0; i < num_pixels; ++i) {
		float x = fabs (i - middle) / middle;
		squares [i] = FLOAT_TO_SAMPLE (x * x / 2.0);
	}

	return squares;
}

void
bw_process_no_cache_8 (int width, int height,
		       uint8_t *out_data, int out_pixel_stride, int out_row_stride,
		       uint8_t *in_data, int in_pixel_stride, int in_row_stride,
		       float red, float green, float blue,
		       int num_contrast_layers, contrast_layer_t *contrast_layers,
		       float tint_hue, float tint_amount,
		       sample_t *vignetting_curve,
		       sample_t *grain_buffer)
{
	int32_t red_factor, green_factor, blue_factor;
	sample_t *tint_curve;
	int row, col;
	uint8_t *out_row;
	sample_t *vsquares_x = NULL;
	sample_t *vsquares_y = NULL;
	sample_t out_pixel [3];
	int grain_buffer_index = 0;

	prepare_mixer (red, green, blue, &red_factor, &green_factor, &blue_factor);
	tint_curve = prepare_tint_curve (tint_hue, tint_amount);

	if (vignetting_curve) {
		vsquares_x = prepare_vignetting_squares (width);
		vsquares_y = prepare_vignetting_squares (height);
	}

	out_row = out_data;
	for (row = 0; row < height; ++row) {
		uint8_t *out = out_row;
		uint8_t *in_row = in_data + row * in_row_stride;
		sample_t row_square = 0;
		int grain_buffer_increment = random () % GRAIN_BUFFER_INCREMENT_MAX + 1;

		if (vignetting_curve)
			row_square = vsquares_y [row];

		for (col = 0; col < width; ++col) {
			int pixel = row * width + col;
			uint8_t *in_8 = in_row + col * in_pixel_stride;
			sample_t in [3];
			sample_t vignetting, grain;

			if (vignetting_curve) {
				sample_t col_square = vsquares_x [col];
				sample_t square = row_square + col_square;
				vignetting = vignetting_curve [square >> CURVE_SHIFT];
			} else {
				vignetting = FLOAT_TO_SAMPLE (1.0f);
			}

			if (grain_buffer) {
				grain = grain_buffer [grain_buffer_index];

				grain_buffer_index += grain_buffer_increment;
				if (grain_buffer_index >= GRAIN_BUFFER_SIZE)
					grain_buffer_index -= GRAIN_BUFFER_SIZE;
			} else {
				grain = SAMPLE_MAX / 2;
			}

			in [0] = (sample_t)in_8 [0] << 8;
			in [1] = (sample_t)in_8 [1] << 8;
			in [2] = (sample_t)in_8 [2] << 8;

#define PIXEL_OUT out_pixel
#include "procfunc.h"
#undef PIXEL_OUT

			out [0] = out_pixel [0] >> 8;
			out [1] = out_pixel [1] >> 8;
			out [2] = out_pixel [2] >> 8;

			out += out_pixel_stride;
		}

		out_row += out_row_stride;
	}

	if (vignetting_curve) {
		free (vsquares_x);
		free (vsquares_y);
	}
}

#ifdef BW_COMMANDLINE
#include "rwimg/readimage.h"
#include "rwimg/writeimage.h"

int
main (int argc, char *argv[])
{
	unsigned char *orig;
	unsigned char *output;
	int width, height;
	int out_width, out_height;
	sample_t contrast_curve [CURVE_NUM];
	sample_t brighten_curve [CURVE_NUM];
	sample_t darken_curve [CURVE_NUM];
	sample_t *vignetting_curve, *grain_buffer;
	contrast_layer_t layers[3];
	int i;
	int x, y;

	assert (argc == 2);

	orig = read_image (argv[1], &width, &height);
	assert (orig != NULL);

	printf("loaded\n");

	out_width = width;
	out_height = height;

	for (i = 0; i < CURVE_NUM; ++i) {
		contrast_curve [i] = (sample_t)i << CURVE_SHIFT;
		brighten_curve [i] = FLOAT_TO_SAMPLE (powf ((float)(i) / CURVE_NUM, 0.5));
		darken_curve [i] = FLOAT_TO_SAMPLE (powf ((float)(i) / CURVE_NUM, 2.0));
	}

	layers [0].curve = contrast_curve;
	layers [0].mask = NULL;

	layers [1].curve = brighten_curve;
	layers [1].mask = (sample_t*)malloc (width * height * sizeof (sample_t));

	layers [2].curve = darken_curve;
	layers [2].mask = (sample_t*)malloc (width * height * sizeof (sample_t));

	for (y = 0; y < height; ++y)
		for (x = 0; x < width; ++x) {
			float fx = (float)(x - (width / 2)) / (width / 2);
			float fy = (float)(y - (height / 2)) / (height / 2);
			float r = sqrtf(fx*fx + fy*fy);

			if (r >= 1.0) {
				layers [1].mask [y * width + x] = 0;
				layers [2].mask [y * width + x] = SAMPLE_MAX;
			} else {
				layers [1].mask [y * width + x] = FLOAT_TO_SAMPLE (1.0 - r);
				layers [2].mask [y * width + x] = FLOAT_TO_SAMPLE (r);
			}
		}

	vignetting_curve = bw_make_sinusoidal_vignetting_curve (0.3, 1.0, 0.5);

	grain_buffer = bw_make_uniform_grain_buffer (0.2);

	output = (unsigned char*)malloc (out_width * out_height * 3);
	assert (output != NULL);

	for (i = 0; i < 200; ++i) {
		bw_process_no_cache_8 (width, height,
				       output, 3, out_width * 3,
				       orig, 3, width * 3,
				       0.5, 0.3, 0.2,
				       1, layers,
				       23.0, 0.1,
				       vignetting_curve,
				       grain_buffer);
	}

	printf("processed\n");

	write_image ("/tmp/beidel2.png", out_width, out_height, output, 3, width * 3, IMAGE_FORMAT_PNG);

	return 0;
}
#endif
