// wls.h


void wlsError(char *m,int n);
void wlsWarning(char *m,int n);
void wlsMessage(char *m,int n);
int wlsQuery(char *m,int n);
void wlsStatus(char *msg);
void record_malloc(int func,void *m);
void showcount(int c);
