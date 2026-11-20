.section .rodata
.global _binary_font_psf_start
.global _binary_font_psf_end
_binary_font_psf_start:
    .incbin "src/font.psf"
_binary_font_psf_end: