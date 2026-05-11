// Master Control Orchestration Server
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential.
//
// Shared imperative card-grid renderer for the WinUI Shell. Builds a stack
// of "endpoint stat" cards (one per registered MCP server or sub-agent)
// from a vector of ShellSubAgentRuntimeStat / ShellMcpServerRuntimeStat.
//
// Two visual modes:
//
//   compact == false (Runtime + Overview surfaces):
//     Wide stacked cards. Each card is a separate Border in the parent
//     StackPanel with multi-line content (name + spec, util row, endpoint
//     host:port row, pool note, per-client rows). ~140 pixels tall.
//
//   compact == true (Telemetry surface):
//     The "tile grid" layout, matching the SUB-AGENT GRID cross-tab footer
//     built by MainWindow::ApplySubAgentFooter. Each tile is a small
//     bordered Border (~150x80 px) carrying:
//       - title row: NAME (uppercase, 11pt, semibold, cyan) + 8x8 dot
//       - spec line (10pt, neutral) -- if present
//       - util row: util% label (10pt) + ProgressBar (h=4) + active/cap
//       - clients block: "no clients" / "ip (type)" lines + "+N more"
//     Tiles render side by side in a fixed-column Grid and stack
//     vertically as additional rows when the item count exceeds the
//     column count. The Telemetry deck thus shows the MCP server
//     inventory + the sub-agent inventory in the same dense tile grid
//     the operator already reads at the bottom of every other tab.
//
// Originally lived as a translation-unit-private template inside
// RuntimeSectionControl.xaml.cpp (v0.8.3). Extracted to this header so
// TelemetrySectionControl can reuse the exact same renderer without
// duplicating ~250 lines of WinRT card-construction code. The compact
// tile-grid path was added in v0.10.7 after operator clarification that
// the SUB-AGENT GRID footer tile shape is the reference visual the
// Telemetry MCP + Sub-Agent panels must match.

#pragma once

#include "pch.h"

#include "ShellRuntime.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace winrt::MasterControlShell::implementation {

// Stat-kind-overloaded id accessor so the templated card builder below
// can resolve the per-kind ID field (subAgentId vs mcpServerId)
// uniformly. Used as the fallback display label when the stat's
// displayName is empty.
inline const std::wstring& endpointStatId(const ::MasterControlShell::ShellSubAgentRuntimeStat& s) {
    return s.subAgentId;
}
inline const std::wstring& endpointStatId(const ::MasterControlShell::ShellMcpServerRuntimeStat& s) {
    return s.mcpServerId;
}

namespace endpoint_stat_card_grid_detail {

// Build one "footer-style" tile Border for a single stat. Matches the
// per-tile shape produced by MainWindow::ApplySubAgentFooter v0.7.8 so
// the Telemetry MCP + Sub-Agent panels are visually flush with the
// cross-tab footer tiles the operator pointed at as the reference.
template <class StatT>
winrt::Microsoft::UI::Xaml::Controls::Border buildFooterStyleTile(
    const StatT& stat,
    const wchar_t* fallbackName) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;
    using winrt::Windows::UI::ColorHelper;

    auto fromHex = [](uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
        return ColorHelper::FromArgb(a, r, g, b);
    };
    const auto goodColor    = fromHex(0x1c, 0xf2, 0xc1);
    const auto warnColor    = fromHex(0xff, 0xc8, 0x57);
    const auto critColor    = fromHex(0xff, 0x6a, 0x80);
    const auto neutralColor = fromHex(0x8c, 0xb7, 0xc4);
    const auto titleColor   = fromHex(0xb8, 0xff, 0xf0);
    const auto cardEdge     = fromHex(0xff, 0x3d, 0x2e, 0x55);
    const auto cardFill     = fromHex(0xff, 0x3d, 0x2e, 0x18);
    const auto barTrack     = fromHex(0x8c, 0xb7, 0xc4, 0x40);

    Border card;
    card.Background(SolidColorBrush(cardFill));
    card.BorderBrush(SolidColorBrush(cardEdge));
    Thickness cardBorder;
    cardBorder.Left = 1.0; cardBorder.Top = 1.0; cardBorder.Right = 1.0; cardBorder.Bottom = 1.0;
    card.BorderThickness(cardBorder);
    winrt::Microsoft::UI::Xaml::CornerRadius cardCorners;
    cardCorners.TopLeft = 6.0; cardCorners.TopRight = 6.0;
    cardCorners.BottomRight = 6.0; cardCorners.BottomLeft = 6.0;
    card.CornerRadius(cardCorners);
    Thickness cardPadding;
    cardPadding.Left = 8.0; cardPadding.Top = 6.0;
    cardPadding.Right = 8.0; cardPadding.Bottom = 6.0;
    card.Padding(cardPadding);

