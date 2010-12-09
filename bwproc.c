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

sample_t*
make_logistic_contrast_curve (float v)
{
	sample_t *curve = alloc_curve ();
	float y_crop = logistic (-v);
	float crop_factor = 1.0f / (1.0f - 2.0f * y_crop);
	int i;

	for (i = 0; i < CURVE_NUM; ++i) {
		float t = (float)i / (float)(CURVE_NUM - 1) * v * 2.0f - v;
		float val = (logistic (t) - y_crop) * crop_factor;
		curve [i] = FLOAT_TO_SAMPLE (val);
	}

	return curve;
}

sample_t*
bw_make_inverted_contrast_curve (void)
{
	sample_t *curve = alloc_curve ();
	int i;

	for (i = 0; i < CURVE_NUM; ++i)
		curve [i] = FLOAT_TO_SAMPLE (1.0 - (float)i / (float)(CURVE_NUM - 1));

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

void
bw_make_rows_cols (int in_width, int in_height,
		   int out_width, int out_height,
		   int rotation,
		   int **_rows, int **_cols,
		   int *out_rotated_width, int *out_rotated_height, int *row_major)
{
	int *rows, *cols;
	int i;

	*_rows = rows = (int*)malloc (sizeof (int) * out_height);
	assert (rows != NULL);
	for (i = 0; i < out_height; ++i) {
		if (rotation == 1 || rotation == 2)
			rows [i] = in_height - 1 - i * in_height / out_height;
		else
			rows [i] = i * in_height / out_height;
	}

	*_cols = cols = (int*)malloc (sizeof (int) * out_width);
	assert (cols != NULL);
	for (i = 0; i < out_width; ++i) {
		if (rotation == 2 || rotation == 3)
			cols [i] = in_width - 1 - i * in_width / out_width;
		else
			cols [i] = i * in_width / out_width;
	}

	if (rotation == BW_ROTATION_0 || rotation == BW_ROTATION_180) {
		*out_rotated_width = out_width;
		*out_rotated_height = out_height;
		*row_major = 1;
	} else {
		*out_rotated_width = out_height;
		*out_rotated_height = out_width;
		*row_major = 0;
	}
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
prepare_vignetting_squares (int num_pixels)
{
	float middle = num_pixels / 2.0;
	sample_t *squares = malloc (sizeof (int) * num_pixels);
	int i;

	assert (squares);

	for (i = 0; i < num_pixels; ++i) {
		float x = fabs (i - middle) / middle;
		squares [i] = FLOAT_TO_SAMPLE (x * x / 2.0);
	}

	return squares;
}

void
bw_process (int width, int height, sample_t *out_data, sample_t *in_data,
	    int num_cols, int *cols, int num_rows, int *rows,
	    unsigned char *cache_mask, sample_t *cache,
	    float red, float green, float blue,
	    int num_contrast_layers, contrast_layer_t *contrast_layers,
	    float tint_hue, float tint_amount)
{
	int32_t red_factor, green_factor, blue_factor;
	int i;
	sample_t f;
	sample_t saturation;
	int row, col;
	sample_t *out;
	sample_t vignetting = FLOAT_TO_SAMPLE (1.0f);
	sample_t grain = FLOAT_TO_SAMPLE (0.5f);

	for (i = 0; i < num_cols; ++i)
		assert (cols [i] >= 0 && cols [i] < width);
	for (i = 0; i < num_rows; ++i)
		assert (rows [i] >= 0 && rows [i] < height);

	prepare_mixer (red, green, blue, &red_factor, &green_factor, &blue_factor);
	prepare_tint (tint_hue, tint_amount, &i, &f, &saturation);

	out = out_data;

	for (row = 0; row < num_rows; ++row) {
		for (col = 0; col < num_cols; ++col) {
			int pixel = rows [row] * width + cols [col];
			sample_t *cache_in = cache + pixel * 3;
			sample_t *in;

			if (cache_mask [pixel])
				goto copy_from_cache;

			cache_mask [pixel] = 1;

			in = in_data + pixel * 3;

#define PIXEL_OUT cache_in
#include "procfunc.h"
#undef PIXEL_OUT

		copy_from_cache:

			out [0] = cache_in [0];
			out [1] = cache_in [1];
			out [2] = cache_in [2];

			out += 3;
		}
	}
}

void
bw_process_no_cache_8 (int width, int height,
		       uint8_t *out_data, int out_pixel_stride, int out_row_stride,
		       uint8_t *in_data, int in_pixel_stride, int in_row_stride,
		       int num_cols, int *cols, int num_rows, int *rows, int row_major,
		       float red, float green, float blue,
		       int num_contrast_layers, contrast_layer_t *contrast_layers,
		       float tint_hue, float tint_amount,
		       sample_t *vignetting_curve,
		       sample_t *grain_buffer)
{
	int32_t red_factor, green_factor, blue_factor;
	int i;
	sample_t f;
	sample_t saturation;
	int row, col;
	uint8_t *out_row;
	sample_t *vsquares_x = NULL;
	sample_t *vsquares_y = NULL;
	sample_t out_pixel [3];
	int grain_buffer_index = 0;

	for (i = 0; i < num_cols; ++i)
		assert (cols [i] >= 0 && cols [i] < width);
	for (i = 0; i < num_rows; ++i)
		assert (rows [i] >= 0 && rows [i] < height);

	prepare_mixer (red, green, blue, &red_factor, &green_factor, &blue_factor);
	prepare_tint (tint_hue, tint_amount, &i, &f, &saturation);

	if (vignetting_curve) {
		vsquares_x = prepare_vignetting_squares (num_cols);
		vsquares_y = prepare_vignetting_squares (num_rows);
	}

	if (row_major) {
		out_row = out_data;
		for (row = 0; row < num_rows; ++row) {
			uint8_t *out = out_row;
			uint8_t *in_row = in_data + rows [row] * in_row_stride;
			sample_t row_square = 0;
			int grain_buffer_increment = random () % GRAIN_BUFFER_INCREMENT_MAX + 1;

			if (vignetting_curve)
				row_square = vsquares_y [row];

			for (col = 0; col < num_cols; ++col) {
				int pixel = rows [row] * width + cols [col];
				uint8_t *in_8 = in_row + cols [col] * in_pixel_stride;
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
	} else {
		for (row = 0; row < num_rows; ++row) {
			uint8_t *in_row = in_data + rows [row] * in_row_stride;
			uint8_t *out;
			sample_t row_square = 0;
			int grain_buffer_increment = random () % GRAIN_BUFFER_INCREMENT_MAX + 1;

			if (vignetting_curve)
				row_square = vsquares_y [row];

			out_row = out_data + row * out_pixel_stride;
			out = out_row;

			for (col = 0; col < num_cols; ++col) {
				int pixel = rows [row] * width + cols [col];
				uint8_t *in_8 = in_row + cols [col] * in_pixel_stride;
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

				out += out_row_stride;
			}

			out_row += out_row_stride;
		}
	}

	if (vignetting_curve) {
		free (vsquares_x);
		free (vsquares_y);
	}
}

static void
translate_layers (int num_layers, contrast_layer_t *layers, sample_t **layer_array)
{
	int i;

	for (i = 0; i < num_layers; ++i) {
		layers [i].curve = layer_array [i * 2 + 0];
		layers [i].mask = layer_array [i * 2 + 1];
	}
}

void
bw_process_layers (int width, int height, sample_t *out_data, sample_t *in_data,
		   int num_cols, int *cols, int num_rows, int *rows,
		   unsigned char *cache_mask, sample_t *cache,
		   float red, float green, float blue,
		   int num_layers, sample_t **layer_array,
		   float tint_hue, float tint_amount)
{
	contrast_layer_t layers[num_layers];

	translate_layers (num_layers, layers, layer_array);

	/*
	if (num_layers >= 1)
		printf("curve: %d %d %d\n", layers [0].curve [0], layers [0].curve [1024], layers [0].curve [2047]);
	*/

	bw_process (width, height, out_data, in_data,
		    num_cols, cols, num_rows, rows,
		    cache_mask, cache,
		    red, green, blue,
		    num_layers, layers,
		    tint_hue, tint_amount);
}

void
bw_process_layers_8 (int width, int height, uint8_t *out_data, sample_t *in_data,
		     int num_cols, int *cols, int num_rows, int *rows,
		     unsigned char *cache_mask, sample_t *cache,
		     float red, float green, float blue,
		     int num_layers, sample_t **layer_array,
		     float tint_hue, float tint_amount)
{
	sample_t *out_data_16 = (sample_t*)malloc (sizeof (sample_t) * num_cols * num_rows * 3);
	int i;

	/*
	printf("in data 16: %d %d %d\n", in_data [0], in_data [1], in_data [2]);
	printf("cache mask: %d\n", cache_mask [0]);
	printf("rgb: %f %f %f\n", red, green, blue);
	printf("tint: %f %f\n", tint_hue, tint_amount);
	*/

	bw_process_layers (width, height, out_data_16, in_data,
			   num_cols, cols, num_rows, rows,
			   cache_mask, cache,
			   red, green, blue,
			   num_layers, layer_array,
			   tint_hue, tint_amount);

	//printf("out data 16: %d %d %d\n", out_data_16 [0], out_data_16 [1], out_data_16 [2]);

	for (i = 0; i < num_cols * num_rows * 3; ++i)
		out_data [i] = out_data_16 [i] >> 8;

	//printf("out data 8: %d %d %d\n", out_data [0], out_data [1], out_data [2]);

	free (out_data_16);
}

void
query_pixel (int width, int height, sample_t *out_pixel, sample_t *in_data,
	     int x, int y,
	     float red, float green, float blue,
	     int num_contrast_layers, contrast_layer_t *contrast_layers,
	     float tint_hue, float tint_amount,
	     sample_t *_mixed, sample_t *layered)
{
	int32_t red_factor, green_factor, blue_factor;
	int i;
	sample_t f;
	sample_t saturation;
	int pixel = y * width + x;
	sample_t *in = in_data + pixel * 3;
	sample_t mixed;
	sample_t vignetting = FLOAT_TO_SAMPLE (1.0f);
	sample_t grain = SAMPLE_MAX / 2;

	assert (x >= 0 && x < width && y >= 0 && y < height);

	prepare_mixer (red, green, blue, &red_factor, &green_factor, &blue_factor);
	prepare_tint (tint_hue, tint_amount, &i, &f, &saturation);

#define PIXEL_OUT out_pixel
#define MIXED mixed
#define LAYERED layered
#include "procfunc.h"
#undef PIXEL_OUT
#undef MIXED
#undef LAYERED

	*_mixed = mixed;
}

void
query_pixel_layers (int width, int height, sample_t *out_pixel, sample_t *in_data,
		    int x, int y,
		    float red, float green, float blue,
		    int num_layers, sample_t **layer_array,
		    float tint_hue, float tint_amount,
		    sample_t *mixed, sample_t *layered)
{
	contrast_layer_t layers [num_layers];

	translate_layers (num_layers, layers, layer_array);

	query_pixel (width, height, out_pixel, in_data,
		     x, y,
		     red, green, blue,
		     num_layers, layers,
		     tint_hue, tint_amount,
		     mixed, layered);
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
	int *rows, *cols;
	contrast_layer_t layers[3];
	int i;
	int x, y;
	int rotation = 0;
	int row_major, out_rotated_width, out_rotated_height;

	assert (argc == 2 || argc == 3);

	orig = read_image (argv[1], &width, &height);
	assert (orig != NULL);

	printf("loaded\n");

	if (argc == 3) {
		rotation = atoi (argv [2]);
		assert (rotation >= 0 && rotation < 4);
	}

	out_width = width;
	out_height = height;

	bw_make_rows_cols (width, height, out_width, out_height, rotation,
			   &rows, &cols, &out_rotated_width, &out_rotated_height, &row_major);

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

	for (i = 0; i < 100; ++i) {
		bw_process_no_cache_8 (width, height,
				       output, 3, out_rotated_width * 3,
				       orig, 3, width * 3,
				       out_width, cols, out_height, rows, row_major,
				       0.5, 0.3, 0.2,
				       3, layers,
				       23.0, 0.1,
				       vignetting_curve,
				       grain_buffer);
	}

	printf("processed\n");

	write_image ("/tmp/beidel2.png", out_rotated_width, out_rotated_height, output, 3, out_rotated_width * 3, IMAGE_FORMAT_PNG);

	return 0;
}
#endif
