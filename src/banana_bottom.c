// Bottom panel of the Anbernic RG DS under bananaOS: shows an RGBA picture on DSI-1 through a DRM dumb buffer.
// (From uzdoom-bananaos, GPL-3.0-or-later; C version, top-down input.)
// The DRM device is the one SDL's KMSDRM video driver already holds open (found through /proc/self/fd), the
// panel gets its own free CRTC. Same idea as the ScummVM second-screen shim (GPL-3.0-or-later).
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define BLOG(...) do { fprintf(stderr, "BANANA-BOTTOM: " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

static struct { uint32_t handle; int fd; uint32_t pitch; size_t size; uint8_t *px; int W, H; int tried, ok; } B = { 0, -1, 0, 0, NULL, 0, 0, 0, 0 };

static int bottom_find_fd()
{
	char path[64], buf[256];
	for (int fd = 3; fd < 256; fd++)
	{
		snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
		ssize_t n = readlink(path, buf, sizeof buf - 1);
		if (n > 0) { buf[n] = 0; if (!strncmp(buf, "/dev/dri/card", 13)) return fd; }
	}
	return -1;
}

int banana_bottom_open(int *w, int *h)
{
	if (B.tried) { if (B.ok) { *w = B.W; *h = B.H; } return B.ok; }
	B.tried = 1;
	int fd = bottom_find_fd();
	if (fd < 0) { BLOG("no /dev/dri/card fd in this process"); return 0; }
	drmModeRes *res = drmModeGetResources(fd);
	if (!res) { BLOG("drmModeGetResources failed"); return 0; }
	const char *e = getenv("BANANA_PANEL_ID");
	int want = e ? atoi(e) : 1;
	drmModeConnector *conn = NULL;
	for (int i = 0; i < res->count_connectors; i++)
	{
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
		if (!c) continue;
		if (c->connector_type == 16 && (int)c->connector_type_id == want && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) conn = c;
		else drmModeFreeConnector(c);
	}
	if (!conn) { BLOG("DSI-%d not found", want); drmModeFreeResources(res); return 0; }
	drmModeModeInfo mode = conn->modes[0];
	int W = mode.hdisplay, H = mode.vdisplay;
	uint32_t conn_id = conn->connector_id, busy = 0;
	for (int i = 0; i < res->count_connectors; i++)
	{
		if (res->connectors[i] == conn_id) continue;
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
		if (!c) continue;
		if (c->encoder_id)
		{
			drmModeEncoder *en = drmModeGetEncoder(fd, c->encoder_id);
			if (en) { for (int k = 0; k < res->count_crtcs; k++) if (res->crtcs[k] == en->crtc_id) busy |= 1u << k; drmModeFreeEncoder(en); }
		}
		drmModeFreeConnector(c);
	}
	uint32_t possible = 0, cur = 0;
	for (int i = 0; i < conn->count_encoders; i++)
	{
		drmModeEncoder *en = drmModeGetEncoder(fd, conn->encoders[i]);
		if (!en) continue;
		possible |= en->possible_crtcs;
		if (conn->encoder_id == en->encoder_id) cur = en->crtc_id;
		drmModeFreeEncoder(en);
	}
	uint32_t crtc = 0;
	for (int k = 0; k < res->count_crtcs && !crtc; k++)
		if ((possible & (1u << k)) && !(busy & (1u << k)) && !(cur && res->crtcs[k] != cur)) crtc = res->crtcs[k];
	for (int k = 0; k < res->count_crtcs && !crtc; k++)
		if ((possible & (1u << k)) && !(busy & (1u << k))) crtc = res->crtcs[k];
	BLOG("fd %d connector %u crtc %u mode %dx%d", fd, conn_id, crtc, W, H);
	if (!crtc) { BLOG("no free CRTC"); return 0; }

	struct drm_mode_create_dumb cd = {};
	cd.width = W; cd.height = H; cd.bpp = 32;
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) { BLOG("create dumb failed"); return 0; }
	uint32_t handles[4] = { cd.handle, 0, 0, 0 }, pitches[4] = { cd.pitch, 0, 0, 0 }, offsets[4] = { 0, 0, 0, 0 }, fb = 0;
	uint32_t fmt = 0x34324258; // XBGR8888: bytes R,G,B,X
	if (drmModeAddFB2(fd, W, H, fmt, handles, pitches, offsets, &fb, 0))
	{
		fmt = 0x34324241; // ABGR8888
		if (drmModeAddFB2(fd, W, H, fmt, handles, pitches, offsets, &fb, 0)) { BLOG("addfb2 failed"); return 0; }
	}
	struct drm_mode_map_dumb md = {};
	md.handle = cd.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) { BLOG("map dumb failed"); return 0; }
	void *m = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
	if (m == MAP_FAILED) { BLOG("mmap failed"); return 0; }
	memset(m, 0, cd.size);
	uint32_t cids[1] = { conn_id };
	if (drmModeSetCrtc(fd, crtc, fb, 0, 0, cids, 1, &mode)) { BLOG("setcrtc failed"); return 0; }
	B.handle = cd.handle; B.fd = fd; B.pitch = cd.pitch; B.size = cd.size; B.px = (uint8_t *)m; B.W = W; B.H = H; B.ok = 1;
	drmModeFreeConnector(conn); drmModeFreeResources(res);
	BLOG("running: %dx%d pitch %u", W, H, B.pitch);
	*w = W; *h = H;
	return 1;
}

// rgba: w*h pixels, first row at the top. Scaled (nearest) to the panel.
void banana_bottom_present(const uint8_t *rgba, int w, int h)
{
	static int *xmap = NULL; static int xmap_w = 0, xmap_n = 0;
	if (!B.ok || !rgba || w <= 0 || h <= 0) return;
	if (!xmap || xmap_w != w || xmap_n != B.W)
	{
		free(xmap); xmap = (int *)malloc(sizeof(int) * B.W); xmap_w = w; xmap_n = B.W;
		for (int x = 0; x < B.W; x++) xmap[x] = (int)((int64_t)x * w / B.W);
	}
	for (int y = 0; y < B.H; y++)
	{
		const uint32_t *src = (const uint32_t *)(rgba + (size_t)((int64_t)y * h / B.H) * w * 4);
		uint32_t *dst = (uint32_t *)(B.px + (size_t)y * B.pitch);
		if (w == B.W) memcpy(dst, src, (size_t)B.W * 4);
		else for (int x = 0; x < B.W; x++) dst[x] = src[xmap[x]];
	}
}
