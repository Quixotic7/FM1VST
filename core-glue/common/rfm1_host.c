/* SPDX-License-Identifier: GPL-3.0-only */
/* rfm1_host.h natively: no folder (FS_ENOFOLDER), until FM1VST hosts the shared RemoteFM1 folder itself
 * (docs/VIRTUAL-CARTS.md, Q8). In an .rfm1 build the functions are the module's imports and this unit is empty. */
#include "rfm1_host.h"

#if !(defined FM1CORE_RFM1 && defined __wasm__)
int32_t rfm1_fs_request(int32_t op, const char *path, int32_t path_len, int64_t offset, void *buf, int32_t len)
{
    (void)op, (void)path, (void)path_len, (void)offset, (void)buf, (void)len;
    return FS_ENOFOLDER;
}
int32_t rfm1_fs_poll(int32_t id)
{
    (void)id;
    return FS_ENOFOLDER;
}
void rfm1_fs_cancel(int32_t id) { (void)id; }
int32_t rfm1_fs_changed(void) { return 0; }
#else
typedef int fm1_rfm1_host_imports;       /* (the imports: nothing to define here) */
#endif
