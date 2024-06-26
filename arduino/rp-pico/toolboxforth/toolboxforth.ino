// RP Arduino
// https://arduino-pico.readthedocs.io/en/latest/
//
#include <Arduino.h>
#define HAVE_FS

#ifdef HAVE_FS
#include <LittleFS.h>
#define FLASH_FS LittleFS
#endif

extern "C" {
#include "tbforth.h"
  // #include "tbforth.img.h"
}

enum { RP_PWM_FREQ=200, RP_PWM_RANGE, RP_PWM_DUTY };
void load_ext_words () {
  tbforth_interpret("mark RP-HAL");
  tbforth_cdef("pwm-freq", RP_PWM_FREQ);
  tbforth_cdef("pwm-range", RP_PWM_RANGE);
  tbforth_cdef("pwm-duty", RP_PWM_DUTY);
}

SerialUART* HS[]  = { &Serial1, &Serial2};

tbforth_stat c_handle(void) {
  RAMC r1 = dpop();

  switch(r1) {
  case MCU_GPIO_MODE:
    pinMode(dpop(),dpop());
    break;
  case MCU_GPIO_READ:
    dpush(digitalRead(dpop()));
    break;
  case MCU_GPIO_WRITE:
    digitalWrite(dpop(), dpop());
    break;
  case MCU_UART_WRITE:
    r1 = dpop();
    HS[r1]->write((uint8_t)dpop());
    break;
  case MCU_UART_READ:
    r1 = dpop();
    dpush(HS[r1]->read());
    break;
  case MCU_UART_AVAIL:
    r1 = dpop();
    dpush(HS[r1]->available());
    break;
  case MCU_UART_END:
    r1 = dpop();
    HS[r1]->flush(); HS[r1]->end();
    break;
  case MCU_UART_BEGIN:
    {
      r1 = dpop();
      RAMC baud = dpop();
      RAMC rx  = dpop();
      RAMC tx  = dpop();
      HS[r1]->setRX(rx);
      HS[r1]->setTX(tx);
      HS[r1]->setPollingMode(true);
      HS[r1]->begin(baud);
    }
    break;
  case MCU_DELAY:
    delay(dpop());
    break;
  case MCU_COLD:
#ifdef HAVE_FS
    if (FLASH_FS.exists("/TBFORTH.IMG")) {
      FLASH_FS.remove("/TBFORTH.IMG");
    }
#endif
  case MCU_RESTART:
    rp2040.reboot();
    break;
  case MCU_WDT_CONFIG:
    rp2040.wdt_begin(r1);
    break;
  case MCU_WDT_RESET:
    rp2040.wdt_reset();
    break;

  case OS_MS:		/* milliseconds */
    dpush(millis());
    break;
  case OS_US:		/* microseconds */
    dpush(micros());
    break;
  case OS_EMIT:			/* emit */
    txc(dpop()&0xff);
    break;
  case OS_KEY:			/* key */
    dpush((CELL)rxc());
    break;
#ifdef HAVE_FS
  case OS_INCLUDE:
    (void)tbforth_next_word();    
    break;
  case OS_SAVE_IMAGE:			/* save image */
    {
      //      FLASH_FS.begin(true);
      File fp;
      uint32_t dict_size= (dict_here())*sizeof(CELL);
      fp = FLASH_FS.open("/TBFORTH.IMG", "w");
      fp.write(DICT_VERSION);
      fp.write((dict_size >> 24) & 0xFF);
      fp.write((dict_size >> 16) & 0xFF);
      fp.write((dict_size >> 8) & 0xFF);
      fp.write(dict_size & 0xFF);
      fp.write((uint8_t*)dict, dict_size);
      fp.close();
    }
    break;
#endif
  case RP_PWM_FREQ:
    analogWriteFreq(dpop());
    break;
  case RP_PWM_RANGE:
    analogWriteRange(dpop());
    break;
  case RP_PWM_DUTY:
    r1 = dpop();
    analogWrite(r1,dpop());
    break;
  }
  return U_OK;
}

