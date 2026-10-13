// ProsperoEden - Launcher home as a carousel: every game on one row, the last played first, and the
// sections as round buttons under it instead of the rail. A theme's choice (Theme::carousel_home).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <string>

namespace pe::ui
{

using audio::Cue;

namespace
{

constexpr float kPi = 3.14159265f;
const Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

// The top bar: who is playing at the left, the controllers and the clock (draw_clock) at the right.
constexpr float kBarY = 76.0f; // its centre line, the clock's
constexpr float kLeft = 120.0f;
constexpr float kRight = layout::kRight;
// The carousel: square cards side by side, the selected game's name over them, its details under.
constexpr float kCardTop = 292.0f;
constexpr float kCardSize = 380.0f;
constexpr float kCardPitch = 412.0f;
constexpr int kCardsShown = 4;
// The section buttons, round, centred under the carousel.
constexpr float kButtonY = 846.0f;
constexpr float kButtonRadius = 46.0f;
constexpr float kButtonPitch = 150.0f;

struct Button
{
    Icon icon;
    const char *name;
};
// home_focus_ 1 to 5.
constexpr Button kButtons[] = {{Icon::library, TR("Library")},
                               {Icon::multiplayer, TR("Multiplayer")},
                               {Icon::profile, TR("Profiles")},
                               {Icon::settings, TR("Settings")},
                               {Icon::about, TR("About")}};
constexpr int kButtonCount = static_cast<int>(std::size(kButtons));
constexpr int kFirstButtonDown = 3; // Down from the carousel: the one in the middle

float button_x(int index)
{
    return 960.0f + kButtonPitch * (static_cast<float>(index) - 0.5f * static_cast<float>(kButtonCount - 1));
}

} // namespace

bool Launcher::carousel_home() const
{
    return th().carousel_home && screen_ == Screen::home;
}

void Launcher::sync_carousel()
{
    // ponytail: read again only when the game list or the recent games are other vectors, or of
    // another size; a list rebuilt in place at the same size keeps its old order until then.
    static const void *games_at = nullptr;
    static const void *recents_at = nullptr;
    static std::size_t games_size = 0;
    static std::size_t recents_size = 0;
    if (games_at == games_.data() && games_size == games_.size() && recents_at == home_.recents.data() &&
        recents_size == home_.recents.size() && carousel_order_.size() == games_.size())
        return;
    games_at = games_.data();
    games_size = games_.size();
    recents_at = home_.recents.data();
    recents_size = home_.recents.size();

    // The recently played first, the latest first; then the others as the Library has them.
    std::vector<std::size_t> rank(games_.size(), home_.recents.size());
    for (std::size_t i = 0; i < games_.size(); ++i)
        for (std::size_t r = 0; r < home_.recents.size(); ++r)
            if (home_.recents[r].file == games_[i].file)
            {
                rank[i] = r;
                break;
            }
    std::vector<int> order(games_.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(),
                     [&rank](int a, int b) { return rank[static_cast<std::size_t>(a)] < rank[static_cast<std::size_t>(b)]; });

    // The selection stays on its game; the first time, on the last one played.
    const Game *held = carousel_game();
    const std::string file = held != nullptr ? held->file : std::string{};
    carousel_order_ = std::move(order);
    int index = 0;
    for (int i = 0; !file.empty() && i < static_cast<int>(carousel_order_.size()); ++i)
        if (games_[static_cast<std::size_t>(carousel_order_[static_cast<std::size_t>(i)])].file == file)
            index = i;
    carousel_.visible = kCardsShown;
    carousel_.pitch = kCardPitch;
    carousel_.wrap = false;
    carousel_.reset(static_cast<int>(carousel_order_.size()), index);
}

Game *Launcher::carousel_game()
{
    if (carousel_order_.empty() || carousel_.selected < 0 ||
        carousel_.selected >= static_cast<int>(carousel_order_.size()))
        return nullptr;
    const int at = carousel_order_[static_cast<std::size_t>(carousel_.selected)];
    return at >= 0 && at < static_cast<int>(games_.size()) ? &games_[static_cast<std::size_t>(at)] : nullptr;
}

void Launcher::press_carousel_home(Key key)
{
    if (home_focus_ <= 0 || home_focus_ > kButtonCount)
    {
        home_focus_ = 0;
        return press_carousel(key);
    }
    const int before = home_focus_;
    switch (key)
    {
    case Key::left:
        home_focus_ = std::max(1, home_focus_ - 1);
        break;
    case Key::right:
        home_focus_ = std::min(kButtonCount, home_focus_ + 1);
        break;
    case Key::up:
        home_focus_ = 0;
        break;
    case Key::circle:
        home_focus_ = 0;
        cue(Cue::back);
        return;
    case Key::cross:
        press_ = 1.0f;
        switch (home_focus_)
        {
        case 1:
            return go_section(1); // Library
        case 2:
            return go_section(2); // Multiplayer
        case 3:
            return open_profiles();
        case 4:
            return go_section(3); // Settings
        default:
            return go_section(4); // About
        }
    default:
        return;
    }
    if (home_focus_ != before)
        cue(Cue::focus);
}

void Launcher::press_carousel(Key key)
{
    Game *game = carousel_game();
    switch (key)
    {
    case Key::left:
    case Key::right:
    case Key::l1:
    case Key::r1:
    {
        const bool page = key == Key::l1 || key == Key::r1;
        const int delta = key == Key::right || key == Key::r1 ? 1 : -1;
        if (page ? carousel_.page(delta) : carousel_.move(delta))
        {
            message_.clear();
            carousel_detail_.value = 0.0f;
            carousel_detail_.velocity = 0.0f;
            cue(page ? Cue::page : Cue::focus);
        }
        return;
    }
    case Key::down:
        home_focus_ = kFirstButtonDown;
        cue(Cue::focus);
        return;
    case Key::triangle:
        if (game == nullptr)
            return;
        if (game->remote)
        {
            say(tr("Download the game to change its settings."), true);
            cue(Cue::error);
            return;
        }
        if (game->title_id == 0)
        {
            say(tr("This game's settings cannot be saved (no title ID)."), true);
            cue(Cue::error);
            return;
        }
        open_game_settings_from_home(game->file);
        return;
    case Key::square:
    {
        // A game on download sources: into the download queue, or out of it.
        if (game != nullptr && game->remote)
        {
            queue_remote(*game);
            return;
        }
        // The Mods switch: all of the game's mods on or off at once. Their own switches (Game
        // settings > Mods) keep their state behind it.
        if (game == nullptr || game->title_id == 0 || game->mods == 0)
        {
            if (game != nullptr)
                cue(Cue::error);
            return;
        }
        const bool saved = services_.set_mods_enabled(game->title_id, !game->mods_enabled);
        if (saved)
            count_mods(*game, services_.mods(game->title_id));
        say(saved ? tr("Saved for this game. Applies on next launch.") : tr("Could not save. Please try again."),
            !saved);
        cue(saved ? Cue::toggle : Cue::error);
        return;
    }
    case Key::cross:
        press_ = 1.0f;
        if (game == nullptr || !home_.setup_ready)
        {
            // Nothing to start yet: Settings says what the console still needs.
            if (!home_.setup_ready)
                return go_section(3);
            cue(Cue::error);
            return;
        }
        if (game->remote)
        {
            play_remote(*game);
            return;
        }
        launch_from_ = {kLeft + carousel_.cursor() - carousel_.scroll(), kCardTop, kCardSize, kCardSize};
        launch(game->file, game->name, game->cover);
        return;
    default:
        return;
    }
}

void Launcher::draw_carousel_controllers(Canvas &c, float right, float cy)
{
    if (textures_.controller() == 0)
        return;
    // The four controllers, player 1 at the left, ending at `right`.
    constexpr float kWidth = 54.0f;
    constexpr float kGap = 10.0f;
    for (int player = 0; player < 4; ++player)
    {
        const std::size_t index = static_cast<std::size_t>(player);
        const Rect r{right - (4.0f - static_cast<float>(player)) * (kWidth + kGap) + kGap, cy - kWidth * 25.0f / 72.0f,
                     kWidth, kWidth * 50.0f / 72.0f};
        const float lit = tween::clamp01(controller_lit_[index].value);
        // A controller that joins lands with a small bounce.
        const float pop = controller_pop_[index];
        const float bounce = std::sin(kPi * (1.0f - pop)) * pop * motion();
        c.list.push_transform(1.0f + 0.34f * bounce, r.x + r.w * 0.5f, r.y + r.h * 0.5f, 0.0f, -5.0f * bounce);
        controller_icon(c, r, lit);
        c.list.pop_transform();
    }
}

void Launcher::draw_carousel(Canvas &c)
{
    gfx::DrawList &list = c.list;
    const Theme &t = th();
    const int count = static_cast<int>(carousel_order_.size());
    if (count == 0)
    {
        // Nothing to show yet: why, where the cards would be.
        const Rect panel{kLeft, kCardTop, kRight - kLeft, kCardSize};
        glass(c, panel, t.radius_card, t.panel.with_alpha(0.60f), t.panel_edge.with_alpha(0.30f));
        const char *title = !home_.setup_ready ? tr("Setup required") :
                            !games_loaded_     ? tr("Reading...") :
                                                 tr("No ROM files found.");
        text_shrink(c, title, 960.0f, baseline(kCardTop + 120.0f, 56.0f, t.size_display), t.size_display, t.title,
                    panel.w - 120.0f, Align::center);
        if (!home_.status.empty())
            notice_block(c, home_.status, kLeft + 80.0f, baseline(kCardTop + 200.0f, 36.0f, t.size_text), t.size_text,
                         36.0f, t.copy, panel.w - 160.0f, 3, home_.launch_failed);
        return;
    }

    const float focus = tween::clamp01(home_springs_[0].value);
    const auto card_x = [&](float at) { return kLeft + at - carousel_.scroll(); };
    // The card under the highlight grows, and dips when pressed.
    const auto grow = [&](float near) { return 1.0f + (0.06f - 0.03f * press_) * near * motion(); };

    // ---- the cards ----
    for (int row = std::max(0, carousel_.first_row() - 1); row <= std::min(count - 1, carousel_.last_row() + 1);
         ++row)
    {
        const Game &game = games_[static_cast<std::size_t>(carousel_order_[static_cast<std::size_t>(row)])];
        const float at = static_cast<float>(row) * kCardPitch;
        const float near = tween::clamp01(1.0f - std::fabs(carousel_.cursor() - at) / kCardPitch) * focus;
        const Rect r{card_x(at), kCardTop, kCardSize, kCardSize};
        list.push_transform(grow(near), r.x + r.w * 0.5f, r.y + r.h * 0.5f, 0.0f, 0.0f);
        if (game.remote)
            remote_cover(c, game.cover, r, t.radius_card, 0.9f);
        else
            cover(c, game.cover, r, t.radius_card, 0.9f);
        // A game being downloaded: how far it is, along the bottom of its card.
        const Download *download = game.remote ? download_of(game.key) : nullptr;
        if (download != nullptr && download->total > 0 &&
            (download->state == DownloadState::downloading || download->state == DownloadState::verifying))
        {
            const float done = std::clamp(
                static_cast<float>(static_cast<double>(download->done) / static_cast<double>(download->total)), 0.0f,
                1.0f);
            const Rect bar{r.x + 24.0f, r.y + r.h - 22.0f, r.w - 48.0f, 6.0f};
            list.rounded_rect(bar, 3.0f, t.scrim.with_alpha(0.7f));
            list.rounded_rect({bar.x, bar.y, std::max(6.0f, bar.w * done), bar.h}, 3.0f, t.accent);
        }
        list.pop_transform();
    }

    // ---- the highlight: a lit frame that glides from card to card ----
    const float x = card_x(carousel_.cursor());
    if (focus > 0.01f)
    {
        const Rect r{x, kCardTop, kCardSize, kCardSize};
        const float pulse = 0.5f + 0.5f * std::sin(c.time * 3.2f) * motion();
        list.push_opacity(focus);
        list.push_transform(grow(1.0f), r.x + r.w * 0.5f, r.y + r.h * 0.5f, 0.0f, 0.0f);
        list.shadow({r.x - 6.0f, r.y - 6.0f, r.w + 12.0f, r.h + 12.0f}, t.radius_card + 8.0f, 40.0f,
                    t.accent.with_alpha(0.16f + 0.14f * pulse));
        list.bordered_rect({r.x - 9.0f, r.y - 9.0f, r.w + 18.0f, r.h + 18.0f}, t.radius_card + 8.0f,
                           kWhite.with_alpha(0.0f), 5.0f, gfx::mix(t.accent, t.accent_pale, pulse));
        list.pop_transform();
        list.pop_opacity();
    }

    // ---- the selected game: its name over its card, its details under it ----
    const Game &game = *carousel_game();
    const float shown = tween::clamp01(carousel_detail_.value);
    const float left = std::max(kLeft, x);
    list.push_opacity((0.45f + 0.55f * focus) * shown);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - shown) * 8.0f * motion());
    {
        const Heavy heavy(c);
        text_fit(c, game.name, left, baseline(212.0f, 48.0f, t.size_heading), t.size_heading, t.title, kRight - left);
    }
    Color tone = t.meta;
    const std::string details =
        game.remote ? remote_state(game, &tone) :
                      game.format + "  /  " + game.size + "  /  " +
                          addons_line(game.addons_short, game.mods, game.mods_on, true);
    text_shrink(c, details, left, baseline(700.0f, 32.0f, t.size_small), t.size_small, tone, kRight - left - 160.0f);
    list.pop_transform();
    list.pop_opacity();
    text(c, list_position(carousel_.selected + 1, count), kRight, baseline(700.0f, 32.0f, t.size_small), t.size_small,
         t.accent_pale, Align::right);

    // What needs attention: a game that failed to start, or one without the language chosen. (What
    // the last press said is at the bottom right, draw_footer.)
    const float line = baseline(744.0f, 32.0f, t.size_small);
    if (!home_.status.empty())
        notice_block(c, home_.status, kLeft, line, t.size_small, 28.0f, home_.launch_failed ? t.warning : t.copy,
                     kRight - kLeft, 1, home_.launch_failed);
    else if (!game.language_note.empty())
        notice(c, game.language_note, left, line, t.size_small, t.warning, kRight - left, true);
}

