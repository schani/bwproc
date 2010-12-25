{
	int32_t sgray;
	sample_t gray;
	sample_t contrasted, grained;
	int layer;

	sgray = (int32_t)in [0] * red_factor + (int32_t)in [1] * green_factor + (int32_t)in [2] * blue_factor;

	if (sgray < 0)
		sgray = 0;
	sgray >>= RGB_SHIFT;
	gray = ((sgray > SAMPLE_MAX) ? SAMPLE_MAX : sgray);

#ifdef MIXED
	MIXED = gray;
#endif

	contrasted = gray;
	for (layer = 0; layer < num_contrast_layers; ++layer) {
		sample_t result = contrast_layers [layer].curve [contrasted >> CURVE_SHIFT];

		if (contrast_layers [layer].mask == NULL)
			contrasted = result;
		else {
			sample_t mask = contrast_layers [layer].mask [pixel];
			contrasted = SAMPLE_MUL (result, mask) + SAMPLE_MUL (contrasted, SAMPLE_MAX - mask);
		}

#ifdef LAYERED
		LAYERED [layer] = contrasted;
#endif
	}

	contrasted = SAMPLE_MUL (contrasted, vignetting);

	if (grain < SAMPLE_MAX / 2) {
		sample_t diff = SAMPLE_MAX / 2 - grain;
		if (contrasted < diff)
			grained = 0;
		else
			grained = contrasted - diff;
	} else {
		sample_t diff = grain - SAMPLE_MAX / 2;
		if (contrasted > SAMPLE_MAX - diff)
			grained = SAMPLE_MAX;
		else
			grained = contrasted + diff;
	}

	grained >>= CURVE_SHIFT;

	PIXEL_OUT [0] = tint_curve [grained * 3 + 0];
	PIXEL_OUT [1] = tint_curve [grained * 3 + 1];
	PIXEL_OUT [2] = tint_curve [grained * 3 + 2];
}
