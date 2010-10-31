#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "bwproc.h"

/*
#include "rwimg/readimage.h"
#include "rwimg/writeimage.h"
*/

#define SAMPLE_MAX       65535

#define RGB_MULT         2048
#define RGB_SHIFT        11

#define CURVE_NUM     2048
#define CURVE_SHIFT   5

#define FLOAT_TO_SAMPLE(f)      ((sample_t)((f) * SAMPLE_MAX))
#define SAMPLE_MUL(a,b)		((sample_t)(((uint32_t)(a) * (uint32_t)(b)) >> 16))

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

/*
int
main (int argc, char *argv[])
{
	unsigned char *orig;
	unsigned char *output;
	int width, height;
	int out_width, out_height;
	sample_t *data, *out_data;
	sample_t contrast_curve [CURVE_NUM];
	sample_t brighten_curve [CURVE_NUM];
	sample_t darken_curve [CURVE_NUM];
	unsigned char *cache_mask;
	sample_t *cache;
	int *rows, *cols;
	contrast_layer_t layers[3];
	int i;
	int x, y;

	assert (argc == 2);

	orig = read_image (argv[1], &width, &height);
	assert (orig != NULL);

	printf("loaded\n");

	data = (sample_t*)malloc (sizeof (sample_t) * width * height * 3);
	assert (data != NULL);

	for (i = 0; i < width * height * 3; ++i)
		data[i] = (sample_t)orig[i] << 8;

	printf("converted\n");

	cache_mask = (unsigned char*)malloc (width * height);
	memset (cache_mask, 0, width * height);

	cache = (sample_t*)malloc (sizeof (sample_t) * width * height * 3);

	out_width = width;
	out_height = height;

	rows = (int*)malloc (sizeof (int) * out_height);
	for (i = 0; i < out_height; ++i)
		rows [i] = i * height / out_height;

	cols = (int*)malloc (sizeof (int) * out_width);
	for (i = 0; i < out_width; ++i)
		cols [i] = i * width / out_width;

	out_data = (sample_t*)malloc (sizeof (sample_t) * out_width * out_height * 3);
	assert (out_data != NULL);

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

	for (i = 0; i < 100; ++i)
		bw_process (width, height, out_data, data,
			    out_width, cols, out_height, rows,
			    cache_mask, cache,
			    0.5, 0.3, 0.2,
			    3, layers,
			    23.0, 0.1);

	printf("processed\n");

	output = (unsigned char*)malloc (out_width * out_height * 3);
	for (i = 0; i < out_width * out_height * 3; ++i)
		output [i] = out_data [i] >> 8;

	printf("converted\n");

	write_image ("/tmp/beidel2.png", out_width, out_height, output, 3, out_width * 3, IMAGE_FORMAT_PNG);

	return 0;
}
*/
