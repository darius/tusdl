\ Random showoff words

:within xyz-	x y < (&&) y z < ;

:yells z-	z (when)  z . 32 emit  z 1- yells ;

:spaces z-	z 0> (when)  32 emit  z 1- spaces ;


:run z-		z @ z yz-
		y (when)
		y 1 = (if)  z 4+ @  z 8 + run ; (then)
		y execute  z 4+ run ;

:repeat yz-	y 0> (when)  z run  y 1- z repeat ;
:plural		given ;will repeat ;

:dashes (plural) $- emit $- emit $  emit ;

:abort		type ;       \ FIXME need an error raiser
:erased z-	here  z allot  z erase ;
:string z-	given  z ,  z erased
		;will yz-  y z @ u< (if)  z 4+ y + ;  (then)
		           "Range error" abort ;


:array yz-	given z ,  y z * erased    \ y=#rows, z=#columns
		;will xyz-  \ x=row, y=column
			z 4+  z @ x * +  y + ;


:plot		(if) $* emit ; (then) $  emit ;
:rowing yz-	z (when)  y 0x80 and plot  y 2* z 1- rowing ;
:.row		cr  8 rowing ;
:.rows yz-	z (when)  z 1- y + c@ .row  y z 1- .rows ;

:shape		given c, c, c, c, c, c, c, c,
		;will 8 .rows cr ;

:man    (0x18 0x18 0x3C 0x5A 0x99 0x24 0x24 0x24 shape)
:equis  (0x81 0x42 0x24 0x18 0x18 0x24 0x24 0x81 shape)
:castle (0xAA 0xAA 0xFE 0xFE 0x38 0x38 0x38 0xFE shape)
(
man
equis
castle
)
