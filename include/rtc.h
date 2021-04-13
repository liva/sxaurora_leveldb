#pragma once
#include <stdio.h>

extern int rtc_index;
extern uint64_t rtc_array[3000000];
extern uint64_t rtc_total;
static inline uint64_t ve_get_rtc() {
  uint64_t ret;
  void *vehva = ((void *)0x000000001000);
  asm volatile("":::"memory");
  asm volatile("lhm.l %0,0(%1)":"=r"(ret):"r"(vehva));
  asm volatile("":::"memory");
  // the "800" is due to the base frequency of Tsubasa
  return ((uint64_t)1000 * ret) / 800;
}

//#define RTC
#ifdef RTC
#define get_rtctime(x) uint64_t x = ve_get_rtc();
#define record_rtctime(x) rtc_array[rtc_index] = ve_get_rtc() - x; rtc_index++;
#define show_rtctime(x) printf("t>%luns\n", ve_get_rtc() - x);
static inline void rtc_init() {
  rtc_total = ve_get_rtc();
  rtc_index = 0;
}
static inline void rtc_show() {
  rtc_total = ve_get_rtc() - rtc_total;
  printf(">>>start\n");
  for(int i = 0; i < rtc_index; i++) {
    printf("%lu\n", rtc_array[i]);
  }
  printf("===\n");
  uint64_t sum = 0;
  for(int i = 0; i < rtc_index; i++) {
    sum += rtc_array[i];
  }
  printf("%f\n", (sum * 100.0) / rtc_total);
  printf("end<<<\n");
}
#else
#define get_rtctime(x)
#define record_rtctime(x)
#define show_rtctime(x)
static inline void rtc_init() {}
static inline void rtc_show() {}
#endif