void Launcher::draw_section_buttons(Canvas &c)
{
    gfx::DrawList &list = c.list;
    const Theme &t = th();
    for (int i = 0; i < kButtonCount; ++i)
    {
        const float f = tween::clamp01(home_springs_[static_cast<std::size_t>(1 + i)].value);
        const float cx = button_x(i);
        list.push_transform(1.0f + (0.10f - 0.05f * press_) * f * motion(), cx, kButtonY, 0.0f, 0.0f);
        if (f > 0.01f)
            list.shadow({cx - kButtonRadius, kButtonY - kButtonRadius, 2.0f * kButtonRadius, 2.0f * kButtonRadius},
                        kButtonRadius, 30.0f, t.accent.with_alpha(0.22f * f));
        list.circle(cx, kButtonY, kButtonRadius, gfx::mix(t.glass.with_alpha(0.82f), t.row_focus, f));
        list.ring(cx, kButtonY, kButtonRadius, 2.0f + 2.5f * f, gfx::mix(kWhite.with_alpha(0.16f), t.accent, f));
        icon(c, kButtons[i].icon, cx, kButtonY, 40.0f, gfx::mix(t.muted, t.title, f));
        list.pop_transform();
        // Its name shows while it is focused.
        list.push_opacity(f);
        text(c, tr(kButtons[i].name), cx, baseline(kButtonY + 72.0f, 0.0f, t.size_small), t.size_small, t.text,
             Align::center);
        list.pop_opacity();
    }
}

