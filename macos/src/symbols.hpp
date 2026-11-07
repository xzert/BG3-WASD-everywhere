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
};

// A vtable symbol points at the offset-to-top entry; virtual function slots start 16 bytes later.
inline constexpr size_t kVtableAddressPoint = 16;
// OnInputEvent's slot in the ecl::CameraSystem vtable, from the address point.
inline constexpr size_t kCameraOnInputEventSlot = 0x40;
// How many gui::GameUI vtable slots to search for Update (0x70 in game version 4.1.1.7631656).
inline constexpr size_t kGameUIVtableSlots = 64;

}  // namespace wasd::symbols
