// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.

#pragma once

#include "TelemetrySectionControl.g.h"
#include "pch.h"

#include "ShellRuntime.h"

namespace winrt::MasterControlShell::implementation {

// v0.8.0: per-tile layout state. The TelemetrySectionControl persists
// this map (keyed by tile name) to the runtime data directory so the
// operator's customization survives a shell restart.
struct TelemetryTileLayoutState final {
    bool visible = true;
    bool detached = false;
    int row = 0;
    int column = 0;
    std::wstring sectionId;  // "host-pressure" / "resource-allocation" / "operational-activity"
    int detachedX = 80;
    int detachedY = 80;
    int detachedWidth = 360;
    int detachedHeight = 320;
};

struct TelemetrySectionControl : TelemetrySectionControlT<TelemetrySectionControl> {
    TelemetrySectionControl();

    void ApplySnapshot(const ::MasterControlShell::ShellSnapshot& snapshot);

    // v0.8.0: drag-drop reorder. Each tile Border has CanDrag/AllowDrop;
    // DragStarting stamps the source tile's Tag onto the DataPackage,
    // DragOver authorizes the drop visually, and Drop swaps the source
    // tile with the target tile via Grid.Row/Grid.Column attached
    // properties. Reordering only happens within the same parent Grid
    // section -- cross-section drags are ignored.
    void Tile_DragStarting(Windows::Foundation::IInspectable const& sender,
                           Microsoft::UI::Xaml::DragStartingEventArgs const& e);
    void Tile_DragOver(Windows::Foundation::IInspectable const& sender,
                       Microsoft::UI::Xaml::DragEventArgs const& e);
    void Tile_Drop(Windows::Foundation::IInspectable const& sender,
                   Microsoft::UI::Xaml::DragEventArgs const& e);

    // v0.8.0: per-tile detach + hide buttons. DetachButton_Click pops the
    // tile out into a new Microsoft::UI::Xaml::Window on the desktop;
    // HideButton_Click sets the tile's Visibility to Collapsed (and
    // unchecks it in the Customize flyout).
    void DetachButton_Click(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void HideButton_Click(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::RoutedEventArgs const& e);

    // v0.8.0: Customize flyout reset.
    void ResetLayoutButton_Click(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const& e);

private:
    // Look up a tile Border by its registered name (CpuTile, MemoryTile, ...).
    Microsoft::UI::Xaml::Controls::Border ResolveTileByName(const std::wstring& tileName);
    // Find the parent Grid that currently hosts the tile.
    Microsoft::UI::Xaml::Controls::Grid ResolveSectionGridForTile(const std::wstring& tileName);
    // Section ID lookup ("host-pressure" / "resource-allocation" / ...).
    std::wstring ResolveSectionIdForTile(const std::wstring& tileName);

    // Snapshot a tile's current Grid.Row / Grid.Column / Visibility into the
    // layout map and persist to disk.
    void PersistLayout();
    // Read layout-from-disk and apply Visibility + Grid.SetRow/SetColumn +
    // detached-window-respawn for every tile.
    void LoadAndApplyLayout();
    // Replace persisted layout with the v0.8.0 hardcoded defaults.
    void ResetLayoutToDefaults();
    // Build the seven default-layout entries that match the original XAML
    // grid positions. Used both at first run and by Reset to defaults.
    static std::map<std::wstring, TelemetryTileLayoutState> BuildDefaultLayout();
    // Repopulate the Customize flyout's checkbox column from the current
    // layout state. Each checkbox toggles a tile's Visible flag.
    void RebuildCustomizeFlyoutCheckboxes();
    // Toggle a tile's Visible flag from the Customize flyout.
    void OnTileVisibleCheckboxChanged(const std::wstring& tileName, bool visible);
    // Apply Visibility + reattach to its section Grid based on layout state.
    void ApplyTileVisibility(const std::wstring& tileName, bool visible);

    // v0.8.0: pop a tile to a separate desktop window. The window has a
    // single "Re-attach" button beside the tile content; closing the window
    // also reattaches.
    void DetachTileToDesktop(const std::wstring& tileName);
    void ReattachTile(const std::wstring& tileName);

    // Path to the persisted layout JSON.
    std::wstring LayoutFilePath();

    std::map<std::wstring, TelemetryTileLayoutState> tileLayout_;
    // Active detached windows keyed by tile name. Owns the Window so the
    // window stays alive until we explicitly close it.
    std::map<std::wstring, Microsoft::UI::Xaml::Window> detachedWindows_;
    // Suppress checkbox-change handlers while we rebuild the flyout.
    bool suspendCheckboxHandler_ = false;
    // First snapshot has not landed yet -- avoid persisting layout until we
    // have stable state to capture.
    bool layoutLoaded_ = false;
};

} // namespace winrt::MasterControlShell::implementation

namespace winrt::MasterControlShell::factory_implementation {

struct TelemetrySectionControl : TelemetrySectionControlT<TelemetrySectionControl, implementation::TelemetrySectionControl> {
};

} // namespace winrt::MasterControlShell::factory_implementation
