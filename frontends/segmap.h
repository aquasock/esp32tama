#ifndef SEGMAP_H
#define SEGMAP_H
#include <stdint.h>

#define SEG_COUNT     970
#define SEG_RUN_COUNT 5366
#define SEG_W         292
#define SEG_H         300
#define SEG_BBOX_X0   619.939
#define SEG_BBOX_Y0   850.125
#define SEG_BBOX_W    1219.684
#define SEG_BBOX_H    1254.576
#define SEG_SCALE     0.239043825

typedef struct { uint8_t byte, bit; uint16_t run_start, run_count; } seg_entry;
typedef struct { uint16_t y, x0, x1; } seg_run;

extern const seg_entry seg_map[SEG_COUNT];
extern const seg_run   seg_runs[SEG_RUN_COUNT];
#endif
