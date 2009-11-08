\ Basics

:false (0 constant)
:true (-1 constant)

:4+		2+ 2+ ;
:4-		2- 2- ;
:negate z-	0 z - ;

:/mod yz-	y z mod  y z / ;

:2@ z-		z 4+ @  z @ ;
:2! z-		z !  z 4+ ! ;

:0> z-		0 z < ;

:c,		here c!  1 allot ;

:if		'<<branch>>(#),  here  0 , ;
:then z-	here  z ! ;
:unless		if ';(#), then ;
:when		'0=(#), unless ;
:&&             '0=(#), if 'false(#), ';(#), then ;
:||		if 'true(#), ';(#), then ;

:variable	here constant  0 , ;
:2variable	variable  0 , ;

:given		0 , ;   \ Reserve a cell for ;will data

:abs z-		z 0< (if) 0 z - ; (then) z ;


:space		32 emit ;
:cr		10 emit ;
:type z-	z c@ (when)  z c@ emit  z 1+ type ;
:?		@ . ;

:strlening yz-	z c@ (if) y 1+ z 1+ strlening ; (then)  y ;
:strlen	z-	0 z strlening ;

:erase yz-	z (when)  0 y c!  y 1+ z 1- erase ;


:random-seed (variable  1234567 random-seed !)
:random		random-seed @  16807 u*  2147483647 umod  random-seed !
		random-seed @ ;


:.hex-digit	0xf and  "0123456789abcdef" + c@ emit ;
:.hex yz-	z (when)  y 4 u>> z 1- .hex  y .hex-digit ;
:.byte		2 .hex ;
:.address	8 .hex ;
:?.address z-	z 8 umod 0= (if) space (then)
		z 16 umod 0= (if) cr z .address  $: emit (then) ;
:dump yz-	y ?.address
		z 0> (when)
		32 emit  y c@ .byte
		y 1+ z 1- dump ;
:test		here 3 , 1 , 4 , 1 , 5 , 20 dump ;
