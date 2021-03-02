#ifndef PIT_SIM_H
#define PIT_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

int sim_add(char *service, int id, char *host, int port, bt_provider_t *bt);
int sim_connect(char *service, int id);
int sim_write(int fd, uint8_t *b, int n);
int sim_read(int fd, uint8_t *b, int n);

#ifdef __cplusplus
}
#endif

#endif
