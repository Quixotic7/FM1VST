/* SPDX-License-Identifier: GPL-3.0-only */
/* Carnac OS's PCM waves (areas 0 and 1) and wavetable bank 0, Foundations, as the host builds carry them
 * (src/host/pcm_images_host.c, foundations_image_host.c: the bytes of src/dsp/pcm_area*.bin and foundations_bank.bin,
 * written by gen_pcm_waves.py / gen_foundations.py), unmodified: core_carnacos.c puts them into an empty flash */
#pragma clang diagnostic ignored "-Weverything"
#include "host/pcm_images_host.c"
#include "host/foundations_image_host.c"
