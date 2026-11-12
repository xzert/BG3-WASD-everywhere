// Pass-through stubs of the macOS library (arm64). Each one leaves every argument register as it was
// (x0-x8, q0-q7) and jumps on to the original code, so it can't disturb the call, whatever the
// function's signature or return convention is: x8 carries the address for a returned struct.

#if defined(__arm64__)

    .text
    .p2align 2

    // Calls ObserveCameraInput with the arguments of ecl::CameraSystem::OnInputEvent.
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

    // Remembers the object of each gui::GameUI::Update call in g_game_ui. x16 is the scratch register
    // of calls between functions, free when a function starts.
    .p2align 2
    .private_extern _GameUIUpdateProbe
_GameUIUpdateProbe:
    adrp x16, _g_game_ui@PAGE
    str x0, [x16, _g_game_ui@PAGEOFF]
    adrp x16, _g_game_ui_update_original@PAGE
    ldr x16, [x16, _g_game_ui_update_original@PAGEOFF]
    br x16

    // Calls ObserveCameraTarget with the arguments of ecl::CameraSystem::CalcCamTargetDest.
    .p2align 2
    .private_extern _CameraTargetProbe
_CameraTargetProbe:
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
    bl _ObserveCameraTarget
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
    adrp x16, _g_camera_target_original@PAGE
    ldr x16, [x16, _g_camera_target_original@PAGEOFF]
    br x16

    // phx::PhysXScene::RaycastAll(this, start, end, ...): AdjustRayStart(start, end, caller) may
    // give another start, which replaces x1. The stack arguments stay where they are.
    .p2align 2
    .private_extern _RaycastProbe
_RaycastProbe:
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
    mov x0, x1
    mov x1, x2
    mov x2, x30
    bl _AdjustRayStart
    mov x16, x0
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
    mov x1, x16
    adrp x17, _g_raycast_original@PAGE
    ldr x17, [x17, _g_raycast_original@PAGEOFF]
    br x17

    // Camera tweaks. Each of the stubs below replaces 16 bytes of game code (patterns.hpp), does
    // the same instructions with the tweak, and jumps back. x16 is free at these places (the game
    // code there doesn't use it), and v16 is kept in the red zone below sp, which Darwin reserves.

    // UpdateGameCameraBehavior: `mov x0, x26 ; mov w1, #0 ; bl GetCameraPitchDegrees ;
    // ldr s1, [x26, #0x164]`. PitchUpdateHook calls GetCameraPitchDegrees and returns the pitch
    // to use. It is a call where the game made one, so the registers it may change are those.
    .p2align 2
    .private_extern _PitchUpdateStub
_PitchUpdateStub:
    mov x0, x26
    mov w1, #0
    bl _PitchUpdateHook
    ldr s1, [x26, #0x164]
    adrp x16, _g_pitch_update_return@PAGE
    ldr x16, [x16, _g_pitch_update_return@PAGEOFF]
    br x16

    // ecl::CameraSystem::OnInputEvent, zoom: `fmul s1, s1, s2 ; fmul s0, s1, s0 ;
    // str s0, [x22, #0xA4] ; b next`. The speed stored is multiplied; s0 keeps the game's value.
    .p2align 2
    .private_extern _ZoomSpeedStub
_ZoomSpeedStub:
    fmul s1, s1, s2
    fmul s0, s1, s0
    stur q16, [sp, #-16]
    adrp x16, _g_zoom_speed_mult@PAGE
    ldr s16, [x16, _g_zoom_speed_mult@PAGEOFF]
    fmul s16, s0, s16
    str s16, [x22, #0xa4]
    ldur q16, [sp, #-16]
    adrp x16, _g_zoom_speed_return@PAGE
    ldr x16, [x16, _g_zoom_speed_return@PAGEOFF]
    br x16

    // The keyboard rotation commands: `fmov s2, w8 ; fmul s1, s1, s2 ; fmul s0, s1, s0 ;
    // str s0, [x22, #0x9C]`.
    .p2align 2
    .private_extern _RotationSpeedStub
_RotationSpeedStub:
    fmov s2, w8
    fmul s1, s1, s2
    fmul s0, s1, s0
    stur q16, [sp, #-16]
    adrp x16, _g_rotation_speed_mult@PAGE
    ldr s16, [x16, _g_rotation_speed_mult@PAGEOFF]
    fmul s16, s0, s16
    str s16, [x22, #0x9c]
    ldur q16, [sp, #-16]
    adrp x16, _g_rotation_speed_return@PAGE
    ldr x16, [x16, _g_rotation_speed_return@PAGEOFF]
    br x16

    // Where the stubs continue: the original functions.
    .data
    .p2align 3
    .private_extern _g_camera_input_original
_g_camera_input_original:
    .quad 0
    .private_extern _g_game_ui_update_original
_g_game_ui_update_original:
    .quad 0
    .private_extern _g_game_ui
_g_game_ui:
    .quad 0
    .private_extern _g_camera_target_original
_g_camera_target_original:
    .quad 0
    .private_extern _g_raycast_original
_g_raycast_original:
    .quad 0
    // Where the camera tweak stubs jump back to.
    .private_extern _g_pitch_update_return
_g_pitch_update_return:
    .quad 0
    .private_extern _g_zoom_speed_return
_g_zoom_speed_return:
    .quad 0
    .private_extern _g_rotation_speed_return
_g_rotation_speed_return:
    .quad 0
    // The speed multipliers, set before each camera command by ObserveCameraInput.
    .private_extern _g_zoom_speed_mult
_g_zoom_speed_mult:
    .float 1.0
    .private_extern _g_rotation_speed_mult
_g_rotation_speed_mult:
    .float 1.0

#endif
