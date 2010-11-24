{
	int32_t sgray;
	sample_t gray;
	sample_t contrasted, inverted;
	sample_t s, v, p, q, t;
	sample_t r, g, b;
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
	
	inverted = SAMPLE_MAX - contrasted;

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

	PIXEL_OUT [0] = SAMPLE_MUL (r, saturation) + SAMPLE_MUL ((SAMPLE_MAX - inverted), (SAMPLE_MAX - saturation));
	PIXEL_OUT [1] = SAMPLE_MUL (g, saturation) + SAMPLE_MUL ((SAMPLE_MAX - inverted), (SAMPLE_MAX - saturation));
	PIXEL_OUT [2] = SAMPLE_MUL (b, saturation) + SAMPLE_MUL ((SAMPLE_MAX - inverted), (SAMPLE_MAX - saturation));
}