/* A static variable for holding the line. */
static char line_read[128];


inline char rxc(){
  while (!Serial.available());
  return Serial.read();
}

void txc(uint8_t c) {
  Serial.write(c);
}
void txs(char* s, int cnt) {
  int i;
  
  for(i = 0;i < cnt;i++) {
    txc((uint8_t)s[i]);
  }
}
#define txs0(s) txs(s,strlen(s))

/* Read a string, and return a pointer to it.
   Returns NULL on EOF. */
char * rl_gets () {
  int i = 0;
  char c;
  while (i < 128) {
    c = rxc();
    txc(c);
    switch(c) {
    case 3:
      txs0(" ^C\n");
      line_read[0] = '\0';
      return line_read;
      break;
    case '\n':
    case '\r':
      line_read[i] = '\0';
      txc('\n');
      return line_read;
    case 8:
      if (i > 0) i--;
      break;
    default:
      line_read[i++] = c;
      break;
    }
  }
  line_read[127] = 0;
  return line_read;
}


char *line;
void console() {
  int stat;
  int16_t lineno = 0;
  while (1) {
    ++lineno;
    txs0(" ok\r\n");
    line=rl_gets(); // if (line==NULL) exit(0);
    if (line[0] == '\r' || line[0] == '\0') continue;
    stat = tbforth_interpret(line);
    switch(stat) {
    case E_NOT_A_WORD:
      txs0("\r\nHuh? >>> ");
      txs(&tbforth_iram->tib[tbforth_iram->tibwordidx],tbforth_iram->tibwordlen);
      txs0(" <<< ");
      txs(&tbforth_iram->tib[tbforth_iram->tibwordidx + tbforth_iram->tibwordlen],
	  tbforth_iram->tibclen - 
	  (tbforth_iram->tibwordidx + tbforth_iram->tibwordlen));
      txs0("\r\n");
      break;
    case E_ABORT:
      txs0("Abort!:<"); txs0(line); txs0(">\n");
      break;
    case E_STACK_UNDERFLOW:
      txs0("Stack underflow!\n");
      break;
    case E_DSTACK_OVERFLOW:
      txs0("Stack overflow!\n");
      break;
    case E_RSTACK_OVERFLOW:
      txs0("Return Stack overflow!\n");
      break;
    case U_OK:
      break;
    default:
      txs0("Ugh\n");
      break;
    }
  }
}

struct dict thedict;
struct dict  *dict = &thedict; // &flashdict;

void setup () {
  //  dict = &flashdict;
  dict->version = DICT_VERSION;
  dict->word_size = sizeof(CELL);
  dict->max_cells = MAX_DICT_CELLS;
  dict->here =  0;
  dict->last_word_idx = 0;
  dict->varidx = 1;

  Serial.begin(115200);
}
void bootstrap () {
  tbforth_init();
  tbforth_load_prims();
  tbforth_interpret("(create) : 1 iram ! (create) 1 iram ! here 2 iram + !  ;");
  OS_WORDS();
  MCU_WORDS();
}

void loop() {
#ifdef HAVE_FS
  if (FLASH_FS.begin() && FLASH_FS.exists("/TBFORTH.IMG")) {
    txs0("trying to read TBFORTH.IMG");
    File fp = FLASH_FS.open("/TBFORTH.IMG", "r");
    if (fp && fp.read() == DICT_VERSION) {
      uint32_t dsize = fp.read() << 24 | fp.read() << 16 | fp.read() << 8 | fp.read();
      fp.read((uint8_t*)dict, dsize);
      fp.close();
      tbforth_init();
      tbforth_interpret("init");
      tbforth_interpret("cr memory cr");
    } else {
      txs0("Can't load /TBFORTH.IMG...");
      bootstrap();
    }
  } else {
     bootstrap();
  }
#else
     bootstrap();
#endif  

  console();
}

