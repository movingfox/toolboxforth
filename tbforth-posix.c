#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>
#include <poll.h>
#include <sys/socket.h>
#include <netdb.h>
#include "tbforth.h"
#include <errno.h>

FILE *OUTFP;
FILE *INFP;

#define CONFIG_IMAGE_FILE "tbforth.img"


struct dict *dict;
struct timeval start_tv;


uint8_t rxc(void) {
  return getc(INFP);
}


 	
/* A static variable for holding the line. */
static char *line_read = (char *)NULL;

/* Read a string, and return a pointer to it.
   Returns NULL on EOF. */
char *rl_gets ()
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
  (void)fgets(str,128,stdin);
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
  fprintf(cfp,"%d\n", (int)size);
  fwrite((char*)src, size, 1, cfp);
  return 1;
}

bool config_read(char *dest) {
  int size;
  (void)fscanf(cfp,"%d\n",(int*)&size);
   (void)fread((char*)dest, size, 1, cfp);
  return 1;
}
bool config_close(void) {
  fclose(cfp);
  return 1;
}



void load_ext_words () {
  OS_WORDS();
  MCU_WORDS();
}

tbforth_stat c_handle(void) {
  RAMC r2, r1 = dpop();
  FILE *fp;
  static char buf[1024];
  switch(r1) {
  case OS_POLL:
    {
      int ret_poll;
      int ms = dpop();
      int event = dpop();
      int fdcnt = dpop();
      
      struct pollfd input[fdcnt];
      
      for (int i=0; i < fdcnt; i++) {
	input[i].fd = dpop();
	input[i].events = event;
      }
      ret_poll = poll(input, fdcnt, ms);
      dpush(ret_poll);
      if (ret_poll > 0) {
	for (int i=0; i < fdcnt; i++) {
	  dpush(input[i].revents);
	}
      }
    }
    break;
  case OS_SECS:
    {
      time_t now;
      time(&now);
      dpush(now);
    }
    break;	
  case OS_MS:		/* milliseconds */
    {
      struct timeval tv;
      gettimeofday(&tv,0);
      tv.tv_sec -= start_tv.tv_sec;
      tv.tv_usec -= start_tv.tv_usec;
      r2 = (tv.tv_sec * 1000) + (tv.tv_usec/1000);
      dpush(r2);
    }
    break;
  case OS_EMIT:			/* emit */
    txc(dpop()&0xff);
    break;
  case OS_KEY:			/* key */
    dpush((CELL)rxc());
    break;
  case OS_SAVE_IMAGE:			/* save image */
    {
      int dict_size= (dict_here());
      char *s = tbforth_next_word();
      strncpy(buf, s, tbforth_iram->tibwordlen+1);
      buf[(tbforth_iram->tibwordlen)+1] = '\0';

      char *hfile = malloc(strlen(buf) + 3);
      strcpy(hfile,buf);

      config_open_w(hfile);
      config_write((char*)dict, dict_size*4);
      config_close();
      
      strcat(hfile,".h");
      printf("Saving dictionary into %s\n", hfile);
      fp = fopen(hfile,"w");
      free(hfile);
      fprintf(fp,"struct dict flashdict = {%d,%d,%d,%d,%d,%d,{\n",
	      dict->version,dict->word_size,dict->max_cells,dict->here,dict->last_word_idx,
	      dict->varidx);
      int i;
      printf("dictionary size = %d\n", dict_size);
      for(i = 0; i < dict_size-1; i++) {
	fprintf(fp, "0x%0X,",dict->d[i]);
      }
      fprintf(fp, "0x%0X",dict->d[dict_size-1]);
      fprintf(fp,"\n}};\n");
      fclose(fp);
    }
    break;
  case OS_READB:
    {
      int b;
      r2=dpop();
      if (read(r2,&b,1) == -1) {
	dpush(-1);
      } else {
	dpush(b);
      }
    }
    break;
  case OS_WRITEB:
    {
      char b;
      r2=dpop();
      b=dpop();
      dpush(write(r2,&b,1));
    }
    break;
  case OS_READBUF:
    {
      char *buf;
      r1=dpop(); 		/* fd */
      r2=dpop();		/* length */
      RAMC r3=dpop();		/* block */
      if (r3 & 0x80000000) {
	buf = (char*)&tbforth_ram[(r3 & 0x7FFFFFFF)];
      } else {
	buf = (char*)&tbforth_dict[r3];
      }
      dpush(read(r1, buf, r2));
    }
    break;
  case OS_WRITEBUF:
    {
      char *buf;
      r1=dpop(); 		/* fd */
      r2=dpop();		/* length */
      RAMC r3=dpop();		/* block */
      if (r3 & 0x80000000) {
	buf = (char*)&tbforth_ram[(r3 & 0x7FFFFFFF)];
      } else {
	buf = (char*)&tbforth_dict[r3];
      }
      dpush(write(r1, buf, r2));
    }
    break;
  case OS_CLOSE:
    close(dpop());
    break;
  case OS_TCP_CONN:
    {
      int sock;
      char *s;
      char port_s[80];
      r1 = dpop();              /* port */
      snprintf(port_s, 80, "%d", r1);
      r2 = dpop();              /* hostname */
      if (r2 & 0x80000000) {
	s = (char*)&tbforth_ram[(r2 & 0x7FFFFFFF) +1];
	strncpy(buf,s,tbforth_ram[r2 & 0x7FFFFFFF]);
	buf[tbforth_ram[r2 & 0x7FFFFFFF]] = '\0';
      } else {
	s = (char*)&tbforth_dict[r2+1];
	strncpy(buf,s, tbforth_dict[r2]);
	buf[tbforth_dict[r2]] = '\0';
      }
      struct addrinfo hints, *addrs;  
      memset(&hints, 0, sizeof(struct addrinfo));
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      if (0 == getaddrinfo(s, port_s, &hints, &addrs)) {
	sock = socket(addrs->ai_family, addrs->ai_socktype, addrs->ai_protocol);
	if (sock >= 0 ) {
	  if (connect(sock, addrs->ai_addr, addrs->ai_addrlen) >= 0) {
	    dpush((uint64_t)addrs >> 32);
	    dpush((uint64_t)addrs & 0xFFFFFFFF);
	    dpush(sock);
	    //	    printf("addr=%lx\n", (uint64_t)addrs);
	    goto TCP_OK;
	  }
	}
      }
    }
    dpush(-1);
  TCP_OK:
    break;
  case OS_TCP_DISCONN:
    {
      r1 = dpop();
      close(r1);
      uint64_t r164 = (uint64_t)dpop() & 0xFFFFFFFF;
      uint64_t r264 = (uint64_t)dpop();
      struct addrinfo *addrs = (struct addrinfo*) (r264 << 32 | r164);
      //      printf("r2=%lx, r1=%lx, addr=%lx\n", r264, r164, (uint64_t)addrs);
      freeaddrinfo(addrs);
    }
    break;
  case OS_DELETE:
    {
      char *s;
      r2 = dpop();
      if (r2 & 0x80000000) {
	r2 &=0x7FFFFFFF;
	s = (char*)&tbforth_ram[r2+1];
	strncpy(buf,s, tbforth_ram[r2]);
	buf[tbforth_ram[r2]] = '\0';
      } else {
	s = (char*)&tbforth_dict[r2+1];
	strncpy(buf,s, tbforth_dict[r2]);
	buf[tbforth_dict[r2]] = '\0';
      }
      dpush(unlink(buf));
    }
    break;
  case OS_OPEN:
    {
      char *s;
      r1 = dpop();
      r2 = dpop();
      if (r2 & 0x80000000) {
	r2 &=0x7FFFFFFF;
	s = (char*)&tbforth_ram[r2+1];
	strncpy(buf,s, tbforth_ram[r2]);
	buf[tbforth_ram[r2]] = '\0';
      } else {
	s = (char*)&tbforth_dict[r2+1];
	strncpy(buf,s, tbforth_dict[r2]);
	buf[tbforth_dict[r2]] = '\0';
      }
      
      dpush(open(buf, r1));
    }
    break;
  case OS_SEEK:
    {
      r1=dpop();		/* fd */
      r2=dpop();		/* offset */
      dpush(lseek(r1,r2, SEEK_SET));
    }
    break;
  case OS_INCLUDE:			/* include */
    {
      int interpret_from(FILE *fp);
      char *s = tbforth_next_word();
      strncpy(buf,s, tbforth_iram->tibwordlen+1);
      buf[tbforth_iram->tibwordlen] = '\0';
      printf("   Loading %s\n",buf);
      fp = fopen(buf, "r");
      if (fp != NULL) {
	int stat = interpret_from(fp);
	fclose(fp);
	INFP = stdin;
	if (stat != 0)
	  return E_ABORT;
      } else {
	printf("File not found: <%s>\n", buf);
	return E_ABORT;
      }
    }  
    break;
  }
  return U_OK;
}