    StackPanel inner;
    inner.Spacing(4);

    // Title row: name (uppercase) + reachability dot.
    StackPanel titleRow;
    titleRow.Orientation(Orientation::Horizontal);
    titleRow.Spacing(6);

    TextBlock nameText;
    std::wstring rawName = stat.displayName.empty() ? endpointStatId(stat) : stat.displayName;
    if (rawName.empty()) {
        rawName = (fallbackName != nullptr) ? std::wstring(fallbackName) : std::wstring(L"(unnamed)");
    }
    std::wstring upperName = rawName;
    std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(::towupper(c)); });
    nameText.Text(winrt::hstring(upperName));
    nameText.FontSize(11);
    nameText.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
    nameText.Foreground(SolidColorBrush(titleColor));
    nameText.TextTrimming(TextTrimming::CharacterEllipsis);
    nameText.VerticalAlignment(VerticalAlignment::Center);

    Border dot;
    dot.Width(8); dot.Height(8);
    winrt::Microsoft::UI::Xaml::CornerRadius dotCorners;
    dotCorners.TopLeft = 4.0; dotCorners.TopRight = 4.0;
    dotCorners.BottomRight = 4.0; dotCorners.BottomLeft = 4.0;
    dot.CornerRadius(dotCorners);
    dot.VerticalAlignment(VerticalAlignment::Center);
    const bool hasEndpoint = !stat.endpointHostPort.empty();
    const auto dotColor = hasEndpoint
        ? (stat.reachable ? goodColor : critColor)
        : neutralColor;
    dot.Background(SolidColorBrush(dotColor));

    titleRow.Children().Append(nameText);
    titleRow.Children().Append(dot);
    inner.Children().Append(titleRow);

    // Specialization sub-text (mirrors the SUB-AGENT GRID footer tile).
    if (!stat.specialization.empty()) {
        TextBlock specText;
        specText.Text(winrt::hstring(stat.specialization));
        specText.FontSize(10);
        specText.Foreground(SolidColorBrush(neutralColor));
        specText.TextTrimming(TextTrimming::CharacterEllipsis);
        inner.Children().Append(specText);
    }

    // Utilization bar + percent + active/capacity ratio.
    const double util = stat.utilizationPercent;
    const auto barTone = (util >= 95.0) ? critColor
                        : (util >= 75.0) ? warnColor
                        : goodColor;

    StackPanel utilRow;
    utilRow.Orientation(Orientation::Horizontal);
    utilRow.Spacing(4);

    TextBlock utilLabel;
    wchar_t pctBuf[16]{};
    swprintf_s(pctBuf, L"%.0f%%", util < 0 ? 0.0 : util);
    utilLabel.Text(pctBuf);
    utilLabel.FontSize(10);
    utilLabel.Foreground(SolidColorBrush(barTone));
    utilLabel.MinWidth(28);
    utilRow.Children().Append(utilLabel);

    ProgressBar bar;
    bar.Minimum(0);
    bar.Maximum(100);
    bar.Value(util < 0 ? 0.0 : (util > 100.0 ? 100.0 : util));
    bar.Foreground(SolidColorBrush(barTone));
    bar.Background(SolidColorBrush(barTrack));
    bar.Height(4);
    bar.HorizontalAlignment(HorizontalAlignment::Stretch);
    bar.MinWidth(60);
    utilRow.Children().Append(bar);

    TextBlock ratioText;
    wchar_t ratioBuf[32]{};
    swprintf_s(ratioBuf, L"%d/%d",
               stat.activeLeaseCount,
               stat.leaseCapacity);
    ratioText.Text(ratioBuf);
    ratioText.FontSize(10);
    ratioText.Foreground(SolidColorBrush(neutralColor));
    utilRow.Children().Append(ratioText);

    inner.Children().Append(utilRow);

    // Active client IPs. Cap at 2 lines and append "+N more" when over;
    // empty list shows "no clients" in muted color so the operator can
    // tell at a glance which endpoints are idle vs in use. Matches the
    // SUB-AGENT GRID footer tile.
    TextBlock clientsText;
    clientsText.FontSize(10);
    clientsText.TextWrapping(TextWrapping::Wrap);
    clientsText.Foreground(SolidColorBrush(neutralColor));
    if (stat.activeClients.empty()) {
        clientsText.Text(L"no clients");
    } else {
        std::wstring acc;
        const size_t shown = std::min<size_t>(2, stat.activeClients.size());
        for (size_t i = 0; i < shown; ++i) {
            if (i > 0) acc += L'\n';
            const auto& holder = stat.activeClients[i];
            acc += holder.ipAddress.empty() ? std::wstring(L"unknown") : holder.ipAddress;
            if (!holder.clientType.empty()) {
                acc += L" (";
                acc += holder.clientType;
                acc += L")";
            }
        }
        if (stat.activeClients.size() > shown) {
            acc += L"\n+";
            acc += std::to_wstring(stat.activeClients.size() - shown);
            acc += L" more";
        }
        clientsText.Text(winrt::hstring(acc));
    }
    inner.Children().Append(clientsText);

    card.Child(inner);
    return card;
}

} // namespace endpoint_stat_card_grid_detail

