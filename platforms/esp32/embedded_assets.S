/* Auto-embedded assets via .incbin — bypasses PlatformIO EMBED_FILES bug. */
    .section .rodata
    .align 4

    /* Default Font Bundles */
    .global _binary_bookerly_bin_start
    .global _binary_bookerly_bin_end
_binary_bookerly_bin_start:
    .incbin "../../resources/fonts/bookerly.bin"
_binary_bookerly_bin_end:

    .align 4
    .global _binary_alegreya_bin_start
    .global _binary_alegreya_bin_end
_binary_alegreya_bin_start:
    .incbin "../../resources/fonts/alegreya.bin"
_binary_alegreya_bin_end:

    .align 4

    /* Sleep Images */
    .global _binary_sleep_0_mgr_start
    .global _binary_sleep_0_mgr_end
_binary_sleep_0_mgr_start:
    .incbin "../../resources/sleep/sleep_0.mgr"
_binary_sleep_0_mgr_end:

    .align 4
    .global _binary_sleep_1_mgr_start
    .global _binary_sleep_1_mgr_end
_binary_sleep_1_mgr_start:
    .incbin "../../resources/sleep/sleep_1.mgr"
_binary_sleep_1_mgr_end:

    .align 4
    .global _binary_sleep_2_mgr_start
    .global _binary_sleep_2_mgr_end
_binary_sleep_2_mgr_start:
    .incbin "../../resources/sleep/sleep_2.mgr"
_binary_sleep_2_mgr_end:
