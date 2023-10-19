/*
  toolboxForth - A tiny ROMable 16/32-bit FORTH-like scripting language
          for any C99 compiler. From POSIX to microcontrollers.
	  Version 3.0. Based on uForth...

  Copyright � 2009-2023 Todd Coram, todd@maplefish.com, USA.

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:
  
  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "tbforth.h"

#define min(a,b) ((a < b) ? a : b)


#define BYTES_PER_CELL sizeof(CELL)

#define DICT_HEADER_WORDS	((sizeof(struct dict)/(sizeof (CELL))) - MAX_DICT_CELLS)
#define DICT_INFO_SIZE_BYTES	(sizeof(CELL)*DICT_HEADER_WORDS)

#define IRAM_BYTES (RAMC)(sizeof(struct tbforth_iram))/sizeof(RAMC)
#define URAM_HDR_BYTES (RAMC)(sizeof(struct tbforth_uram))/sizeof(RAMC)
#define URAM_START (IRAM_BYTES+URAM_HDR_BYTES)
#define VAR_ALLOTN(n) (IRAM_BYTES+URAM_HDR_BYTES+dict_incr_varidx(n))
#define VAR_ALLOT_1() (IRAM_BYTES+URAM_HDR_BYTES+dict_incr_varidx(1))

#define PAD_ADDR (IRAM_BYTES+URAM_HDR_BYTES)
#define PAD_STR (char*)&tbforth_ram[PAD_ADDR+1]
#define PAD_STRLEN tbforth_ram[PAD_ADDR]

#ifdef USE_LITTLE_ENDIAN
# define BYTEPACK_FIRST(b) b
# define BYTEPACK_SECOND(b) ((CELL)b)<<8
#else
# define BYTEPACK_FIRST(b) ((CELL)b)<<8
# define BYTEPACK_SECOND(b) b
#endif

#ifdef SUPPORT_FLOAT_FIXED
#include <math.h>
#endif


tbforth_stat interpret_tib();

CELL *tbforth_dict;			/* treat dict struct like array */
abort_t _tbforth_abort_request;	/* for emergency aborts */

struct tbforth_iram *tbforth_iram;
struct tbforth_uram *tbforth_uram;


/*
  Words must be under 64 characters in length
*/
#define WORD_LEN_BITS 0x3F  
#define IMMEDIATE_BIT (1<<7)
#define PRIM_BIT     (1<<6)


RAMC tbforth_ram[TOTAL_URAM_CELLS];

// LIT must be 1!
//
enum { 
  LIT=1, COLD, DLIT, ABORT, DEF, IMMEDIATE, URAM_BASE_ADDR,  RPICK,
  HERE, RAM_BASE_ADDR, INCR, DECR,
  ADD, SUB, MULT, DIV, MULT_DIV, MOD, AND, JMP, JMP_IF_ZERO, SKIP_IF_ZERO, EXIT,
  OR, XOR, LSHIFT, RSHIFT, EQ_ZERO, EQ, DROP, DUP,  SWAP, OVER, ROT,
  NEXT, CNEXT,  EXEC, LESS_THAN,
  INVERT, COMMA, DCOMMA, RPUSH, RPOP, FETCH, STORE,  DICT_FETCH, DICT_STORE,
  COMMA_STRING,
  VAR_ALLOT, CALLC,   FIND, FIND_ADDR, CHAR_APPEND, CHAR_STORE, CHAR_FETCH, DCHAR_FETCH,
  BYTE_COPY, BYTE_CMP,
  POSTPONE, _CREATE, PARSE_NUM,
  INTERP, SUBSTR, NUM_TO_STR, UNUM_TO_STR,
  LAST_PRIMITIVE
};

void tbforth_cdef (char* name, int val) {
  char n[80];
  snprintf(n, 80, ": %s %d cf ;", name, val);
  tbforth_interpret(n);
}

