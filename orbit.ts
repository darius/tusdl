32 start-sdl

 100000000   0 0  0   0 make-particle
       100  50 0  0 163 make-particle
.s

:stepping  orbit-tick orbit-multishow  listen-quit? (unless)  stepping ;
(stepping report-frames)
