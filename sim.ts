"/usr/local/share/tusl/tuslrc.ts" load

:listen-quit?  \ True iff keyboard `q' was hit.
  listen yz-  z 1 =  y $q =  and ;

:iterate z-  z execute show  listen-quit? (unless)  z iterate ;