RAMC parse_num(char *s, uint8_t base) {
  char *p = s;
  char *endptr;
#ifdef SUPPORT_FLOAT_FIXED
  double f;
  while (*p != '\0' && *p != ' ' && *p != '.') ++p;
  if (*p == '.') {		/* got a dot, must be floating */
    f = strtod(s,NULL);
    return (RAMC)FIXED_PT_MULT(f);
  }
#endif
  p = s;
  int curbase = tbforth_uram->base;
  switch (*p) {
  case '%':
    tbforth_uram->base = 2; p++; break;
  case '$':
    tbforth_uram->base = 16; p++; break;
  case '#':
    tbforth_uram->base = 10; p++; break;
  }
  // Treat base 10 as 0 so we can handle 0xNN as well as decimal.
  //
  RAMC num = strtol(p,&endptr, tbforth_uram->base == 10 ? 0 : tbforth_uram->base);
  tbforth_uram->base = curbase;
  if (*endptr != 32  && *endptr != '\0' && *endptr != '\n' && *endptr != '\r') {
    tbforth_abort_request(ABORT_NAW);
  }
  return num;
}

char* i32toa(int32_t value, char* result, int32_t base) {
  // check that the base if valid
  if (base < 2 || base > 36) { *result = '\0'; return result; }
  
  char *ptr = result, *ptr1 = result, tmp_char;
  int32_t tmp_value = value;
  
  do {
    tmp_value = value;
    value /= base;
    *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
  } while ( value );
  
  // Apply negative sign
  if (tmp_value < 0) *ptr++ = '-';
  *ptr-- = '\0';
  while(ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr-- = *ptr1;
    *ptr1++ = tmp_char;
  }
  return result;
}

char* u32toa(uint32_t value, char* result, int32_t base) {
  // check that the base if valid
  if (base < 2 || base > 36) { *result = '\0'; return result; }
  
  char *ptr = result, *ptr1 = result, tmp_char;
  uint32_t tmp_value = value;
  
  do {
    tmp_value = value;
    value /= base;
    *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
  } while ( value );
  
  *ptr-- = '\0';
  while(ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr-- = *ptr1;
    *ptr1++ = tmp_char;
  }
  return result;
}

CELL find_word(char* s, uint8_t len, RAMC* addr, bool *immediate, char *prim);

/*
 Every entry in the dictionary consists of the following cells:
  [index of previous entry]  
  [flags, < 128 byte name byte count]
  [name [optional pad byte]... [ data ..]
*/
void make_word(char *str, uint8_t str_len) {
  CELL my_head = dict_here();

  dict_append(dict->last_word_idx);
  dict_append(str_len);
  dict_append_string(str, str_len);
  dict_set_last_word(my_head);

}

void make_immediate(void) {
  dict_write((dict->last_word_idx+1), tbforth_dict[dict->last_word_idx+1]|IMMEDIATE_BIT);
}

char next_char(void) {
  if (tbforth_iram->tibidx >= tbforth_iram->tibclen) return 0;
  return tbforth_iram->tib[tbforth_iram->tibidx++];
}
#define EOTIB() (tbforth_iram->tibidx >= tbforth_iram->tibclen)
#define CURR_TIB_WORD &(tbforth_iram->tib[tbforth_iram->tibwordidx])
#define CLEAR_TIB() (tbforth_iram->tibidx=0, tbforth_iram->tibclen=0, tbforth_iram->tibwordlen=0, tbforth_iram->tibwordidx=0)

char* tbforth_next_word (void) {
  char nc; uint8_t cnt = 0;
  do { nc = next_char(); } while (!EOTIB() && isspace(nc));
  if (EOTIB()) {
    return "";
  }
  cnt=1;
  tbforth_iram->tibwordidx = (tbforth_iram->tibidx)-1;
  while(!isspace(next_char()) && !EOTIB()) ++cnt;
  tbforth_iram->tibwordlen = cnt;
  return &(tbforth_iram->tib[tbforth_iram->tibwordidx]);
}