void Launcher::draw_carousel_home(Canvas &c)
{
    gfx::DrawList &list = c.list;
    const Theme &t = th();
    // Each band of the screen arrives a moment after the one above it.
    const auto begin_band = [&](int band, float rise)
    {
        const float e = tween::cubic_out((intro_ - 0.12f - t.stagger * static_cast<float>(band)) / 0.55f);
        list.push_opacity(e);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - e) * rise * motion());
    };
    const auto end_band = [&]
    {
        list.pop_transform();
        list.pop_opacity();
    };

    // ---- top bar: who is playing, the app and its version, the controllers (the clock is the shell's) ----
    begin_band(0, -18.0f);
    {
        const std::string who = playing_.empty() ? std::string{"ProsperoEden"} : playing_;
        std::size_t end = 0;
        (void)gfx::next_codepoint(who, &end);
        std::string initial = who.substr(0, end);
        if (initial.size() == 1)
            initial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(initial[0])));
        const float cx = kLeft + 40.0f;
        list.shadow({cx - 40.0f, kBarY - 40.0f, 80.0f, 80.0f}, 40.0f, 18.0f, Color{0.0f, 0.0f, 0.0f, 0.35f});
        list.circle(cx, kBarY, 40.0f, t.accent_deep);
        list.ring(cx, kBarY, 40.0f, 3.0f, t.accent.with_alpha(0.85f));
        if (fonts_.font != nullptr && fonts_.font->can_draw(initial))
        {
            const Heavy heavy(c);
            text(c, initial, cx, kBarY + t.size_heading * 0.35f, t.size_heading, t.title, Align::center);
        }
        else
        {
            icon(c, Icon::profile, cx, kBarY, 36.0f, t.title);
        }
        text_shrink(c, who, kLeft + 102.0f, baseline(kBarY - 34.0f, 40.0f, t.size_text), t.size_text, t.text, 640.0f);
        text(c, "PROSPEROEDEN  /  " + version_, kLeft + 102.0f, baseline(kBarY + 4.0f, 30.0f, t.size_small),
             t.size_small, t.meta, Align::left, 1.0f);
        draw_carousel_controllers(c, kRight - text_width(c, clock_, t.size_heading) - 36.0f, kBarY);
    }
    end_band();

    begin_band(1, 28.0f);
    draw_carousel(c);
    end_band();

    begin_band(2, 28.0f);
    draw_section_buttons(c);
    end_band();

    // ---- footer: the hints for what has the focus, the console's state at the right ----
    begin_band(3, 0.0f);
    std::string status = home_.system_status;
    if (services_.room_status().phase == Eden::Multiplayer::Phase::Connected)
        status += std::string{"  /  "} + tr("Multiplayer") + ": " + tr("Connected");
    const Game *game = home_focus_ == 0 ? carousel_game() : nullptr;
    static constexpr Hint kGame[] = {{Pad::cross, TR("Play")},
                                     {Pad::triangle, TR("Game settings")},
                                     {Pad::square, TR("Mods")},
                                     {Pad::l1, TR("Page"), Pad::r1}};
    static constexpr Hint kRemote[] = {{Pad::cross, TR("Download and play")},
                                       {Pad::square, TR("Download")},
                                       {Pad::l1, TR("Page"), Pad::r1}};
    static constexpr Hint kQueued[] = {{Pad::cross, TR("Play when downloaded")},
                                       {Pad::square, TR("Cancel download")},
                                       {Pad::l1, TR("Page"), Pad::r1}};
    static constexpr Hint kSectionHints[] = {{Pad::cross, TR("Select")}, {Pad::circle, TR("Back")}};
    static constexpr Hint kNone[] = {{Pad::dpad, TR("Navigate")}};
    if (game == nullptr)
        draw_footer(c, home_focus_ == 0 ? kNone : kSectionHints, home_focus_ == 0 ? 1 : 2, status);
    else if (!game->remote)
        draw_footer(c, kGame, 4, status);
    else if (download_of(game->key) != nullptr)
        draw_footer(c, kQueued, 3, status);
    else
        draw_footer(c, kRemote, 3, status);
    end_band();
}

} // namespace pe::ui
