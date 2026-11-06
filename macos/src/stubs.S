// Pass-through stubs of the macOS probe (arm64). Each stub calls a C observer with the hooked
// function's arguments, restores every argument register (x0-x8, q0-q7) and jumps on to the original
// code. The observer can't disturb the call, whatever the function's signature or return convention
// is: x8 carries the address for a returned struct.

#if defined(__arm64__)

    .text
    .p2align 2

    .private_extern _CameraInputProbe
_CameraInputProbe:
    stp x29, x30, [sp, #-224]!
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    str x8, [sp, #80]
    stp q0, q1, [sp, #96]
    stp q2, q3, [sp, #128]
    stp q4, q5, [sp, #160]
    stp q6, q7, [sp, #192]
    bl _ObserveCameraInput
    ldp q6, q7, [sp, #192]
    ldp q4, q5, [sp, #160]
    ldp q2, q3, [sp, #128]
    ldp q0, q1, [sp, #96]
    ldr x8, [sp, #80]
    ldp x6, x7, [sp, #64]
    ldp x4, x5, [sp, #48]
    ldp x2, x3, [sp, #32]
    ldp x0, x1, [sp, #16]
    ldp x29, x30, [sp], #224
    adrp x16, _g_camera_input_original@PAGE
    ldr x16, [x16, _g_camera_input_original@PAGEOFF]
    br x16

    .private_extern _RotatedInputProbe
_RotatedInputProbe:
    stp x29, x30, [sp, #-224]!
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    str x8, [sp, #80]
    stp q0, q1, [sp, #96]
    stp q2, q3, [sp, #128]
    stp q4, q5, [sp, #160]
    stp q6, q7, [sp, #192]
    bl _ObserveRotatedInput
    ldp q6, q7, [sp, #192]
    ldp q4, q5, [sp, #160]
    ldp q2, q3, [sp, #128]
    ldp q0, q1, [sp, #96]
    ldr x8, [sp, #80]
    ldp x6, x7, [sp, #64]
    ldp x4, x5, [sp, #48]
    ldp x2, x3, [sp, #32]
    ldp x0, x1, [sp, #16]
    ldp x29, x30, [sp], #224
    adrp x16, _g_rotated_input_original@PAGE
    ldr x16, [x16, _g_rotated_input_original@PAGEOFF]
    br x16

    // Where each stub continues: the original function, or the trampoline of an inline hook.
    .data
    .p2align 3
    .private_extern _g_camera_input_original
_g_camera_input_original:
    .quad 0
    .private_extern _g_rotated_input_original
_g_rotated_input_original:
    .quad 0

#endif