void tbforth_abort(void) {
  if (tbforth_iram->state == COMPILING) {
    dict_append(ABORT);
  }
  tbforth_iram->state = 0;
  tbforth_abort_clr();
  tbforth_uram->ridx = tbforth_uram->rsize + tbforth_uram->dsize;
  tbforth_uram->didx = -1;
}

void store_prim(char* str, CELL val) {
  make_word(str,strlen(str));
  dict_append(val);
  dict_append(EXIT);
  dict_write((dict->last_word_idx+1), tbforth_dict[dict->last_word_idx+1]|PRIM_BIT);
}

typedef tbforth_stat (*wfunct_t)(void);


void tbforth_init(void) {
  tbforth_dict = (CELL*)dict;
  tbforth_iram = (struct tbforth_iram*) tbforth_ram;
  tbforth_iram->state = 0;
  tbforth_iram->total_ram = TOTAL_URAM_CELLS;
  tbforth_uram = (struct tbforth_uram*)
    (tbforth_ram + sizeof(struct tbforth_iram));
  tbforth_uram->len = TOTAL_URAM_CELLS - sizeof(struct tbforth_iram);
  tbforth_uram->dsize = DS_CELLS;
  tbforth_uram->rsize = RS_CELLS;
  tbforth_uram->ridx = DS_CELLS + RS_CELLS;
  tbforth_uram->didx = -1;
  
  tbforth_abort_clr();
  tbforth_abort();

  tbforth_uram->base = 10;
}

/* Bootstrap code */
void tbforth_load_prims(void) {
  dict->here = DICT_HEADER_WORDS+1;
  /*
    Store our primitives into the dictionary.
  */
  store_prim("lit", LIT);
  store_prim("cold", COLD);
  store_prim("here", HERE);
  store_prim("uram", URAM_BASE_ADDR);
  store_prim("iram", RAM_BASE_ADDR);
  store_prim("immediate", IMMEDIATE);
  store_prim("abort", ABORT);
  store_prim("rpick", RPICK);
  store_prim("0skip?", SKIP_IF_ZERO);
  store_prim("drop", DROP);
  store_prim("rot", ROT);
  store_prim("dup", DUP);
  store_prim("swap", SWAP);
  store_prim("over", OVER);
  store_prim("jmp", JMP);
  store_prim("0jmp?", JMP_IF_ZERO);
  store_prim("exec", EXEC);
  store_prim(",", COMMA);
  store_prim("d,", DCOMMA);
  store_prim("1+", INCR);
  store_prim("1-", DECR);
  store_prim("+", ADD);
  store_prim("-", SUB);
  store_prim("and", AND);
  store_prim("or", OR);
  store_prim("xor", XOR);
  store_prim("invert", INVERT);
  store_prim("lshift", LSHIFT);
  store_prim("rshift", RSHIFT);
  store_prim("*", MULT);
  store_prim("/", DIV);
  store_prim("*/", MULT_DIV);
  store_prim("mod", MOD);
  store_prim("0=", EQ_ZERO);
  store_prim("=", EQ);
  store_prim("<", LESS_THAN);
  store_prim("dlit", DLIT);
  store_prim(">r", RPUSH);
  store_prim("r>", RPOP);
  store_prim("dict@", DICT_FETCH);
  store_prim("!", STORE);
  store_prim("@", FETCH);
  store_prim("dict!", DICT_STORE);
  store_prim(";", EXIT);  make_immediate();
  store_prim(":", DEF); 
  store_prim("(create)", _CREATE); 
  store_prim("(allot1)", VAR_ALLOT);
  store_prim("(find-code)", FIND);
  store_prim("(find-head)", FIND_ADDR);
  store_prim(",\"", COMMA_STRING); make_immediate();
  store_prim("postpone", POSTPONE); make_immediate();

  store_prim("next-word", NEXT);
  store_prim("next-char", CNEXT);
  store_prim("+c!", CHAR_STORE);
  store_prim("c!+", CHAR_APPEND);
  store_prim("+c@", CHAR_FETCH);
  store_prim("bcopy", BYTE_COPY);
  store_prim("bstr=", BYTE_CMP);
  store_prim("+dict-c@", DCHAR_FETCH);
  store_prim("substr", SUBSTR);
  store_prim(">string", NUM_TO_STR);
  store_prim(">num", PARSE_NUM);
  store_prim("u>string", UNUM_TO_STR);
  store_prim("interpret", INTERP);
  store_prim("cf", CALLC);

  VAR_ALLOTN(PAD_SIZE);
}

