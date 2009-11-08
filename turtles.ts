\ Stuff

:grid-coords yz- y tile-width /  z tile-height / ;
:grid-square	grid-coords  tcols *  + ;

:thru?		tcols trows * = ;

:event (2variable)
:poll		listen event 2!  event @ ;
:reset		0 0 event 2! ;
:absorb		event @ (unless)  wait event 2! ;

:reshow		evaluate show ;

:gridding yz-	z thru? (unless)  z y execute  poll (unless)  y z 1+ gridding ;


\ Evolution

:init z-	z randomize  z reshow ;
:fresh		'init(#) 0 gridding ;

:try z-		z 0 tcopy  z fuck  z evaluate ;
:new? z-	z 0 tsame? 0= ;
:mutating z-	z try  z new? (unless)  z mutating ;
:replace z-	z mutating  show ;

:choose z-	0 z tcopy  0 reshow  'replace(#) 1 gridding ;


\ UI

:quit? (variable)

:on-keyboard z-	reset
		$f z = (if)  fresh ;         (then)
		$q z = (if)  -1 quit? ! ;    (then)
		z .  $? emit  cr ;

:mouse-xy	65536 /mod ;
:on-mouse	reset  mouse-xy grid-square choose ;
 
:react		event 2@ yz-
		1 z = (if)  y on-keyboard ;  (then)
		2 z = (if)  y on-mouse ;     (then)
		reset  show ;
:reacting	quit? @ (unless)  react  absorb reacting ;

:main		fresh reacting ;


\ Example crap

:waiting	listen-quit? (unless)  waiting ;
:redisplay	0 display  show ;

:step		hatch{ 90 lt  10 fd  -90 lt }  26 lt  20 fd  -11 lt  
        	plot redisplay ;

\ Highly imperfect yin-yang symbol 
\ (notably missing the circular inclusions)
:yin-yang	80 fd  90 lt  50 fd  plot redisplay
		step step step step
		step step step step
		step step step step
		step step step step 
		diffuse diffuse redisplay ;

(32 start-sdl)
