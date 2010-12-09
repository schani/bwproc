#ifndef __MARK_PROBST_BWPROC_H__
#define __MARK_PROBST_BWPROC_H__

typedef uint16_t sample_t;

typedef struct
{
	sample_t *curve;
	sample_t *mask;
} contrast_layer_t;

sample_t* make_logistic_contrast_curve (float v);
sample_t* bw_make_inverted_contrast_curve (void);
sample_t* bw_make_gamma_contrast_curve (float gamma);

sample_t* bw_make_sinusoidal_vignetting_curve (float start, float z, float exponent);

sample_t* bw_make_uniform_grain_buffer (float max);
sample_t* bw_make_gaussian_grain_buffer (float variance);

enum {
	BW_ROTATION_0,
	BW_ROTATION_90,
	BW_ROTATION_180,
	BW_ROTATION_270
};

void bw_make_rows_cols (int in_width, int in_height,
			int out_width, int out_height,
			int rotation,
			int **rows, int **cols,
			int *out_rotated_width, int *out_rotated_height,
			int *row_major);

void bw_process (int width, int height, sample_t *out_data, sample_t *in_data,
		 int num_cols, int *cols, int num_rows, int *rows,
		 unsigned char *cache_mask, sample_t *cache,
		 float red, float green, float blue,
		 int num_contrast_layers, contrast_layer_t *contrast_layers,
		 float tint_hue, float tint_amount);

void bw_process_layers (int width, int height, sample_t *out_data, sample_t *in_data,
			int num_cols, int *cols, int num_rows, int *rows,
			unsigned char *cache_mask, sample_t *cache,
			float red, float green, float blue,
			int num_layers, sample_t **layer_array,
			float tint_hue, float tint_amount);

void bw_process_layers_8 (int width, int height, uint8_t *out_data, sample_t *in_data,
			  int num_cols, int *cols, int num_rows, int *rows,
			  unsigned char *cache_mask, sample_t *cache,
			  float red, float green, float blue,
			  int num_layers, sample_t **layer_array,
			  float tint_hue, float tint_amount);

void bw_process_no_cache_8 (int width, int height,
			    uint8_t *out_data, int out_pixel_stride, int out_row_stride,
			    uint8_t *in_data, int in_pixel_stride, int in_row_stride,
			    int num_cols, int *cols, int num_rows, int *rows, int row_major,
			    float red, float green, float blue,
			    int num_contrast_layers, contrast_layer_t *contrast_layers,
			    float tint_hue, float tint_amount,
			    sample_t *vignetting_curve,
			    sample_t *grain_buffer);

void query_pixel (int width, int height, sample_t *out_pixel, sample_t *in_data,
		  int x, int y,
		  float red, float green, float blue,
		  int num_contrast_layers, contrast_layer_t *contrast_layers,
		  float tint_hue, float tint_amount,
		  sample_t *_mixed, sample_t *layered);

void query_pixel_layers (int width, int height, sample_t *out_pixel, sample_t *in_data,
			 int x, int y,
			 float red, float green, float blue,
			 int num_layers, sample_t **layer_array,
			 float tint_hue, float tint_amount,
			 sample_t *mixed, sample_t *layered);

#endif