/* Return a counted string pointer
 */
char* tbforth_count_str(CELL addr,CELL* new_addr) {
  char *str;
  str =(char*)&tbforth_ram[addr+1];
  *new_addr = tbforth_ram[addr];
  return str;
}

tbforth_stat exec(CELL wd_idx, bool toplevelprim,uint8_t last_exec_rdix) {
  /* Scratch/Register variables for exec */
  RAMC r1, r2, r3;
  char *str1, *str2;
  char b;
  CELL cmd;

  while(1) {
    if (wd_idx == 0) {
      tbforth_abort_request(ABORT_ILLEGAL);
      tbforth_abort();		/* bad instruction */
      return E_NOT_A_WORD;
    }
    cmd = tbforth_dict[wd_idx++];

    switch (cmd) {
    case 0:
      tbforth_abort_request(ABORT_ILLEGAL);
      tbforth_abort();		/* bad instruction */
      return E_NOT_A_WORD;
    case ABORT:
      tbforth_abort_request(ABORT_WORD);
      break;
    case IMMEDIATE:
      make_immediate();
      break;
    case RAM_BASE_ADDR:
      dpush (0);
      break;
    case URAM_BASE_ADDR:
      dpush(sizeof(struct tbforth_iram));
      break;
    case SKIP_IF_ZERO:
      r1 = dpop(); r2 = dpop();
      if (r2 == 0) wd_idx += r1;
      break;
    case DUP:
      dpush(dtop());
      break;
    case SWAP:
      r1 = dtop();
      dtop()=dtop2();
      dtop2()=r1;
      break;
    case OVER:
      dpush(dtop2());
      break;
    case DROP:
      (void)dpop();
      break;
    case ROT:
      r1 = dtop3();
      dtop3() = dtop();
      dtop() = r1;
      break;
    case JMP:
      wd_idx = dpop();
      break;
    case JMP_IF_ZERO:
      r1 = dpop(); r2 = dpop();
      if (r2 == 0) wd_idx = r1;
      break;
    case HERE:
      dpush(dict_here());
      break;
    case COLD:
      tbforth_init();
      break;
    case LIT:  
      dpush(tbforth_dict[wd_idx++]);
      break;
    case DLIT:  
      dpush((((uint32_t)tbforth_dict[wd_idx])<<16) |
	    (uint16_t)tbforth_dict[wd_idx+1]); 
      wd_idx+=2;
      break;
    case LESS_THAN:
      r1 = dpop();
      dtop() = (dtop() < r1);
      break;
    case INCR:
      dtop()++; 
      break;
    case DECR:
      dtop()--; 
      break;
    case ADD:
      r1 = dpop();
      dtop() += r1;
      break;
    case SUB:
      r1 = dpop();
      dtop() -= r1;
      break;
    case AND:
      r1 = dpop();
      dtop() &= r1;
      break;
    case LSHIFT:
      r1 = dpop(); 
      dtop() <<= r1;
      break;
    case RSHIFT:
      r1 = dpop();
      dtop() >>= r1;
      break;
    case OR:
      r1 = dpop(); 
      dtop() |= r1;
      break;
    case XOR:
      r1 = dpop(); 
      dtop() ^= r1;
      break;
    case INVERT:
      dtop() = ~dtop();
      break;
    case MULT:
      r1 = dpop(); 
      dtop() *= r1;
      break;
    case DIV :
      r1 = dpop(); 
      dtop() /= r1;
      break;
    case MULT_DIV :
      {
	uint64_t tmp;
	r1 = dpop(); r2 = dpop();
	tmp = r1 * r2;
	r1 = dpop();
	dpush(tmp/r1);
      }
      break;
    case MOD :
      r1 = dpop();
      dtop() %= r1;
      break;
    case RPICK:
      r1 = dpop();
      r2 = rpick(r1);
      dpush(r2);
      break;
    case EQ_ZERO:
      dtop() = (dtop() == 0);
      break;
    case EQ:
      r1 = dpop(); 
      dtop() =  (r1 == dtop());
      break;
    case RPUSH:
      rpush(dpop());
      break;
    case RPOP:
      dpush(rpop());
      break;
    case DICT_FETCH:
      r1 = dpop();
      dpush(tbforth_dict[r1]);
      break;
    case DICT_STORE:
      r1 = dpop();
      r2 = dpop();
      dict_write(r1,r2);
      break;
    case FETCH:
      r1 = dpop();
      dpush(tbforth_ram[r1]);
      break;
    case STORE:
      r1 = dpop();
      r2 = dpop();
      tbforth_ram[r1] = r2;
      break;
    case EXEC:
      r1 = dpop();
      rpush(wd_idx);
      wd_idx = r1;
      break;
    case EXIT:
      if (tbforth_uram->ridx > last_exec_rdix) return U_OK;
      wd_idx = rpop();
      break;
    case CNEXT:
      b = next_char();
      dpush(b);
      break;
    case CHAR_FETCH:
      r1 = dpop();
      r2 = dpop();
      str1 =(char*)&tbforth_ram[r2];
      str1+=r1;
      dpush(0xFF & *str1);
      break;
    case BYTE_COPY:
    case BYTE_CMP:
      {
	RAMC from, dest, fidx, didx, cnt;
	cnt = dpop();
	didx = dpop();
	dest = dpop();
	fidx = dpop();
	from  = dpop();
	str1 = (char*)&tbforth_ram[from] + fidx;
	str2 = (char*)&tbforth_ram[dest] + didx;
	if (cmd == BYTE_CMP)
	  dpush(memcmp (str2, str1, cnt) == 0);
	else
	  memcpy (str2, str1, cnt);
      }
      break;
    case DCHAR_FETCH:
      r1 = dpop();
      r2 = dpop();
      str1 =(char*)&tbforth_dict[r2];
      str1+=r1;
      dpush(0xFF & *str1);
      break;
    case CHAR_STORE:
      r1 = dpop();
      r2 = dpop();
      str1 =(char*)&tbforth_ram[r2];
      str1+=r1;
      *str1 = dpop();
      break;
    case CHAR_APPEND:
      r1 = dpop();
      r2 = tbforth_ram[r1];
      str1 =(char*)&tbforth_ram[r1+1];
      str1+=r2;
      b = dpop();
      *str1 = b;
      tbforth_ram[r1]++;
      break;
    case NEXT:
      str2 = PAD_STR;
      str1 = tbforth_next_word();
      memcpy(str2,str1, tbforth_iram->tibwordlen);
      PAD_STRLEN = tbforth_iram->tibwordlen;		/* length */
      dpush(PAD_ADDR);
      break;
    case COMMA_STRING:
      if (tbforth_iram->state == COMPILING) {
	dict_append(LIT);
	dict_append(dict_here()+sizeof(RAMC)); /* address of counted string */

	dict_append(LIT);
	rpush(dict_here());	/* address holding adress  */
	dict_incr_here(1);	/* place holder for jump address */
	dict_append(JMP);
      }
      rpush(dict_here());
      dict_incr_here(1);	/* place holder for count*/
      r1 = 0;
      do {
	r2 = 0;
	b = next_char();
	if (b == 0 || b == '"') break;
	r2 |= BYTEPACK_FIRST(b);
	++r1;
	b = next_char();
	if (b != 0 && b != '"') {
	  ++r1;
	  r2 |= BYTEPACK_SECOND(b);
	}
	dict_append(r2);
      } while (b != 0 && b!= '"');
      dict_write(rpop(),r1);
      if (tbforth_iram->state == COMPILING) {
	dict_write(rpop(),dict_here());	/* jump over string */
      }
      break;
    case CALLC:
      r1 = c_handle();
      if (r1 != U_OK) return (tbforth_stat)r1;
      break;
    case VAR_ALLOT:
      dpush(VAR_ALLOT_1());
      break;
    case DEF:
      tbforth_iram->state = COMPILING;
    case _CREATE:
      dict_start_def();
      tbforth_next_word();
      make_word(CURR_TIB_WORD,tbforth_iram->tibwordlen);
      if (cmd == _CREATE) {
	dict_end_def();
      } else {
	tbforth_iram->compiling_word = dict_here();
      }
      break;
    case COMMA:
      dict_append(dpop());
      break;
    case DCOMMA:
      r1 = dpop();
      dict_append((uint32_t)r1>>16);
      dict_append(r1);
      break;
    case PARSE_NUM:
      r1 = dpop();
      str1=tbforth_count_str((CELL)r1,(CELL*)&r1);
      str1[r1] = '\0';
      tbforth_abort_clr();
      r2 = parse_num(str1,tbforth_uram->base);
      if (!tbforth_aborting()) 
	dpush(r2);
      else return E_NOT_A_WORD;
      break;
    case FIND:
    case FIND_ADDR:
      r1 = dpop();
      str1=tbforth_count_str((CELL)r1,(CELL*)&r1);
      r1 = find_word(str1, r1, &r2, 0, &b);
      if (r1 > 0) {
	if (b) r1 = tbforth_dict[r1];
      }
      if (cmd == FIND) {
	dpush(r1);
      } else {
	dpush(r2);
      }
      break;
    case POSTPONE:
      str1 = tbforth_next_word();
      r1 = find_word(str1, tbforth_iram->tibwordlen, 0, 0, &b);
      if (r1 == 0) {
	tbforth_abort_request(ABORT_NAW);
	tbforth_abort();
	return E_NOT_A_WORD;
      }
      if (b) {
	dict_append(tbforth_dict[r1]);
      } else {
	dict_append(r1);
      }
      break;
    case SUBSTR:		/* return a substring of the tbforth string */
      r1 = dpop();		/* addr */
      r2 = dpop();		/* length */
      r3 = dpop();		/* start */
      str1 = tbforth_count_str(r1,(CELL*)&r1);
      if (r1 < r2) r2 = r1;
      PAD_STRLEN = r2;
      memcpy(PAD_STR, str1 + r3, r2);
      dpush(PAD_ADDR);
      break;
    case UNUM_TO_STR:
    case NUM_TO_STR:			/* 32bit to string */
      {
	if (cmd == UNUM_TO_STR)
	  u32toa(dpop(),PAD_STR,tbforth_uram->base);
	else
	  i32toa(dpop(),PAD_STR,tbforth_uram->base);
	PAD_STRLEN=strlen(PAD_STR);
	dpush(PAD_ADDR);
      }
      break;
    case INTERP:
      dpush (interpret_tib());
      break;
    default:
      if (cmd > LAST_PRIMITIVE) {
	/* Execute user word by calling until we reach primitives */
	rpush(wd_idx);
	wd_idx = tbforth_dict[wd_idx-1]; /* wd_idx-1 is current word */
	//	goto CHECK_STAT;
      } else {
	tbforth_abort_request(ABORT_ILLEGAL);
      }
      break;
    }
    if (tbforth_aborting()) {
      tbforth_abort();
      return E_ABORT;
    }
    if (toplevelprim) return U_OK;
  } /* while(1) */
}

