/* SPDX-License-Identifier: GPL-3.0-only */
/* The project store's flash for the JamesOS core (core_jamesos.c), in place of JamesOS's tests/project_device_host.c.
 * dinghy.c under DINGHY_DEVICE_SAVE_TEST includes "../../tests/project_device_host.c" (the device's SAVE screen and
 * project slots, instrument_project_device.c / _ui.c, on a host NOR) before them; core_jamesos.cmake maps that path to
 * this file (-ivfsoverlay). The shape is that file's (st_read, fl_erase4k, fl_write, flash_ok, the worker park), over
 * common/nor.c's image at the device's offsets: the project partition 0x97000 .. 0xB1000 (12 slots and the working
 * copy, two 4 KiB sectors each, project_store.c's format), so the slots live where the device's legacy store keeps
 * them. host_project_fail (a test's injected fault) stays 0. */
#define FL_IN(o,n,l,h) ((uint32_t)(o)>=(l) && (uint32_t)(o)<=(h) && (uint32_t)(n)<=(h)-(uint32_t)(o))
#define FM1CORE_JO_PROJ_LO 0x00097000u
#define FM1CORE_JO_PROJ_HI 0x000B1000u
static uint8_t flash_ok = 1;
static int host_project_fail;
static int flash_worker_park(void) { return 0; }
static int flash_worker_resume(void) { return 0; }
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (host_project_fail == 3 || !FL_IN(off, n, FM1CORE_JO_PROJ_LO, FM1CORE_JO_PROJ_HI)) return -1;
    return fm1nor_read(off, dst, n) ? -1 : 0;
}
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    if (host_project_fail == 1 || (off & 4095u) || !FL_IN(off, 4096u, FM1CORE_JO_PROJ_LO, FM1CORE_JO_PROJ_HI))
        return -1;
    *took = 1;
    return fm1nor_erase(off) ? -1 : 0;
}
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    if (host_project_fail == 2 || !FL_IN(off, n, FM1CORE_JO_PROJ_LO, FM1CORE_JO_PROJ_HI)) return -1;
    for (i = 0; i < n; i++)                     /* (NOR: a program only clears bits) */
        if ((fm1nor_image[off + i] & src[i]) != src[i]) return -1;
    return fm1nor_prog_span(off, src, n) ? -1 : 0;
}
