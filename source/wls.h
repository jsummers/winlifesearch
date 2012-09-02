// wls.h


struct wcontext;
void wlsErrorf(struct wcontext *ctx, TCHAR *fmt, ...);
void wlsMessagef(struct wcontext *ctx, TCHAR *fmt, ...);
void wlsStatusf(struct wcontext *ctx, TCHAR *fmt, ...);
void record_malloc(int func,void *m);
BOOL set_initial_cells(void);
void showcount(void);
