/* SPDX-License-Identifier: MPL-2.0 */
/* rfm1_ep.h: engine parameters (ep1), a firmware's sound parameters for its host (engine-params/SPEC.md, the protocol;
 * docs/RFM1.md "Engine parameters (ep1)"). One header a firmware, or its FM1VST glue, vendors (MPL-2.0, as rfm1_ext.h).
 *
 * The firmware gives the shim its tables and callbacks through fm1core_ep(); the shim (core-glue/common/rfm1_shim.c,
 * built with fm1_add_rfm1(... WANTS ep1)) builds the EP1D description blob, the EP1R record, the values after each
 * frame and the module's exports rfm1_ep / rfm1_ep_set / rfm1_ep_watch.
 *
 * Built for the FM-1, or for any host without the extension (FM1_HOST_EP1 undefined: the default), the header declares
 * nothing: a firmware's tables can stay in its source, unreferenced, and cost nothing. */
#ifndef RFM1_EP_H
#define RFM1_EP_H

#ifdef FM1_HOST_EP1
#include <stdint.h>

#define RFM1_EP_NONE ((int32_t)-32768)          /* get(): not on that layer's engine now (the record's 0x8000) */
enum { RFM1_EP_INT, RFM1_EP_FIXED, RFM1_EP_TABLE, RFM1_EP_NOTE, RFM1_EP_PERCENT, RFM1_EP_ONOFF };
#define RFM1_EPF_BIPOLAR 0x01u
#define RFM1_EPF_LIST    0x02u
#define RFM1_EPF_DYN     0x04u
#define RFM1_EPF_NOLOCK  0x08u
#define RFM1_EPF_SOUND   0x10u
#define RFM1_EPF_GLOBAL  0x20u
#define RFM1_EPF_RETIRED 0x40u
#define RFM1_EPL_BY_CHANNEL 0x01u               /* layer_flags: each layer plays one MIDI channel's notes */
enum { RFM1_EPG_NONE, RFM1_EPG_OSC, RFM1_EPG_FILTER, RFM1_EPG_ENV, RFM1_EPG_LFO, RFM1_EPG_MOD, RFM1_EPG_FX,
       RFM1_EPG_MIX, RFM1_EPG_PERFORM, RFM1_EPG_OTHER };

typedef struct {
    uint16_t pid;                               /* 1..0xFFFE, its meaning frozen forever (SPEC.md 2) */
    const char *name;                           /* <= 10 characters */
    int16_t min, max, def;
    uint8_t fmt, arg, flags, step, cc;          /* cc: 255 none */
    const char *unit;                           /* <= 4 characters, "" or 0 none */
    const char *const *names;                   /* TABLE: max - min + 1 names (min first); params with the same pointer
                                                 * and range share one table in the blob. Or 0: then text() */
    void (*text)(int32_t v, char *buf, uint32_t n);   /* TABLE without names: called for min..max when the blob is made */
} rfm1_ep_param_t;
typedef struct { uint8_t id; const char *name; uint8_t group; uint16_t pid[8]; } rfm1_ep_page_t;   /* pid 0: empty */
typedef struct { uint16_t id; const char *name; const rfm1_ep_page_t *pages; uint8_t npages; } rfm1_ep_engine_t;
typedef struct {
    const char *const *layer_names; uint8_t nlayers; uint8_t layer_flags;
    const rfm1_ep_engine_t *engines; uint8_t nengines;
    const rfm1_ep_param_t *params; uint16_t nparams;
    /* the callbacks, on the module's thread (between device milliseconds, never during audio) */
    uint32_t (*epoch)(void);                    /* optional (0): moves when the description must be made again. The shim
                                                 * compares layer()'s answers after every frame itself */
    void (*layer)(int layer, int *engine_index, int *midi_ch);   /* an index into engines[], -1 none (off); 0..15, -1 */
    int32_t (*get)(int layer, uint16_t pid);    /* RFM1_EP_NONE: not on that layer's engine now */
    void (*set)(int layer, uint16_t pid, int32_t v);   /* as the firmware's own editor writes it, quietly */
    /* optional: the parameter's name and value words as the firmware's own UI says them now (the watched cells);
     * 1 given, 0 none */
    int (*words)(int layer, uint16_t pid, char name[11], char text[11]);
} rfm1_ep_src_t;

/* the firmware's (or its glue's), asked once after init; NULL: no ep1 */
const rfm1_ep_src_t *fm1core_ep(void) __attribute__((weak));
/* while the shim's set() call runs: 1 a SET (the host's knob: an edit of the sound, as the firmware's own editor
 * marks it), 2 a step lock's write or its return (the sequencer's: not an edit of the sound); 0 otherwise. Draw
 * nothing, push no readout (the shim drops readouts pushed meanwhile) */
extern volatile int rfm1_ep_host;
#endif

#endif
