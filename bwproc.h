#ifndef __MARK_PROBST_BWPROC_H__
#define __MARK_PROBST_BWPROC_H__

typedef uint16_t sample_t;

typedef struct
{
	sample_t *curve;
	sample_t *mask;
} contrast_layer_t;

sample_t* make_logistic_contrast_curve (float v);
sample_t* bw_make_inverted_contrast_curve (float min, float max);
sample_t* bw_make_gamma_contrast_curve (float gamma);

sample_t* bw_make_sinusoidal_vignetting_curve (float start, float z, float exponent);

sample_t* bw_make_uniform_grain_buffer (float max);
sample_t* bw_make_gaussian_grain_buffer (float variance);

void bw_process_no_cache_8 (int width, int height,
			    uint8_t *out_data, int out_pixel_stride, int out_row_stride,
			    uint8_t *in_data, int in_pixel_stride, int in_row_stride,
			    float red, float green, float blue,
			    int num_contrast_layers, contrast_layer_t *contrast_layers,
			    float tint_hue, float tint_amount,
			    sample_t *vignetting_curve,
			    sample_t *grain_buffer);

#endif
