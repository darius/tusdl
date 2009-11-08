8 start-sdl

:under+ xyz-  x z +  y ;
:4dup wxyz-  w x y z  w x y z ;

:drawing  0 0 ;
:drawn  wxyz- ;

:my-p (variable)   \ ugh
:plot  my-p !  wxyz-  w y +  x z +  my-p @ grid8!  w x  y 1+ z ;
:...  yz-  0 z 1+ ;

\ w<-x0 x<-y0 y<-x z<-y
\ :plot  [ x0 y0  x y p ]  x0 x +  y0 y +  p grid8!  x0 y0  x 1+ y ;
\ :plot   4 pick 3 pick +  4 pick 3 pick +  2 roll grid8!  swap 1+ swap ;
\ :...   1+ swap  drop 0 swap ;

:_   0 plot ;
:X   1 plot ;

:r!     drawing
  _ X X  ...
  X X _  ...
  _ X _  drawn ;

:block! drawing
  X X  ...
  X X  drawn ;


:tally  ;   1 frames +!u ;

:quit?  \ True iff keyboard `q' was hit.
  listen yz-  
  z 1 =  y $r = and (if)  sprinkle 0 ;  (then)
  z 1 =  y $q = and ;

\ sleep() doesn't work here in cygwin for some reason, so here's
\ a busy-wait loop instead. (disabled now)
:delay z-   z (when)  z 1- delay ;
:sheesh   ; 10000 delay  frames @u . cr ;

:marg                show  quit? (unless)  margolus-step  tally sheesh marg ;
:life                show  quit? (unless)  life-step      tally life ;
:munch  decay-colors show  quit? (unless)  munch-step     tally munch ;

:sierp    show  quit? (unless)  1 under+  4dup sierp-step  tally sierp ;

:center  width 2/ height 2/ ;
:clear   clear8 ;

\ Here are some top-level animations to try.
:r-pentomino   4-colors     clear  center r!  life ;
:bubbles       4-colors     clear  center block!  0 frames !u  marg ;
:m             wipe-colors  clear  munch ;
:s             center 0 20 sierp  wxyz- ;

:slideshow   bubbles  r-pentomino  m  0 0 0 2 sierp wxyz- ;
