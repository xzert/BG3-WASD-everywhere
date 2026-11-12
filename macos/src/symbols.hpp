#pragma once

#include <cstddef>

// Game symbols the macOS probe looks up, as stored in the symbol table (C++ names start with "__Z").
// The macOS binary keeps its function names, so the port finds code by name instead of signatures.
namespace wasd::symbols {

enum Index {
    CameraSystemVtable,        // vtable for ecl::CameraSystem
    CameraSystemOnInputEvent,  // ecl::CameraSystem::OnInputEvent(ecs::EntityRef const&, ls::InputEvent const&)
    GetRotatedInput,           // (anonymous namespace)::GetRotatedInput(short, bool)
    GetInputValue,             // ls::InputManager::GetInputValue(unsigned const&, ls::EInputPlayerIndex) const
    OnIsGameMenuOpenChanged,   // gui::GameUI::OnIsGameMenuOpenChanged(short, bool)
    SetKeyboardMode,           // ecl::EocClient::SetKeyboardMode(bool)
    MoveControllerCanExecute,  // ecl::CharacterTask_MoveController::CanExecute()
    GameUIVtable,              // vtable for gui::GameUI
    GameUIUpdate,              // gui::GameUI::Update(ls::AnyView const&, ls::GameTime const&)
    IsTextInputElementFocused, // gui::GameUI::IsTextInputElementFocused() const
    AddForcedMouseCursor,      // ecl::CursorControl::AddForcedMouseCursor(ecl::EMouseCursorMode)
    RemoveForcedMouseCursor,   // ecl::CursorControl::RemoveForcedMouseCursor(ecl::EMouseCursorMode)
    Core,                      // gCore: the game's cursor position is the first two floats it points to
    RecordDialogStart,         // ecl::GameAnalyticsSystem::RecordDialogStart(ecl::Character const*, char const*, ls::STDString const&, bool) const
    RecordDialogEnd,           // ecl::GameAnalyticsSystem::RecordDialogEnd(ecl::Character const*, ls::FixedString const&, ls::STDString const&, bool) const
    IsFading,                  // ls::IsFading(ls::ID<ecs::EntityHandleTraits> const&, ecs::WorldView<...> const&, signed char)
    OnSelectedCharacterChanged,  // gui::GameUI::OnSelectedCharacterChanged(short, ecs::EntityRef const&, bool)
    // The picking code that asks IsFading, in ecl::PickingHelper.
    PickGameObject,
    PickPlaceable,
    PickWalkable,
    PickScenery,
    PickingDoPick,
    PickingUpdate,
    PickingForEachSelectableTarget,
    CalcCamTargetDest,         // ecl::CameraSystem::CalcCamTargetDest(EntityView<GameCameraBehavior, ...> const&): x1 points at the GameCameraBehavior*
    PhysXSceneVtable,          // vtable for phx::PhysXScene
    RaycastAll,                // phx::PhysXScene::RaycastAll(Vector3f const& start, Vector3f const& end, ...) const
    GetCameraPitchDegrees,     // ecl::GameCameraBehavior::GetCameraPitchDegrees(bool, bool) const
    UpdateGameCameraBehavior,  // ecl::CameraSystem::UpdateGameCameraBehavior(ecs::WorldView<...>&, ls::GameTime const&, ...)
    Count
};

inline constexpr const char* kNames[Count] = {
    "__ZTVN3ecl12CameraSystemE",
    "__ZN3ecl12CameraSystem12OnInputEventERKN3ecs9EntityRefERKN2ls10InputEventE",
    "__ZN12_GLOBAL__N_115GetRotatedInputEsb",
    "__ZNK2ls12InputManager13GetInputValueERKjNS_17EInputPlayerIndexE",
    "__ZN3gui6GameUI23OnIsGameMenuOpenChangedEsb",
    "__ZN3ecl9EocClient15SetKeyboardModeEb",
    "__ZN3ecl28CharacterTask_MoveController10CanExecuteEv",
    "__ZTVN3gui6GameUIE",
    "__ZN3gui6GameUI6UpdateERKN2ls7AnyViewERKNS1_8GameTimeE",
    "__ZNK3gui6GameUI25IsTextInputElementFocusedEv",
    "__ZN3ecl13CursorControl20AddForcedMouseCursorENS_16EMouseCursorModeE",
    "__ZN3ecl13CursorControl23RemoveForcedMouseCursorENS_16EMouseCursorModeE",
    "_gCore",
    "__ZNK3ecl19GameAnalyticsSystem17RecordDialogStartEPKNS_9CharacterEPKcRKN2ls9STDStringEb",
    "__ZNK3ecl19GameAnalyticsSystem15RecordDialogEndEPKNS_9CharacterERKN2ls11FixedStringERKNS4_9STDStringEb",
    "__ZN2ls8IsFadingERKNS_2IDIN3ecs18EntityHandleTraitsEEERKNS1_9WorldViewIJKNS_16ConstructionTileEKNS_15EffectComponentEKNS_20FadingView0ComponentEKNS_20FadingView1ComponentEKNS_23InvisibleClimbingHelperEKNS_15VisualComponentEEEEa",
    "__ZN3gui6GameUI26OnSelectedCharacterChangedEsRKN3ecs9EntityRefEb",
    "__ZN3ecl13PickingHelper14GameObjectPick15ExecuteWTKernelEv",
    "__ZN3ecl13PickingHelper13PlaceablePick15ExecuteWTKernelEv",
    "__ZN3ecl13PickingHelper12WalkablePick15ExecuteWTKernelEv",
    "__ZN3ecl13PickingHelper11SceneryPick15ExecuteWTKernelEv",
    "__ZN3ecl13PickingHelper6DoPickEv",
    "__ZN3ecl13PickingHelper6UpdateERKN2ls8GameTimeE",
    "__ZN2ls8FunctionIFbNS_10PhysicsHitEEE18FunctorMethodsHeapIZN3ecl13PickingHelper23ForEachSelectableTargetERKNS_8GameTimeEPKNS5_9CharacterEbbNS0_IFvRN3ecs9EntityRefEEEEE3$_5E4CallERNS3_7StorageES1_",
    "__ZN3ecl12CameraSystem17CalcCamTargetDestERKN3ecs8_private10EntityViewIJN2ls8TypeListIJNS_18GameCameraBehaviorEKNS_17EocCameraBehaviorEKNS4_18TransformComponentEEEENS5_IJEEEEEE",
    "__ZTVN3phx10PhysXSceneE",
    "__ZNK3phx10PhysXScene10RaycastAllERK8Vector3fS3_RN2ls13PhysicsHitAllENS4_12EPhysicsTypeEjjNS4_15EPhysicsContextEjjNS4_8OptionalIRNS4_26PhysicsSceneScopedReadLockEEE",
    "__ZNK3ecl18GameCameraBehavior21GetCameraPitchDegreesEbb",
    // UpdateGameCameraBehavior: the WorldView lists some 90 component types.
    "__ZN3ecl12CameraSystem24UpdateGameCameraBehaviorERN3ecs9WorldViewIJNS_17AtmosphereTriggerENS"
    "_6camera45ClearScreenFadeRequestManualOneFrameComponentENS4_39EnterPhotoModeRequestSingleton"
    "ComponentENS4_42ScreenFadeToRequestManualOneFrameComponentENS_17CameraLockTriggerENS_9Charac"
    "terENS_17EocCameraBehaviorENS_18GameCameraBehaviorENS_15LightingTriggerENS_19SceneCameraBeha"
    "viorENS_23SpectatorCameraBehaviorEN2ls21ArcBallCameraBehaviorENSF_15CameraComponentENSF_21De"
    "faultCameraBehaviorENSF_20EditorCameraBehaviorENSF_20EffectCameraBehaviorENSF_14LevelCompone"
    "ntENSF_19OrthoCameraBehaviorENSF_18TransformComponentENSF_15VisualComponentENSF_22WorldMapCa"
    "meraBehaviorEKNS_20CameraBlockerTriggerEKNS4_32PhotoModeCameraBehaviorComponentEKNS4_23Platf"
    "ormTargetComponentEKNS4_21SelectorModeComponentEKNS4_15TargetComponentEKNS_18character_creat"
    "ion15CameraComponentEKNS10_21SessionStateComponentEKSA_KNS_27InvisibilityVisualComponentEKNS"
    "_7SceneryEKNS_11screen_fade37ScreenFadeToFinishedOneFrameComponentEKNS1A_45ScreenFadeToReque"
    "stCancelledOneFrameComponentEKNS1A_29ScreenFadeVisualDataComponentEKNS_16SpectatorTriggerEKN"
    "S_10spell_cast14StateComponentEKNS_11splitscreen27AnimationSingletonComponentEKNS1M_22GoalSi"
    "ngletonComponentEKNS1M_29MergedFocusSingletonComponentEKNS1M_25PlayersSingletonComponentEKNS"
    "1M_35SelectedAnimationSingletonComponentEKNS1M_22ViewSingletonComponentEKNS_39TimelineActorL"
    "eftEventOneFrameComponentEKNS_27UnloadSessionEventComponentEKN3eoc14BoundComponentEKNS23_9ch"
    "aracter18CharacterComponentEKNS23_18character_creation10definition17CreationComponentEKNS23_"
    "22ClientControlComponentEKNS23_6combat20ParticipantComponentEKNS23_10controller19LocomotionC"
    "omponentEKNS2I_18NudgeDataComponentEKNS23_3ftb20ParticipantComponentEKNS2N_16RespectComponen"
    "tEKNS23_4item13ItemComponentEKNS23_4jump13InfoComponentEKNS23_17MovementComponentEKNS23_11mu"
    "ltiplayer13UserComponentEKNS23_5party17FollowerComponentEKNS33_15MemberComponentEKNS33_13Vie"
    "wComponentEKNS23_10photo_mode16SessionComponentEKNS23_15PlayerComponentEKNS23_10spell_cast18"
    "IsCastingComponentEKNS23_7trigger13TreeComponentEKNS23_18TurnBasedComponentEKNS23_18TurnOrde"
    "rComponentEKNSF_14DebugComponentEKNSF_28FadeableObstructionComponentEKNSF_16PhysicsComponent"
    "EKNSF_15SplineComponentEKNSF_13TerrainObjectEKNSF_7trigger13AreaComponentEKNSF_4uuid9Compone"
    "ntEEEERKNSF_8GameTimeERKNS1_8_private10EntityViewIJNSF_8TypeListIJSB_SH_S15_KSN_EEENS4C_IJSV"
    "_SX_S2M_EEEEEE",
};

// A vtable symbol points at the offset-to-top entry; virtual function slots start 16 bytes later.
inline constexpr size_t kVtableAddressPoint = 16;
// OnInputEvent's slot in the ecl::CameraSystem vtable, from the address point.
inline constexpr size_t kCameraOnInputEventSlot = 0x40;
// How many gui::GameUI vtable slots to search for Update (0x70 in game version 4.1.1.7631656).
inline constexpr size_t kGameUIVtableSlots = 64;

}  // namespace wasd::symbols
