/*
 * conformance.c - CPU conformance test for the GPLB core
 *
 * Runs Klaus Dormann's 6502 functional test (6502_65C02_functional_tests,
 * GPL-3.0) against gplb.c. That suite exercises every documented opcode and
 * addressing mode and traps on a specific address when one is wrong.
 *
 * Build and run:
 *
 *   gcc -DGPLB_FLAT_RAM -O2 -o conformance conformance.c ../core/gplb.c
 *   ./conformance 6502_functional_test.bin
 *
 * Success is reaching $3469, which the suite labels "test passed, no errors".
 *
 * This exists because the CPU has to be trustworthy before peripheral work
 * means anything. It found four real bugs in a core that had been derived from
 * one validated bit-exactly against another emulator.
 */
#include "gplb.h"
#include <stdio.h>
#include <stdlib.h>
extern uint8_t gplb_flat[0x10000];
extern void gplb_force_pc(gplb *, uint16_t);
#define SUCCESS_PC 0x3469
int main(int argc,char**argv){
  FILE*f=fopen(argv[1],"rb"); fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
  if(n>0x10000)n=0x10000; if(fread(gplb_flat,1,n,f)!=(size_t)n){} fclose(f);
  gplb*c=gplb_new(gplb_flat,0x10000);
  gplb_force_pc(c,0x0400);
  long loop=-1,same=0;
  for(long i=0;i<200000000;i++){
    gplb_state s; gplb_snapshot(c,&s);
    if(s.pc==SUCCESS_PC){ printf("PASS: reached success trap at pc=%04X after %ld instructions\n",s.pc,i); return 0; }
    if((long)s.pc==loop && ++same>100){ printf("FAIL: trap at pc=%04X after %ld instructions\n",s.pc,i); return 1; }
    if((long)s.pc!=loop){loop=s.pc;same=0;}
    gplb_step(c);
  }
  printf("TIMEOUT\n"); return 2;
}