static char linebuf[128];
char *line;
int interpret_from(FILE *fp) {
  int stat;
  int16_t lineno = 0;
  INFP = fp;
  while (!feof(fp)) {
    ++lineno;
    if (fp == stdin) txs0(" ok\r\n");
    if (fp == stdin) {
      line=rl_gets(); if (line==NULL) return (0);
    } else {
      if (fgets(linebuf,128,fp) == NULL) break;
      line = linebuf;
    }
    if (line[0] == '\n' || line[0] == '\0') continue;
    stat = tbforth_interpret(line);
    switch(stat) {
    case E_NOT_A_WORD:
    case E_NOT_A_NUM:
      if (fp != stdout)
	fprintf(stdout," line: %d: ", lineno);
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

#include <sys/types.h>
#include <sys/stat.h>

int load_f (char* fname) {
  int stat = -1;
  FILE *fp;
  printf("   Loading %s\n",fname);
  fp = fopen(fname, "r");
  if (fp != NULL)  {
    stat=interpret_from(fp);
  }
  fclose(fp);
  return stat;
}


const char* history_file = ".tbforth_history";
int main(int argc, char* argv[]) {
  int stat = -1;
  dict = malloc(sizeof(struct dict));
  dict->version = DICT_VERSION;
  dict->word_size = sizeof(CELL);
  dict->max_cells = MAX_DICT_CELLS;
  dict->here =  0;
  dict->last_word_idx = 0;
  dict->varidx = 1;

  gettimeofday(&start_tv,0);

  read_history(history_file);

  tbforth_init();

  OUTFP = stdout;
  INFP = stdin;

  if (argc < 2) {
    tbforth_load_prims();
    stat = load_f("./core.f");
    load_ext_words();
    if (stat == 0) stat = load_f("./util.f");
  } else {
    if (config_open_r(argv[1])) {
      if (!config_read((char*)dict))
	exit(1);
      config_close();
      stat = 0;
    }
  }
  if (stat == 0) stat=tbforth_interpret("init");
  if (stat == 0) stat=tbforth_interpret("cr memory cr");
  do {
    INFP = stdin;
    stat=interpret_from(stdin);
  } while (stat != 0);

  write_history(history_file);
  return stat;
}
