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
};

// A vtable symbol points at the offset-to-top entry; virtual function slots start 16 bytes later.
inline constexpr size_t kVtableAddressPoint = 16;
// OnInputEvent's slot in the ecl::CameraSystem vtable, from the address point.
inline constexpr size_t kCameraOnInputEventSlot = 0x40;

}  // namespace wasd::symbols
