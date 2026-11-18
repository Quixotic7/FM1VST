/* SPDX-License-Identifier: GPL-3.0-only */
/* Stallion's HAL beyond SLOOP's (core_stallion.c sets FM1CORE_SL_STALLION; sloop_firmware.h includes this right after
 * sloop_hal.h). What the device's headers give Stallion's felucca.c that the SLOOP-family HAL does not:
 *   - hal/fm1_clk.h: the boot's clock and flash-line measurements (main.c clk_boot, the console's clk): the loop
 *     timings of a 160 MHz core (GLO's CPU column reads "160 MHz"), a flash line's fill as the cache's hit (no miss)
 *   - hal/fm1_input.h's fm1_an (KEY TOUCH, after ESTAMPIE: the scan tick's converter): the configuration main.c sets
 *     (fm1_an_set) and the pot's last reading, which is the panel's MASTER (sloop_fw.c fm1core_sl_fw_master); no key
 *     is measured (the panel has no analog keys: KEY TOUCH reads no touch, the keys play as switches)
 *   - hal/fm1_adc.h's short conversion (FM1_ADC_CFG)
 *   - hal/fm1_flash.h's FL_XIP and FL_DATA_LO (the console's and clk_boot's read window)
 * hal/fm1_hot.h (HOT_FAR, TABLE_HOT: the render in RAM) is core.h's host default already. */
#define FM1_ADC_CFG(div, delay) ((uint32_t)((div) & 7u) | 8u | ((uint32_t)((delay) & 15u) << 12))
#define FM1_ADC_CFG_FAST FM1_ADC_CFG(7, 2)

static struct {
    volatile uint16_t cfg;                         /* FM1_ADC_CFG; 0: off, the main loop owns the converter */
    uint8_t pend, col, rowbit;
    uint8_t ch, driven, phase;
    volatile uint16_t master;                      /* the pot's last reading (0: none yet): the panel's MASTER */
    uint32_t timeouts;
} fm1_an = {0, 0, 0, 0, 0, 0, 0, 724u, 0};
static void fm1_an_set(uint32_t cfg) { fm1_an.cfg = (uint16_t)cfg; }

static uint32_t fm1_clk_loop_ticks(int ram, uint32_t adds, uint32_t n)   /* TIMER4 ticks (24 MHz) for n turns */
{
    (void)ram;
    return (uint32_t)((uint64_t)n * (adds + 2u) * 24u / 160u);   /* (one add a cycle at 160 MHz, 2 for the loop) */
}
static uint32_t fm1_clk_fill_ticks(uint32_t a, uint32_t n, uint32_t stride)
{
    (void)a;
    (void)stride;
    return n * 24u / 160u;                         /* (a cycle a read: a miss costs no more than a hit) */
}

#ifndef FL_DATA_LO
#define FL_DATA_LO 0x00097000u                     /* (hal/fm1_flash.h: Felucca's main store) */
#endif
#ifndef FL_XIP
#define FL_XIP(off) FM1_XIP(off)
#endif
