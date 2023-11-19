(create) : 1 iram ! (create) 1 iram ! here 2 iram + !  postpone ;  ;
: \ next-char 0 - 3 0skip? \ ; immediate

\  toolboxforth - A tiny ROMable 16/32-bit FORTH-like scripting language
\          for microcontrollers.
\
\  Copyright � 2009-2023 Todd Coram, todd@maplefish.com, USA.
\
\  Permission is hereby granted, free of charge, to any person obtaining
\  a copy of this software and associated documentation files (the
\  "Software"), to deal in the Software without restriction, including
\  without limitation the rights to use, copy, modify, merge, publish,
\  distribute, sublicense, and/or sell copies of the Software, and to
\  permit persons to whom the Software is furnished to do so, subject to
\  the following conditions:
\  
\  The above copyright notice and this permission notice shall be
\  included in all copies or substantial portions of the Software.
\  
\  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
\  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
\  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
\  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
\  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
\  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
\  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

;

\ The first line in this file defines our major defining word: ":"
\ The following line bootstraps in the ability to handle comments.
\ (Notice that is tail call recursive!)
\
\  We could define it clearer using words we define later as:
\  (create) : is-compiling (create) is-compiling here compiling-word! postpone ; ;
\
\ FWIW, the lone ";" above is just for Emacs (Forth mode) since the first line
\ confuses it and looks like the ; is commented out. Of course, it isn't...

\ This file is meant as much to be read by you, dear reader, as by the Toolboxforth
\ interpreter. While not a "literate" program, I wanted to make this the canonical
\ document for how things work.    
\
\ In this file we shall see some primitive words commented out. This is because
\ they were more efficiently coded in C, but you could very well uncomment them
\ as a demonstration of how small the C core can be!

\ Generally, this documentation is a reminder to myself of how Toolboxforth (tbforth)
\ is implemented. 
    