CELL find_word(char* s, uint8_t slen, RAMC* addr, bool *immediate, char *primitive) {
  CELL fidx = dict->last_word_idx;
  CELL prev = fidx;
  uint8_t wlen;

  while (fidx != 0) {
    if (addr != 0) *addr = prev ;
    prev = tbforth_dict[fidx++];
    wlen = tbforth_dict[fidx++]; 
    if (immediate) *immediate = (wlen & IMMEDIATE_BIT) ? 1 : 0;
    if (primitive) *primitive = (wlen & PRIM_BIT) ? 1 : 0;
    wlen &= WORD_LEN_BITS;
    if (wlen == slen && strncmp(s,(char*)(tbforth_dict+fidx),wlen) == 0) {
      fidx += (wlen / BYTES_PER_CELL) + (wlen % BYTES_PER_CELL);
      return fidx;
    }
    fidx = prev;
  }
  if (addr != 0) *addr = 0 ;
  return 0;
}

tbforth_stat interpret_tib() {
  tbforth_stat stat;
  char *word;
  CELL wd_idx;
  bool immediate = 0;
  char primitive = 0;
  while(*(word = tbforth_next_word()) != 0) {
    wd_idx = find_word(word,tbforth_iram->tibwordlen,0,&immediate,&primitive);
    switch (tbforth_iram->state) {
    case 0:			/* interpret mode */
      if (wd_idx == 0) {	/* number or trash */
	RAMC num = parse_num(word,tbforth_uram->base);
	if (tbforth_aborting()) {
	  tbforth_abort_request(ABORT_NAW);
	  tbforth_abort();
	  return E_NOT_A_WORD;
	}
	dpush(num);
      } else {
	stat = exec(wd_idx,primitive,tbforth_uram->ridx-1);
	if (stat != U_OK) {
	  tbforth_abort();
	  tbforth_abort_clr();
	  return stat;
	}
      }
      break;
    case COMPILING:			/* in the middle of a colon def */
      if (wd_idx == 0) {	/* number or trash */
	RAMC num = parse_num(word,tbforth_uram->base);
	if (tbforth_aborting()) {
	  tbforth_abort();
	  dict_end_def();
	  return E_NOT_A_WORD;
	}
	dict_append(DLIT);
	dict_append(((uint32_t)num)>>16);
	dict_append(((uint16_t)num)&0xffff);
      }	else if (word[0] == ';') { /* exit from a colon def */
	tbforth_iram->state = 0;
	dict_append(EXIT);
	dict_end_def();
	tbforth_iram->compiling_word = 0;
      } else if (immediate) {	/* run immediate word */
	stat = exec(wd_idx,primitive,tbforth_uram->ridx-1);
	if (stat != U_OK) {
	  tbforth_abort_request(ABORT_ILLEGAL);
	  tbforth_abort();
	  dict_end_def();
	  return stat;
	}
      } else {			/* just compile word */
	if (primitive) {
	  /* OPTIMIZATION: inline primitive */
	  dict_append(tbforth_dict[wd_idx]);
	} else {
	  /* OPTIMIZATION: skip null definitions */
	  if (tbforth_dict[wd_idx] != EXIT) {
	    if (wd_idx == tbforth_iram->compiling_word) { 
	      /* Natural recursion for such a small language is dangerous.
		 However, tail recursion is quite useful for getting rid
		 of BEGIN AGAIN/UNTIL/WHILE-REPEAT and DO LOOP in some
		 situations. We don't check to see if this is truly a
		 tail call, but we treat it as such.
	      */
	      dict_append(LIT);
	      dict_append(tbforth_iram->compiling_word);
	      dict_append(JMP);
	    } else {
	      dict_append(wd_idx);
	    }
	  }
	}
      }
      break;
    }
  }
  return U_OK;
}

tbforth_stat tbforth_interpret(char *str) {
  CLEAR_TIB();
  tbforth_iram->tibclen = min(PAD_SIZE, strlen(str)+1);
  memcpy(tbforth_iram->tib, str, tbforth_iram->tibclen);
  return interpret_tib();
}
