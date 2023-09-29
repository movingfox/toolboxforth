#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

// #define INLINE inline
#include "toolboxforth.h"

FILE *OUTFP;

#define CONFIG_IMAGE_FILE "tbforth.img"


struct dict *dict;
struct timeval start_tv;


uint8_t rxc(void) {
  return getchar();
}


 	
/* A static variable for holding the line. */
static char *line_read = (char *)NULL;

/* Read a string, and return a pointer to it.
   Returns NULL on EOF. */
char *
rl_gets ()
{
  /* If the buffer has already been allocated,
     return the memory to the free pool. */
  if (line_read)
    {
      free (line_read);
      line_read = (char *)NULL;
    }

  /* Get a line from the user. */
  line_read = readline ("");

  /* If the line has any text in it,
     save it on the history. */
  if (line_read && *line_read)
    add_history (line_read);
  return (line_read);
}

void rxgetline(char* str) {
  fgets(str,128,stdin);
}

void txc(uint8_t c) {
  fputc(c, OUTFP);
  fflush(OUTFP);
}

void txs(char* s, int cnt) {
  fwrite(s,cnt,1,OUTFP);
  fflush(OUTFP);
}
#define txs0(s) txs(s,strlen(s))

static FILE *cfp;
bool config_open_w(char* f) {
  cfp = fopen(f, "w+");
  return (cfp != NULL);
}

bool config_open_r(char* f) {
  cfp = fopen(f, "r");
  return (cfp != NULL);
}

bool config_write(char *src, uint16_t size) {
  fwrite((char*)src, size, 1, cfp);
  return 1;
}

bool config_read(void *dest) {
  uint16_t size;
  fscanf(cfp,"%d\n",(int*)&size);
  fread((char*)dest, size, 1, cfp);
  return 1;
}
bool config_close(void) {
  fclose(cfp);
  return 1;
}

void interpret_from(FILE *fp);

tbforth_stat c_handle(void) {
  RAMC r2, r1 = dpop();
  FILE *fp;
  static char buf[80*2];

  switch(r1) {
  case UF_MS:		/* milliseconds */
    {
      struct timeval tv;
      gettimeofday(&tv,0);
      tv.tv_sec -= start_tv.tv_sec;
      tv.tv_usec -= start_tv.tv_usec;
      r2 = (tv.tv_sec * 1000) + (tv.tv_usec/1000);
      dpush(r2);
    }
    break;
  case UF_EMIT:			/* emit */
    txc(dpop()&0xff);
    break;
  case UF_KEY:			/* key */
    dpush((CELL)rxc());
    break;
  case UF_SAVE_IMAGE:			/* save image */
    {
      int dict_size= (dict_here())*sizeof(CELL);
      char *s = tbforth_next_word();
      strncpy(buf, s, tbforth_iram->tibwordlen+1);
      buf[(tbforth_iram->tibwordlen)+1] = '\0';
      printf("Saving raw dictionary into %s\n", buf);
      fp = fopen(buf, "w+");
      fprintf(fp,"%ld\n",(int)(dict_here())*sizeof(CELL));
      fwrite(dict, dict_size,1,fp);
      fclose(fp);

      char *hfile = malloc(strlen(buf) + 3);
      strcpy(hfile,buf);
      strcat(hfile,".h");
      printf("Saving raw dictionary into %s\n", hfile);
      fp = fopen(hfile,"w");
      free(hfile);
      fprintf(fp,"struct dict flashdict = {%d,%d,%d,%d,%d,%d,{\n",
	      dict->version,dict->word_size,dict->max_cells,dict->here,dict->last_word_idx,
	      dict->varidx);
      int i;
      for(i = 0; i < dict_size-1; i++) {
	fprintf(fp, "0x%0X,",dict->d[i]);
      }
      fprintf(fp, "%0X",dict->d[dict_size-1]);
      fprintf(fp,"\n}};\n");
      fclose(fp);
    }
    break;
  case UF_READB:
    {
      char b;
      r2=dpop();
      read(r2,&b,1);
      dpush(b);
    }
    break;
  case UF_WRITEB:
    {
      char b;
      r2=dpop();
      b=dpop();
      dpush(write(r2,&b,1));
    }
    break;
  case UF_CLOSE:
    close(dpop());
    break;
  case UF_OPEN:
    {
      char *s;
      r1 = dpop();
      r2 = dpop();
      s = (char*)&tbforth_ram[r2+1];
      strncpy(buf,s, tbforth_ram[r2]);
      buf[tbforth_ram[r2]] = '\0';
      dpush(open(buf, r1));
    }
    break;
  case UF_INCLUDE:			/* include */
    {
      char *s = tbforth_next_word();
      strncpy(buf,s, tbforth_iram->tibwordlen+1);
      buf[tbforth_iram->tibwordlen] = '\0';
      printf("   Loading %s\n",buf);
      fp = fopen(buf, "r");
      if (fp != NULL) {
	interpret_from(fp);
	fclose(fp);
      } else {
	printf("File not found: <%s>\n", buf);
      }
    }  
    break;
  }
  return U_OK;
}


static char linebuf[128];
char *line;
void interpret_from(FILE *fp) {
  int stat;
  int16_t lineno = 0;
  while (!feof(fp)) {
    ++lineno;
    if (fp == stdin) txs0(" ok\r\n");
    if (fp == stdin) {
      line=rl_gets(); if (line==NULL) exit(0);
    } else {
      if (fgets(linebuf,128,fp) == NULL) break;
      line = linebuf;
    }
    if (line[0] == '\n' || line[0] == '\0') continue;
    stat = tbforth_interpret(line);
    switch(stat) {
    case E_NOT_A_WORD:
      fprintf(stdout," line: %d: ", lineno);
      txs0("Huh? >>> ");
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

#include <sys/types.h>
#include <sys/stat.h>

int main(int argc, char* argv[]) {

  dict = malloc(sizeof(struct dict));

  dict->version = DICT_VERSION;
  dict->word_size = sizeof(CELL);
  dict->max_cells = MAX_DICT_CELLS;
  dict->here =  0;
  dict->last_word_idx = 0;
  dict->varidx = 1;

  gettimeofday(&start_tv,0);


  tbforth_init();
  OUTFP = stdout;
  if (argc < 2) {
    FILE *fp;
    tbforth_load_prims();
    printf("   Loading ./core.f\n");
    fp = fopen("./core.f", "r");
    if (fp != NULL) 
      interpret_from(fp);
    fclose(fp);
    printf("   Loading ./posix.f\n");
    fp = fopen("./posix.f", "r");
    if (fp != NULL) 
      interpret_from(fp);
    fclose(fp);
    printf("   Loading ./util.f\n");
    fp = fopen("./util.f", "r");
    if (fp != NULL) 
      interpret_from(fp);
    fclose(fp);
  } else {
    if (config_open_r(argv[1])) {
      if (!config_read(dict))
	exit(1);
      config_close();
    }
  }
  tbforth_interpret("init");
  interpret_from(stdin);

  return 0;
}
