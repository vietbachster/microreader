/* Auto-embedded sleep.mgr via .incbin */
    .section .rodata
    .global _binary_sleep_mgr_start
    .global _binary_sleep_mgr_end
    .align 4
_binary_sleep_mgr_start:
    .incbin "../../resources/sleep.mgr"
_binary_sleep_mgr_end:
