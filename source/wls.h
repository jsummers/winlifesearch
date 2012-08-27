// wls.h


void wlsError(TCHAR *m,int n);
void wlsWarning(TCHAR *m,int n);
void wlsMessage(TCHAR *m,int n);
int wlsQuery(TCHAR *m,int n);
void wlsStatus(TCHAR *msg);
void record_malloc(int func,void *m);
void showcount(int c);