\ Here is another useful comment word: stack effects. It cannot span multiple lines...
\    
: ( next-char 41 - 3  0skip? ( ; immediate \ enable stack comments  )
;



\ A RAM CELL is always 32 bits (4 bytes).
\ A Dictionary CELL is always 16 bits (2 bytes).
\
: RCELL 4 ;
: DCELL 2 ;
 
\ The dictionary starts at address 0. We have a single dictionary and the header
\ describes the essentials:
\
: dversion ( - u)  0 @ ;		  \ version of dictionary
: cellsize ( - u) 1 @ ;			  \ size of cell (always 2, for now at least)
: maxdictcells ( - u) 2 @ ;		  \ max size of dictionary in cells
: (here) ( - a) 3 ;			  \ address of here
: lwa  ( - a) 4 ;			  \ address of last (or current) word defined
: uramsize ( -- u) 5 @ ;		  \ size of uram in RCELL


\ Machine stuff (iram):
\
: state? ( - f) 0 iram + @  ;		  \ what state are we in?
: reset-state ( - ) 0 0 iram + !  ;	  \ force not compiling mode
: is-compiling ( - ) 1 0 iram + ! ;	  \ force compiling  mode
: compiling? ( - f) state? 1 = ;	  \ are we compiling?
: compiling-word! ( a --) 2 iram + ! ;	  \ save the address of word being compiled
: ramsize ( - u) 1 iram + @  ;		  \ size of ram


\ Task based RAM (uram):
\
: uram-size ( - u) uram 0 + @  ;	  	\ size of uram
: base ( - a) uram 1 +  ;			\ number base address
: sidx ( - u) uram 2 + @ ;			\ data stack pos
: ridx ( - u) uram 3 + @ ;			\ return stack pos
: dslen ( - u) uram 4 + @ ;			\ data stack length
: rslen ( - u) uram 5 + @ ;			\ return stack length
: dsa ( - a)  uram 6 + ;			\ data stack address
: rsa ( - a) dsa dslen + ;			\ return stack address
: T ( - a) dsa sidx + 1- ;			\ top of data stack
: R ( - a) dsa ridx + 1+ ;			\ top of return stack

\ Terminal input buffer:
\
: tibidx iram 3 + ;
: tibwordidx iram 4 + ;
: tibwordlen iram 5 + ;
: tib iram 6 + ;		( starts with cnt... )
: tibbuf iram 7 + ;


\ Stuff that doesn't have to be in the C core, but is there for speed.
\
\ : rpick  dsa ridx + 1+ 1+ + @ ;
\ : r@ ( - a ) R @ ;
\ : dup ( a - a a) T @ ;
\ : over ( a b - a b a) T 1- @ ;
\ : swap ( a b - b a) >r >r 1 rpick r> r> drop ;
\ : rot ( a b c - c b a) >r >r >r 2 rpick 1 rpick r> r> r> drop drop ;
\ : mod ( y x  - u) over swap ( y y x) dup >r  ( y y x)  / r> * - ;
\ : = ( a b - f) - 0= ;

\ Stack item count
\
: depth ( -- u) sidx 1  + ; 


\ no operation
\
: nop ;

\ Useful number stuff
\
: negate -1 * ;
: not 0= ;
: 2+ 2 + ;
: 2* 2 * ;
: 2/ 2 / ;


\ Stack manipulation
\
: 2dup ( x y - x y x y)  over over ;
: 2drop ( x y - ) drop drop ;
: nip ( x y - y) over xor xor ;
: 2swap ( a b c d - c d a b) rot >r rot r> ;

\ Misc useful stuff
\
: +! ( u a - ) dup >r @ + r> ! ;
: incr ( a - ) 1 swap +! ;
: decr ( a - ) -1 swap +! ;

: /mod ( u u - r q)
  2dup / >r mod r> ;

\ Index to exec'able code
\
: ' ( -<word>- a) next-word (find-code) ;
: ['] next-word (find-code)  1 ,  , ; immediate \ hack: LIT *is* 1

\ Index to header of word
\
: '& ( -<word>- a) next-word (find-head) ;
: ['&] next-word (find-head)  1 ,  , ; immediate \ hack: LIT *is* 1

\ Helper word for building the below constructs.
\
: [compile]
    postpone [']			\ for later: get word
    ['] ,  ,				\ store a comma (for later get word)
; immediate


\ A create that simply returns address of here after created word.
\
: create
    (create)
    [compile] lit
    here 2+ ,				\ just pass the exit (here after def)
    [compile] ;
;

: variable
    (create)
    [compile] dlit
\    (allot1) $80000000 or d,
    (allot1)  d,
    [compile] ;
;

\ constants are stored in dictionary as two 16 bit dictionary cells.
\
: constant ( d -<name>- )
    (create)
    [compile] dlit d,
    [compile] ; 
;


: dict@ [compile] @ ; immediate
: dict! [compile] ! ; immediate
: ddict@ ( a - u)
  dup dict@ swap 1+ dict@ swap 16 lshift or ;
  
: ddict! ( d a - )
    over 16 rshift over dict!
    swap $0000ffff and swap 1+ dict! ;

\ Conditionals

: if ( -- ifaddr)
    [compile] lit			\ precede placeholder
    here				\ push 'if' address on the stack
    0 ,				\ placeholder for 'then' address
    [compile] 0jmp?			\ compile a conditional jmp to 'then'
; immediate

: else ( ifaddr -- elseaddr )
    here 3 +				\ after lit and jmp
    swap dict!				\ fix 'if' to point to else block
    [compile] lit			\ precede placeholder
    here				\ push 'else' address on stack
    0 ,				\ place holder for 'then' address
    [compile] jmp			\ compile an uncoditional jmp to 'then'
; immediate


: then ( ifaddr|elseaddr -- )
    here swap dict!			\ resolve 'if' or 'else' jmp address
; immediate


\ Looping
\

\ First "for". It's simpler and faster than do loop... but not as flexible
\  It also always execute at least once.. regardless of value.
\
: for ( cnt -- faddr)
  [compile] 1-
  [compile] >r
  here ; immediate


: next ( faddr -- )
  [compile] r>
  [compile] 1-
  [compile] dup
  [compile] >r
  [compile] 0<
  [compile] lit
  ,					\ store for's here
  [compile] 0jmp?
  [compile] r> [compile] drop ; immediate

variable _leaveloop

: do ( limit start -- doaddr)
    [compile] swap			( -- start limit)
    [compile] >r  [compile] >r		\ store them on the return stack
    here				\ push address of 'do' onto stack
; immediate ( -- r:limit r:start)

: i ( -- inner_idx)
    [compile] lit 0 , [compile] rpick	\ pick index from return stack
; immediate

: j ( -- outer_idx)
    [compile] lit 2 , [compile] rpick	\ pick index from return stack
; immediate

: leave ( -- )
    [compile] lit			\ precede placeholder
    here _leaveloop !			\ store address for +loop resolution
    0 ,				\ placeholder for 'leave' address
    [compile] jmp			\ jmp out of here
; immediate

: +loop ( doaddr n -- )
    [compile] r>			\ ( -- start{idx})
    [compile] +				\ ( -- idx+n)
    [compile] dup			\ ( -- idx idx)
    [compile] >r			\ ( -- idx) store new
    [compile] lit  1 ,
    [compile] rpick			\ ( -- idx limit)
    [compile] >=			\ if idx >= limit, we are done.
    [compile] lit
    ,					\ store doaddr
    [compile] 0jmp?
    _leaveloop @ 0 > if			\ if we have a leave, resolve it
	here  _leaveloop @ dict!	\ This is where 'leave' wants to be
	0 _leaveloop !
    then
    [compile] r>   [compile] r>		\ clean up
    [compile] drop [compile] drop
; immediate

: loop  ( doaddr -- )
    [compile] lit 1 ,
    postpone +loop
; immediate

: unloop ( -- )
    [compile] r>   [compile] r>		\ clean up
    [compile] drop [compile] drop
; immediate

: exit  r> drop ;

: begin  here ; immediate

: again ( here -- )
    [compile] lit
    ,
    [compile] jmp
; immediate

: while
    [compile] lit
    here
    0 , \ place holder for later.
    [compile] 0jmp?
; immediate

: repeat
    [compile] lit
    >r ,
    [compile] jmp
    here r> dict! \ fix WHILE
; immediate

: until
    [compile] lit
    ,
    [compile] 0jmp?
; immediate

: dup? dup 0= 0= if dup then ;

: allot ( n -- )  0 do (allot1) drop loop ;

: byte-allot ( n -- ) RCELL /mod + allot  ;

\ Point to internally allocated (scratch) PAD
\
uram 6 + dslen + rslen + constant pad


\ Scratch variable (be careful, it doesn't nest and may be modified by anyone)
\
variable R1

\ Implement traditional forth "word" using the scratch pad.
\
: word ( delim -- addr)
    R1 !
    0 pad !
    begin
	next-char dup R1 @ = over 0= or if drop pad exit then
	pad c!+
    again ;
