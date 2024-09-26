#define _GNU_SOURCE
#include <stddef.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <net/if.h>
// have to rename ioctl temporarily otherwise we get type errors due to "..."
#define ioctl ioctl_aside
#include <sys/ioctl.h>
#undef ioctl
#include <stdio.h>

int ioctl(int d, unsigned long request, char *argp) { 
  static unsigned char mac[6] = {0};
  static int (*ioctl_real)(int d, unsigned long request, ...) = NULL;
  if (ioctl_real == NULL) {
    ioctl_real = dlsym(RTLD_NEXT, "ioctl");
    char *macstr = getenv("MAC") ?: "00:00:00:00:00:00";
    for (int i=0; i<6; i++)
      sscanf(macstr+3*i,"%02hhx",&mac[i]); }
  int ret = ioctl_real(d, request, argp);
  if (d==3 && request==SIOCGIFHWADDR) {
    struct ifreq *ifr = (struct ifreq *) argp;
    for (int i = 0; i < 6; i++)
      ifr->ifr_hwaddr.sa_data[i] = mac[i]; }
  return ret; }
