// Samples Adreno 6xx hardware performance counters through the KGSL ioctls
// (/dev/kgsl-3d0 is world-accessible; no root). Counters are global to the GPU.
//   kgslperf <seconds> <group>:<countable>[=name] ...
// Prints each counter's increase per second over the window.
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define KGSL_IOC_TYPE 0x09
struct kgsl_perfcounter_get {
  unsigned int groupid, countable, offset, offset_hi, pad;
};
struct kgsl_perfcounter_put {
  unsigned int groupid, countable, pad[2];
};
struct kgsl_perfcounter_read_group {
  unsigned int groupid, countable;
  unsigned long long value;
};
struct kgsl_perfcounter_read {
  struct kgsl_perfcounter_read_group* reads;
  unsigned int count, pad[2];
};
#define IOCTL_KGSL_PERFCOUNTER_GET _IOWR(KGSL_IOC_TYPE, 0x38, struct kgsl_perfcounter_get)
#define IOCTL_KGSL_PERFCOUNTER_PUT _IOW(KGSL_IOC_TYPE, 0x39, struct kgsl_perfcounter_put)
#define IOCTL_KGSL_PERFCOUNTER_READ _IOWR(KGSL_IOC_TYPE, 0x3B, struct kgsl_perfcounter_read)

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s seconds group:countable[=name] ...\n", argv[0]);
    return 2;
  }
  const double seconds = atof(argv[1]);
  const int n = argc - 2;
  struct kgsl_perfcounter_read_group* reads = calloc(n, sizeof(*reads));
  unsigned long long* first = calloc(n, sizeof(*first));
  const char** names = calloc(n, sizeof(*names));
  int fd = open("/dev/kgsl-3d0", O_RDWR);
  if (fd < 0) {
    perror("open");
    return 1;
  }
  for (int i = 0; i < n; ++i) {
    unsigned g = 0, c = 0;
    if (sscanf(argv[i + 2], "%u:%u", &g, &c) != 2) return 2;
    const char* eq = strchr(argv[i + 2], '=');
    names[i] = eq ? eq + 1 : argv[i + 2];
    struct kgsl_perfcounter_get get = {g, c, 0, 0, 0};
    if (ioctl(fd, IOCTL_KGSL_PERFCOUNTER_GET, &get)) {
      fprintf(stderr, "get %u:%u (%s): %s\n", g, c, names[i], strerror(errno));
      return 1;
    }
    reads[i].groupid = g;
    reads[i].countable = c;
  }
  struct kgsl_perfcounter_read rd = {reads, (unsigned)n, {0, 0}};
  if (ioctl(fd, IOCTL_KGSL_PERFCOUNTER_READ, &rd)) {
    perror("read");
    return 1;
  }
  for (int i = 0; i < n; ++i) first[i] = reads[i].value;
  usleep((useconds_t)(seconds * 1e6));
  if (ioctl(fd, IOCTL_KGSL_PERFCOUNTER_READ, &rd)) {
    perror("read");
    return 1;
  }
  for (int i = 0; i < n; ++i) {
    printf("%-34s %14.0f /s\n", names[i], (double)(reads[i].value - first[i]) / seconds);
    struct kgsl_perfcounter_put put = {reads[i].groupid, reads[i].countable, {0, 0}};
    ioctl(fd, IOCTL_KGSL_PERFCOUNTER_PUT, &put);
  }
  close(fd);
  return 0;
}
