// ProsperoEden - The launcher's themes, and which one is in use.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/theme.hpp"

#include "pe/core/strings.hpp"

#include <array>
#include <cstdint>

namespace pe::ui
{

namespace
{

Color rgb(std::uint32_t hex, float alpha = 1.0f)
{
    return Color::rgb(hex, alpha);
}

// Canopy: a tropical dusk, dark green glass and lime. ProsperoEden's own look, and the one a
// part missing from any other theme falls back to.
Theme canopy()
{
    Theme t;
    // The kit's frosted glass: translucent panels over the artwork, soft shadows, a ring of light
    // for the focus, movement that settles without a bounce.
    theme_from_kit("acrylic", &t);
    t.id = "canopy";
    t.name = "Canopy";
    t.summary = TR("Tropical dusk, dark green glass and lime.");

    t.base = rgb(0x06090a);
    t.scrim = rgb(0x010806);
    t.glass = rgb(0x16221d);
    t.panel = rgb(0x0b1713);
    t.panel_edge = rgb(0x768e75);
    t.row = rgb(0x17241e);
    t.row_edge = rgb(0x6a8267);
    t.row_focus = rgb(0x294433);

    t.accent = rgb(0xa9db63);
    t.accent_deep = rgb(0x245d4a);
    t.accent_pale = rgb(0xdfe8a6);
    t.on_accent = rgb(0x0b1713);
    t.highlight = rgb(0xffd76a);

    t.text = rgb(0xf0f5f2);
    t.title = rgb(0xfffcef);
    t.value = rgb(0xf2f4e9);
    t.body = rgb(0xe2e8d7);
    t.copy = rgb(0xc5d0bd);
    t.meta = rgb(0xb7c6b5);
    t.label = rgb(0xaebdaa);
    t.muted = rgb(0xaab8ac);
    t.faint = rgb(0x8c9e90);
    t.warning = rgb(0xe8b39a);
    t.rule = rgb(0x536b55);

    t.radius_row = 18.0f;
    t.radius_card = 22.0f;
    t.radius_panel = 30.0f;
    t.pill_chips = true;
    t.focus_width = 2.0f;
    t.focus_glow = 30.0f;
    t.glass_panels = true;

    t.focus_spring = 20.0f;
    t.cursor_spring = 26.0f;
    t.scroll_spring = 16.0f;

    t.backdrop = "backdrop.tga";
    t.backdrop_blur = "backdrop-blur.tga";
    t.brand = "brand.tga";
    t.light_x = 0.710f;
    t.light_y = 0.391f;
    t.motes = 26;
    t.drift = 1.0f;
    return t;
}

// Switch: a handheld console's menu, flat dark grey with a cyan focus. Its own artwork, sounds
// and loading screen (a picture behind it, a walker on a bar, a mark for each step).
Theme switch_theme()
{
    Theme t;
    // The kit's flat tiles: opaque panels, crisp corners, no glass.
    theme_from_kit("tiles", &t);
    t.id = "switch";
    t.name = "Switch";
    t.summary = TR("Flat dark grey and cyan, with its own sounds and loading screen.");

    t.base = rgb(0x1b1c1e);
    t.scrim = rgb(0x0e0f10);
    t.glass = rgb(0x2b2d30);
    t.panel = rgb(0x232426);
    t.panel_edge = rgb(0x5a5d62);
    t.row = rgb(0x323438);
    t.row_edge = rgb(0x55585d);
    t.row_focus = rgb(0x3a4950);

    t.accent = rgb(0x00c3e3);
    t.accent_deep = rgb(0x0b5f78);
    t.accent_pale = rgb(0x9fe9f5);
    t.on_accent = rgb(0x062029);
    t.highlight = rgb(0xe8f8ff);

    t.text = rgb(0xf4f5f6);
    t.title = rgb(0xffffff);
    t.value = rgb(0xf0f1f2);
    t.body = rgb(0xe0e2e4);
    t.copy = rgb(0xc8cbcf);
    t.meta = rgb(0xb6babf);
    t.label = rgb(0xaeb2b7);
    t.muted = rgb(0xa8acb1);
    t.faint = rgb(0x9a9ea4);
    t.warning = rgb(0xff8a7a);
    t.rule = rgb(0x4a4d52);

    t.radius_row = 12.0f;
    t.radius_card = 16.0f;
    t.radius_panel = 20.0f;
    t.pill_chips = false;
    t.focus_width = 3.0f;
    t.focus_glow = 18.0f;
    t.glass_panels = false;

    t.backdrop = "switch-backdrop.tga";
    t.backdrop_blur = "switch-backdrop-blur.tga";
    t.brand = "switch-brand.tga";
    t.light_x = 0.5f;
    t.light_y = 0.3f;
    t.motes = 0;
    t.drift = 0.4f;
    t.sounds = "switch";
    t.loading_picture = "switch-loading.tga";
    t.loading_sprites = true;
    t.carousel_home = true;
    return t;
}

struct Store
{
    std::array<Theme, 2> all{canopy(), switch_theme()};
    int index = 0;
    Theme active = all[0];
};

Store &store()
{
    static Store value;
    return value;
}

} // namespace

const Theme &th()
{
    return store().active;
}

std::span<const Theme> themes()
{
    return store().all;
}

int theme_index()
{
    return store().index;
}

bool use_theme(std::string_view id)
{
    Store &all = store();
    for (std::size_t i = 0; i < all.all.size(); ++i)
    {
        if (id == all.all[i].id)
        {
            all.index = static_cast<int>(i);
            all.active = all.all[i];
            return true;
        }
    }
    return false;
}

} // namespace pe::ui