// Templated card-grid renderer shared between RuntimeSectionControl,
// TelemetrySectionControl, and any future surface that displays the same
// stat shape. Both stat structs carry the same field set apart from the
// ID, which we resolve via the overloaded endpointStatId() helper above.
//
// Parameters:
//   stack           - destination StackPanel; cleared and rebuilt on each call.
//   headlineText    - one-line summary TextBlock above the grid ("N foos
//                     registered ; M reachable.").
//   stats           - the data; one card per element.
//   kindNoun        - singular label used in the headline ("sub-agent" /
//                     "MCP server").
//   emptyMessage    - text shown in headlineText when stats is empty
//                     (the grid renders nothing).
//   poolHintNoPool  - muted note rendered on cards whose stat has no
//                     managed pool wrapping it.
//   compact         - true for the Telemetry deck. Produces the footer-style
//                     tile grid (small bordered tiles, side-by-side, wrapping
//                     to additional rows). false produces the wide stacked
//                     card form for the Runtime + Overview surfaces.
template <class StatT>
void renderEndpointStatCardGrid(
    winrt::Microsoft::UI::Xaml::Controls::StackPanel stack,
    winrt::Microsoft::UI::Xaml::Controls::TextBlock headlineText,
    const std::vector<StatT>& stats,
    const wchar_t* kindNoun,
    const wchar_t* emptyMessage,
    const wchar_t* poolHintNoPool,
    bool compact = false) {
    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Controls;
    using namespace winrt::Microsoft::UI::Xaml::Media;
    using winrt::Windows::Foundation::IInspectable;
    using winrt::Windows::UI::Color;
    using winrt::Windows::UI::ColorHelper;

    stack.Children().Clear();

    if (stats.empty()) {
        headlineText.Text(winrt::hstring(emptyMessage));
        return;
    }

    int reachableCount = 0;
    for (const auto& s : stats) {
        if (s.reachable) ++reachableCount;
    }
    std::wstring kindPlural = std::wstring(kindNoun);
    if (stats.size() != 1) kindPlural += L"s";
    std::wstring headline = std::to_wstring(stats.size()) + L" " + kindPlural
        + L" registered ; " + std::to_wstring(reachableCount) + L" reachable.";
    headlineText.Text(winrt::hstring(headline));

    // v0.10.7: compact path -- build a multi-row Grid of footer-style
    // tiles. Each tile is the same shape as the SUB-AGENT GRID footer
    // (MainWindow::ApplySubAgentFooter). The grid uses a fixed column
    // count so tiles render side-by-side and stack vertically when the
    // count exceeds one row. 7 tiles per row picks up the same density
    // as the existing 7-wide SUB-AGENT GRID footer on this host's 7
    // sub-agents; 26 MCP servers then wrap to 4 rows (7+7+7+5).
    if (compact) {
        // Restore the XAML-declared row spacing in case a prior render
        // tightened it; the tile grid handles its own vertical pitch.
        stack.Spacing(10.0);

        constexpr int kColumnCount = 7;
        const int total = static_cast<int>(stats.size());
        const int rowCount = (total + kColumnCount - 1) / kColumnCount;

        Grid tileGrid;
        tileGrid.ColumnSpacing(10);
        tileGrid.RowSpacing(10);
        for (int c = 0; c < kColumnCount; ++c) {
            ColumnDefinition col;
            col.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            tileGrid.ColumnDefinitions().Append(col);
        }
        for (int r = 0; r < rowCount; ++r) {
            RowDefinition row;
            row.Height(GridLengthHelper::Auto());
            tileGrid.RowDefinitions().Append(row);
        }

        for (int i = 0; i < total; ++i) {
            auto tile = endpoint_stat_card_grid_detail::buildFooterStyleTile(stats[i], kindNoun);
            Grid::SetRow(tile, i / kColumnCount);
            Grid::SetColumn(tile, i % kColumnCount);
            tileGrid.Children().Append(tile);
        }

        stack.Children().Append(tileGrid);
        return;
    }

    // Wide (non-compact) path -- one stacked Border card per stat, used
    // by Runtime + Overview surfaces. Unchanged from earlier releases.
    auto fromHex = [](uint8_t r, uint8_t g, uint8_t b) {
        return ColorHelper::FromArgb(0xFF, r, g, b);
    };
    const auto goodColor    = fromHex(0x1c, 0xf2, 0xc1);
    const auto warnColor    = fromHex(0xff, 0xaf, 0x3a);
    const auto critColor    = fromHex(0xff, 0x3a, 0x5a);
    const auto neutralColor = fromHex(0x8c, 0xb7, 0xc4);

    const double cardPadH       = 14.0;
    const double cardPadV       = 12.0;
    const double innerSpacing   = 6.0;
    const double nameFontSize   = 16.0;
    const double specFontSize   = 12.0;
    const double utilLabelFont  = 20.0;
    const double utilBarHeight  = 10.0;
    const double utilCol1Width  = 64.0;
    const double utilCol3Width  = 80.0;

    for (const auto& stat : stats) {
        Border card;
        card.Background(SolidColorBrush(ColorHelper::FromArgb(0x18, 0xff, 0x3d, 0x2e)));
        card.BorderBrush(SolidColorBrush(ColorHelper::FromArgb(0x55, 0xff, 0x3d, 0x2e)));
        Thickness cardBorder;
        cardBorder.Left = 1.0;
        cardBorder.Top = 1.0;
        cardBorder.Right = 1.0;
        cardBorder.Bottom = 1.0;
        card.BorderThickness(cardBorder);
        winrt::Microsoft::UI::Xaml::CornerRadius cardCorners;
        cardCorners.TopLeft = 8.0;
        cardCorners.TopRight = 8.0;
        cardCorners.BottomRight = 8.0;
        cardCorners.BottomLeft = 8.0;
        card.CornerRadius(cardCorners);
        Thickness cardPadding;
        cardPadding.Left = cardPadH;
        cardPadding.Top = cardPadV;
        cardPadding.Right = cardPadH;
        cardPadding.Bottom = cardPadV;
        card.Padding(cardPadding);

        StackPanel inner;
        inner.Spacing(innerSpacing);

        // Header: name + specialization.
        StackPanel header;
        header.Orientation(Orientation::Horizontal);
        header.Spacing(10);
        TextBlock nameText;
        nameText.Text(winrt::hstring(stat.displayName.empty() ? endpointStatId(stat) : stat.displayName));
        nameText.FontSize(nameFontSize);
        nameText.FontWeight(winrt::Windows::UI::Text::FontWeight{600});
        TextBlock specText;
        specText.Text(winrt::hstring(stat.specialization.empty() ? L"(no specialization)" : stat.specialization));
        specText.Foreground(SolidColorBrush(neutralColor));
        specText.FontSize(specFontSize);
        header.Children().Append(nameText);
        header.Children().Append(specText);
        inner.Children().Append(header);

        // Utilization row: percent label + ProgressBar + active/capacity ratio.
        const double utilization = (stat.utilizationPercent < 0) ? 0.0 : stat.utilizationPercent;
        const auto toneColor = (utilization >= 95) ? critColor
                              : (utilization >= 75) ? warnColor
                              : goodColor;
        Grid utilRow;
        ColumnDefinition col1; col1.Width(GridLengthHelper::FromPixels(utilCol1Width));
        ColumnDefinition col2; col2.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        ColumnDefinition col3; col3.Width(GridLengthHelper::FromPixels(utilCol3Width));
        utilRow.ColumnDefinitions().Append(col1);
        utilRow.ColumnDefinitions().Append(col2);
        utilRow.ColumnDefinitions().Append(col3);
        utilRow.ColumnSpacing(10);

        TextBlock utilLabel;
        wchar_t utilBuf[16];
        swprintf_s(utilBuf, L"%d%%", static_cast<int>(utilization));
        utilLabel.Text(utilBuf);
        utilLabel.FontSize(utilLabelFont);
        utilLabel.FontWeight(winrt::Windows::UI::Text::FontWeight{600});
        utilLabel.Foreground(SolidColorBrush(toneColor));
        Grid::SetColumn(utilLabel, 0);

        ProgressBar utilBar;
        utilBar.Minimum(0);
        utilBar.Maximum(100);
        utilBar.Value(utilization);
        utilBar.Height(utilBarHeight);
        utilBar.Foreground(SolidColorBrush(toneColor));
        utilBar.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(utilBar, 1);

        TextBlock countsText;
        wchar_t countsBuf[32];
        swprintf_s(countsBuf, L"%d / %d", stat.activeLeaseCount, stat.leaseCapacity);
        countsText.Text(countsBuf);
        countsText.FontSize(11);
        countsText.Foreground(SolidColorBrush(neutralColor));
        countsText.HorizontalAlignment(HorizontalAlignment::Right);
        countsText.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(countsText, 2);

        utilRow.Children().Append(utilLabel);
        utilRow.Children().Append(utilBar);
        utilRow.Children().Append(countsText);
        inner.Children().Append(utilRow);

        // Endpoint + reachability row.
        if (!stat.endpointHostPort.empty()) {
            StackPanel epRow;
            epRow.Orientation(Orientation::Horizontal);
            epRow.Spacing(8);
            Border dot;
            dot.Width(10);
            dot.Height(10);
            winrt::Microsoft::UI::Xaml::CornerRadius dotCorners;
            dotCorners.TopLeft = 5.0;
            dotCorners.TopRight = 5.0;
            dotCorners.BottomRight = 5.0;
            dotCorners.BottomLeft = 5.0;
            dot.CornerRadius(dotCorners);
            dot.VerticalAlignment(VerticalAlignment::Center);
            dot.Background(SolidColorBrush(stat.reachable ? goodColor : critColor));
            TextBlock epText;
            epText.Text(winrt::hstring(stat.endpointHostPort));
            epText.FontSize(12);
            epText.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(winrt::hstring{L"Consolas, Cascadia Mono, Courier New"}));
            TextBlock statusText;
            statusText.Text(winrt::hstring(stat.status.empty() ? L"unknown" : stat.status));
            statusText.FontSize(11);
            statusText.Foreground(SolidColorBrush(neutralColor));
            epRow.Children().Append(dot);
            epRow.Children().Append(epText);
            epRow.Children().Append(statusText);
            inner.Children().Append(epRow);
        }

        // Pool / no-pool note.
        TextBlock poolNote;
        if (!stat.poolId.empty()) {
            wchar_t poolBuf[256];
            swprintf_s(poolBuf, L"Pool %ls : %d/%d Ready : max %d : autoscale %ls",
                       stat.poolId.c_str(),
                       stat.readyInstanceCount,
                       stat.totalInstanceCount,
                       stat.maxInstancesAllowed,
                       stat.autoscaleEnabled ? L"on" : L"off");
            poolNote.Text(poolBuf);
        } else {
            poolNote.Text(winrt::hstring(poolHintNoPool));
        }
        poolNote.FontSize(11);
        poolNote.Foreground(SolidColorBrush(neutralColor));
        poolNote.TextWrapping(TextWrapping::Wrap);
        inner.Children().Append(poolNote);

        // Active client list.
        TextBlock clientsHeader;
        wchar_t clientsBuf[64];
        swprintf_s(clientsBuf, L"Active clients (%zu)", stat.activeClients.size());
        clientsHeader.Text(clientsBuf);
        clientsHeader.FontSize(11);
        clientsHeader.Foreground(SolidColorBrush(neutralColor));
        clientsHeader.Margin(Thickness{0, 4.0, 0, 2.0});
        inner.Children().Append(clientsHeader);

        if (stat.activeClients.empty()) {
            TextBlock noClients;
            const std::wstring noClientsLine = std::wstring(L"No active clients leasing this ") + kindNoun + L".";
            noClients.Text(winrt::hstring(noClientsLine));
            noClients.FontSize(11);
            noClients.Foreground(SolidColorBrush(neutralColor));
            inner.Children().Append(noClients);
        } else {
            for (const auto& holder : stat.activeClients) {
                StackPanel clientRow;
                clientRow.Orientation(Orientation::Horizontal);
                clientRow.Spacing(8);
                TextBlock ipText;
                ipText.Text(winrt::hstring(holder.ipAddress.empty() ? L"unknown" : holder.ipAddress));
                ipText.FontFamily(winrt::Microsoft::UI::Xaml::Media::FontFamily(winrt::hstring{L"Consolas, Cascadia Mono, Courier New"}));
                ipText.FontSize(12);
                TextBlock typeText;
                typeText.Text(winrt::hstring(holder.clientType.empty() ? L"unknown-client" : holder.clientType));
                typeText.FontSize(11);
                typeText.Foreground(SolidColorBrush(neutralColor));
                clientRow.Children().Append(ipText);
                clientRow.Children().Append(typeText);
                inner.Children().Append(clientRow);
            }
        }

        card.Child(inner);
        stack.Children().Append(card);
    }
}

} // namespace winrt::MasterControlShell::implementation
