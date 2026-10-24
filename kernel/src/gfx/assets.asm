; Recursos incrustados en el núcleo (generados por el Makefile)
section .rodata
align 16
global intro_pcm_start, intro_pcm_end, logo_data
intro_pcm_start:
    incbin "build/assets/intro.pcm"
intro_pcm_end:
align 16
logo_data:
    incbin "build/assets/logo.raw"
