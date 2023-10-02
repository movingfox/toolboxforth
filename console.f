\ console with minimal line editing (^c and backspace).
\

tib constant TIB		( faster )

: clear-tib
  0 tibidx !
  0 tibwordlen !
  0 tibwordidx !
  0 TIB ! ;

: rd-line
    key dup emit dup
    TIB c!+
    dup 3 = if  clear-tib drop exit then
    dup 8 = if  -1 TIB +! then
    dup 13 = over 10 = or if 1 TIB +! drop exit then
    drop
    TIB @ cr . cr 
    rd-line ;

\ console... type exit to exit.
\
: quit
  clear-tib
  cr ." OK" cr
  clear-tib
  rd-line
  
  interpret
  dup 2 = if ." Huh? "  cr drop quit then
  dup 6 = if ." Abort!" cr drop quit then
  drop
  quit ;
