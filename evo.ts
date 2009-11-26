\ Stuff

:> yz-  z y < ;

:grid-coords yz- y thumb-width / .  z thumb-height / . cr
		y thumb-width /  z thumb-height / ;
:grid-square	grid-coords  cols *  + ;

:coords		cols /mod ;

:.generate	coords generate ;
:.populate	coords populate ;
:.mutate	coords mutate ;
:.complexity	coords complexity ;
:.same-thumbs? yz-  y coords  z coords  same-thumbs? ;
:.copy yz-	y coords  z coords  copy ;
:.generate-big yz-  y coords  z coords  generate-big ;

:.reshow	.generate show ;

:thru?		cols rows * = ;


:event (0 0 2variable)
:poll		listen event 2!  event @ ;
:reset		0 0 event 2! ;
:absorb		event @ (unless)  wait event 2! ;

:gridding yz-	z thru? (unless)  poll (unless)  z y execute  y z 1+ gridding ;


\ Evolution

:minplexity (5 constant)
:good-start?	.complexity minplexity > ;
:initing z-	z .populate  z good-start? (unless)  z initing ;
:init z-	z initing  z .reshow ;
:fresh		'init 0 gridding ;

:try z-		z 0 .copy  z .mutate ;
:new? z-	z 0 .same-thumbs? 0= ;
:decent? z-	0 .complexity 2 *  z .complexity 3 *  <
		(if)  z .generate  show  z new? ;  (then)  false ;
:mutating z-	z try  z decent? (unless)  z mutating ;
:replace z-	z mutating  show ;
:choose z-	0 z .copy  0 .reshow  'replace 1 gridding ;


\ Gene frequencies

( 5 &constant !u
 12 &x        !u
 12 &y        !u
  0 &sprinkle !u
  1 &abs      !u
  1 &atan     !u
  1 &cos      !u
  1 &exp      !u
  1 &floor    !u
  1 &log      !u
  1 &neg      !u
  1 &sign     !u
  1 &sin      !u
  1 &sqrt     !u
  1 &tan      !u
  1 &hwb      !u
  1 &+        !u
  1 &-        !u
  1 &*        !u
  1 &/        !u
  1 &average  !u
  1 &hypot    !u
  1 &max      !u
  1 &min      !u
  1 &mix      !u
  1 &mod      !u
  1 &pow      !u
  1 &and      !u
  1 &or       !u
  1 &xor      !u
  5 &color    !u
  2 &rotcolor !u
)


\ Display modes

:zoom yz-	y z .generate-big  show ;
:enlarging yz-	z thru? (unless)  y z zoom  poll (unless)  y z 1+ enlarging ;
:big		0 0 enlarging ;

:grid		'.reshow 0 gridding ;

:atomic-enlarging yz-
        	z thru? (unless)  y z .generate-big  y z 1+ atomic-enlarging ;
:atomic-big	0 0 atomic-enlarging ;


\ UI

\ This was broken... hung up.  Why?  Oh, zoom needs to take 2 args... try this.
:sliding z-	z thru? (unless)  z 0 enlarging  poll (unless)  z 1+ sliding ;
:slideshow	0 sliding ;

:quit? (0 variable)

:on-keyboard z-	reset
		$! z = (if)  command-loop ;  (then)
		$1 z = (if)  append1 ;       (then)
		$a z = (if)  append ;        (then)
		$b z = (if)  big ;           (then)
		$f z = (if)  fresh ;         (then)
		$g z = (if)  grid ;          (then)
		$i z = (if)  save-image ;    (then)
		$q z = (if)  -1 quit? ! ;    (then)
		$r z = (if)  restore grid ;  (then)
		$s z = (if)  save ;          (then)
		$v z = (if)  load-random grid ;  (then)
		z .  $? emit  cr ;

:mouse-xy	65536 /mod ;
:on-mouse	reset  mouse-xy grid-square choose ;
 
:react		event 2@ yz-
		1 z = (if)  y on-keyboard ;  (then)
		2 z = (if)  y on-mouse ;     (then)
		reset  show ;
:reacting	quit? @ (unless)  react  absorb reacting ;

:main		fresh reacting ;
\ (32 start-sdl restore grid reacting) \ for debugging

:make-ppm z-	0 z .copy  atomic-big  save-image ;
\ (32 no-sdl restore 0 make-ppm  \ for batch image generation
(32 start-sdl main)              \ for ordinary interactive runs
