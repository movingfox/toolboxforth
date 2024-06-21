#include <stdio.h>
#include <pico/stdlib.h>
#include <hardware/uart.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include "../tbforth.h"

struct dict *dict;

#define DICT_SECTORS ((sizeof (struct dict) / FLASH_SECTOR_SIZE) + 1)
// #define TOP_OF_DICT ((XIP_BASE + PICO_FLASH_SIZE_BYTES) - (DICT_SECTORS * FLASH_SECTOR_SIZE))
// #define TOP_OF_DICT_SECT ((PICO_FLASH_SIZE_BYTES / DICT_SECTORS) - DICT_SECTORS - 1)
#define TOP_OF_DICT_SECT (256*1024)
#define TOP_OF_DICT (XIP_BASE + TOP_OF_DICT_SECT)

void save_image (void) {
  //  printf("Saving: %d bytes (%d sectors) to addr 0x%08lX (sector 0x%04X)\n",
  //	 sizeof (struct dict), DICT_SECTORS, TOP_OF_DICT, TOP_OF_DICT_SECT);
  int32_t ints = save_and_disable_interrupts();
  flash_range_erase (TOP_OF_DICT_SECT, DICT_SECTORS * FLASH_SECTOR_SIZE);

  flash_range_program (TOP_OF_DICT_SECT, (uint8_t*) dict,
		       ((sizeof (struct dict) / FLASH_PAGE_SIZE)+1) * FLASH_PAGE_SIZE);
  restore_interrupts(ints);
}

int load_image (void) {
  memcpy (dict, (uint8_t*)TOP_OF_DICT, sizeof (struct dict));
}


uint8_t rxc(void) {
  return getc(stdin);
}

void txc(uint8_t c) {
  fputc(c, stdout);
  fflush(stdout);
}

void txs(char* s, int cnt) {
  fwrite(s,cnt,1,stdout);
  fflush(stdout);
}
#define txs0(s) txs(s,strlen(s))

enum {
  RP_X_FETCH32 = 200,
  RP_X_STORE32 = 201
};

tbforth_stat c_handle(void) {
  uint32_t *regw;
  uint8_t *regb;
  RAMC r2, r1 = dpop();
  switch(r1) {
  case RP_X_FETCH32 :
    regw = (uint32_t*)dpop();
    dpush(*regw);
    break;
  case RP_X_STORE32 :
    regw = (uint32_t*)dpop();
    *regw = (uint32_t)dpop();
    break;
  case MCU_COLD:
    {
      int32_t ints = save_and_disable_interrupts();
      flash_range_erase (TOP_OF_DICT_SECT, 1);
      restore_interrupts(ints);
    }
  case MCU_RESTART:
#define AIRCR_Register (*((volatile uint32_t*)(PPB_BASE + 0x0ED0C)))
    AIRCR_Register = 0x5FA0004;
    break;
  case OS_SECS:
    break;	
  case OS_MS:		/* milliseconds */
    sleep_ms(dpop());
    break;
  case OS_EMIT:			/* emit */
    txc(dpop()&0xff);
    break;
  case OS_KEY:			/* key */
    dpush((CELL)rxc());
    break;
  case OS_SAVE_IMAGE:			/* save image */
    save_image();
    break;
  }
  return U_OK;
}


static char linebuf[128];
char *line;
int get_line (char* buf, int max) {
  int idx = -1;
  int ch;
  do {
    ch = getchar_timeout_us(100);
    if (ch != PICO_ERROR_TIMEOUT) {
      putchar(ch);fflush(stdin);
      if (ch == 3) return 0;
      if (ch == 8) {
	if (idx > 0) idx--;
	continue;
      }
      if (ch != 13 && ch != '\n') {
	buf[++idx] = ch & 0xff;
      }
    }
  } while (ch != 13 && ch != '\n' && idx < (max-1));
  if (idx > -1)
    buf[idx+1] = '\0';
  return idx;
}
  
int interpret() {
  int stat;
  while (1) {
    txs0(" ok\r\n");
    if (get_line(linebuf,127) < 0) break;
    line = linebuf;
    if (line[0] == '\n' || line[0] == '\0') continue;
    stat = tbforth_interpret(line);
    switch(stat) {
    case E_NOT_A_WORD:
    case E_NOT_A_NUM:
      txs0("Huh? >>> ");
      txs(&tbforth_iram->tib[tbforth_iram->tibwordidx],tbforth_iram->tibwordlen);
      txs0(" <<< ");
      txs(&tbforth_iram->tib[tbforth_iram->tibwordidx + tbforth_iram->tibwordlen],
	  tbforth_iram->tibclen - 
	  (tbforth_iram->tibwordidx + tbforth_iram->tibwordlen));
      txs0("\r\n");
      return -1;
    case E_ABORT:
      txs0("Abort!:<"); txs0(line); txs0(">\n");
      return -1;
    case E_STACK_UNDERFLOW:
      txs0("Stack underflow!\n");
      return -1;
    case E_DSTACK_OVERFLOW:
      txs0("Stack overflow!\n");
      return -1;
    case E_RSTACK_OVERFLOW:
      txs0("Return Stack overflow!\n");
      return -1;
    case U_OK:
      break;
    default:
      txs0("Ugh\n");
      return -1;
    }
  }
  return 0;
}


int main() {
  int stat = -1;
  dict = malloc(sizeof(struct dict));

  setup_default_uart();
  stdio_init_all();

  load_image();

  // Check version & word_size & max_cells as a "signature" that we have
  // a valid image saved.
  //
  if (dict->version == DICT_VERSION &&
      dict->word_size == sizeof(CELL) &&
      dict->max_cells == MAX_DICT_CELLS)  {
    tbforth_init();
    tbforth_interpret("init");
  } else {
    // Bootstrap a raw Forth. You are going to have to send core.f, util.f, etc.
    //
    dict->version = DICT_VERSION;
    dict->word_size = sizeof(CELL);
    dict->max_cells = MAX_DICT_CELLS;
    dict->here =  0;
    dict->last_word_idx = 0;
    dict->varidx = 1;

    tbforth_bootstrap();
  }

  do {
    stat=interpret();
  } while (1);

}
 
