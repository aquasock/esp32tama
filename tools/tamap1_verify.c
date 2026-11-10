/*
 * tamap1_verify.c - deterministic verifier for first-gen Tamagotchi ROMs
 *
 * Loads a P1/P2/Angel/Mothra/Digimon ROM, runs it for a fixed number of
 * instructions, and reports what the LCD reached.
 *
 *   ./tamap1_verify <rom.bin>          one line per ROM
 *   ./tamap1_verify <rom.bin> dump     plus a 32x16 panel dump
 *
 * THE CLOCK IS VIRTUAL, ON PURPOSE. An earlier version used clock_gettime(),
 * and the same ROM gave different results depending on how the host happened
 * to be scheduled - one device looked blank under one sampling pattern and
 * drew 256 pixels under another. TamaLib paces itself through the HAL's
 * get_timestamp/sleep_until, so wall time feeds straight back into how far the
 * emulation gets. A monotonic counter advanced by sleep_until makes runs
 * reproducible, which is the only way a result here means anything.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tamalib.h"
static bool_t M[LCD_WIDTH][LCD_HEIGHT]; static bool_t I[ICON_NUM];
static timestamp_t g_now=0;
static timestamp_t ts(void){ return g_now; }
static void slp(timestamp_t d){ if((int32_t)(d-g_now)>0) g_now=d; }
static void sm(u8_t x,u8_t y,bool_t v){ if(x<LCD_WIDTH&&y<LCD_HEIGHT) M[x][y]=v; }
static void si(u8_t i,bool_t v){ if(i<ICON_NUM) I[i]=v; }
static void sf(u32_t f){(void)f;} static void pf(bool_t e){(void)e;}
static int hd(void){return 0;}
static bool_t le(log_level_t l){(void)l;return 0;}
static void lg(log_level_t l,char*b,...){(void)l;(void)b;}
static void hl(void){}
static void*ma(u32_t s){return malloc(s);} static void fr(void*p){free(p);}
static void us(void){}
static hal_t H={.malloc=ma,.free=fr,.halt=hl,.is_log_enabled=le,.log=lg,
    .sleep_until=slp,.get_timestamp=ts,.update_screen=us,
    .set_lcd_matrix=sm,.set_lcd_icon=si,.set_frequency=sf,.play_frequency=pf,.handler=hd};
static int lit(void){int n=0;for(int y=0;y<LCD_HEIGHT;y++)for(int x=0;x<LCD_WIDTH;x++)if(M[x][y])n++;return n;}
int main(int argc,char**argv){
    FILE*f=fopen(argv[1],"rb");
    if(!f){printf("OPEN FAIL    %s\n",argv[1]);return 1;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    u32_t w=n/2; u12_t*p=malloc(w*sizeof(u12_t));
    for(u32_t i=0;i<w;i++){int b0=fgetc(f),b1=fgetc(f); p[i]=(u12_t)(b1|((b0&0xF)<<8));}
    fclose(f);
    tamalib_register_hal(&H);
    /* Pick the segment map from the ROM itself. 8192 words means E0C6S48, which
     * covers Angel/Mothra/Digimon; of those, Digimon is the only one whose
     * segment order differs, and it is also the only 8192-word device whose
     * header word differs. Rather than sniff headers, take a name hint and fall
     * back to size: the caller knows what it handed us. */
    {
        const char *b = strrchr(argv[1], '/'); b = b ? b+1 : argv[1];
        if (strstr(b, "alienfev") || strstr(b, "stackch")) {
            fprintf(stderr, "%s: not supported (see docs/TAMAP1-DESKTOP.md)\n", b);
            return 2;
        }
        tamalib_set_segmap(strstr(b, "digimon") ? HW_SEGMAP_DIGIMON
                                               : HW_SEGMAP_TAMAGOTCHI);
    }
    if(tamalib_init(p,NULL,1000000)){printf("INIT FAIL\n");return 1;}
    tamalib_set_exec_mode(EXEC_MODE_RUN);
    state_t*s=tamalib_get_state();
    for(long i=0;i<6000000;i++) tamalib_step();
    printf("%-12s %5u words  pc=%04X halted=%d  lit=%3d  icons=",
        strrchr(argv[1],'/')?strrchr(argv[1],'/')+1:argv[1], w,(unsigned)*s->pc,
        (int)*s->cpu_halted, lit());
    for(int i=0;i<ICON_NUM;i++) putchar(I[i]?'1':'0');
    printf("\n");
    if(argc>2){
        for(int y=0;y<LCD_HEIGHT;y++){printf("    ");for(int x=0;x<LCD_WIDTH;x++)putchar(M[x][y]?'#':'.');putchar('\n');}
    }
    return 0;
}
