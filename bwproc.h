#ifndef __MARK_PROBST_BWPROC_H__
#define __MARK_PROBST_BWPROC_H__

typedef uint16_t sample_t;

typedef struct
{
	sample_t *curve;
	sample_t *mask;
} contrast_layer_t;

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
