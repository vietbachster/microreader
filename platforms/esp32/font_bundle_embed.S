/* Auto-embedded font_bundle.bin via .incbin — bypasses PlatformIO EMBED_FILES bug. */
    .section .rodata
    .global _binary_font_bundle_bin_start
    .global _binary_font_bundle_bin_end
    .align 4
_binary_font_bundle_bin_start:
    .incbin "../../resources/fonts/font_bundle.bin"
_binary_font_bundle_bin_end:
