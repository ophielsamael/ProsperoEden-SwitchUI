// ProsperoEden - Launcher home: every game on one carousel, and the system buttons under it.
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

// The top bar: who is playing at the left, the controllers and the clock at the right.
constexpr float kBarY = 96.0f; // its centre line
constexpr float kRight = 1800.0f;
// The carousel: the selected game's name over its card, its details under it.
constexpr float kCardsLeft = 120.0f;
constexpr float kCardTop = 292.0f;
// The system buttons, round, centred under the carousel.
constexpr float kButtonY = 846.0f;
constexpr float kButtonRadius = 46.0f;
constexpr float kButtonPitch = 150.0f;

float button_x(int index)
{
    return 960.0f + kButtonPitch * (static_cast<float>(index) - 1.0f);
}

// The first letter of a name, as the avatar shows it.
std::string initial(const std::string &name)
{
    if (name.empty())
        return "P";
    std::size_t length = 1;
    while (length < name.size() && (static_cast<unsigned char>(name[length]) & 0xc0u) == 0x80u)
        ++length;
    std::string letter = name.substr(0, length);
    if (length == 1)
        letter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
    return letter;
}

// The system buttons' marks, drawn from lines: a person, a gear, an "i".
void draw_mark(gfx::DrawList &list, int index, float cx, float cy, Color ink)
{
    if (index == 0)
    {
        list.circle(cx, cy - 9.0f, 9.0f, ink);
        arc(list, cx, cy + 19.0f, 17.0f, 4.5f, kPi, kPi, ink);
    }
    else if (index == 1)
    {
        list.ring(cx, cy, 11.0f, 5.0f, ink);
        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const float a = kPi * 0.25f * static_cast<float>(tooth);
            list.line(cx + 13.0f * std::cos(a), cy + 13.0f * std::sin(a), cx + 19.0f * std::cos(a),
                      cy + 19.0f * std::sin(a), 5.5f, ink);
        }
    }
    else
    {
        list.ring(cx, cy, 19.0f, 2.5f, ink);
        list.circle(cx, cy - 9.0f, 3.2f, ink);
        list.line(cx, cy - 2.0f, cx, cy + 11.0f, 4.5f, ink);
    }
}

} // namespace

void Launcher::press_home(Key key)
{
    if (home_focus_ == 0)
        return press_carousel(key);
    const int before = home_focus_;
    switch (key)
    {
    case Key::left:
        home_focus_ = std::max(1, home_focus_ - 1);
        break;
    case Key::right:
        home_focus_ = std::min(kSystemButtons, home_focus_ + 1);
        break;
    case Key::up:
        if (home_.setup_ready)
            home_focus_ = 0;
        break;
    case Key::circle:
        if (home_.setup_ready)
        {
            home_focus_ = 0;
            cue(Cue::back);
        }
        return;
    case Key::cross:
        press_ = 1.0f;
        if (home_focus_ == 1)
        {
            open_profiles();
        }
        else if (home_focus_ == 2)
        {
            open(Screen::settings, true);
            prefs_ = services_.preferences();
            section_.snap(1.0f);
        }
        else
        {
            open(Screen::about, true);
        }
        // A confirm on the home screen sounds the same whatever it opens.
        cues_.back() = Cue::select;
        return;
    default:
        return;
    }
    if (home_focus_ != before)
        cue(Cue::focus);
}

void Launcher::press_carousel(Key key)
{
    const int count = static_cast<int>(games_.size());
    Game *game = count > 0 ? &games_[static_cast<std::size_t>(library_.selected)] : nullptr;
    switch (key)
    {
    case Key::left:
    case Key::right:
        if (library_.move(key == Key::right ? 1 : -1))
        {
            message_.clear();
            refresh_selected_game();
            cue(Cue::focus);
        }
        return;
    case Key::l1:
    case Key::r1:
        if (library_.page(key == Key::r1 ? 1 : -1))
        {
            message_.clear();
            refresh_selected_game();
            cue(Cue::page);
        }
        return;
    case Key::down:
        home_focus_ = 2; // Settings, the middle one
        cue(Cue::focus);
        return;
    case Key::triangle:
        if (game != nullptr)
            open_game_settings();
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
        say(saved ? tr("Saved for this game. Applies on next launch.") :
                    tr("Could not save. Please try again."),
            !saved);
        cue(saved ? Cue::toggle : Cue::error);
        return;
    }
    case Key::cross:
        if (game == nullptr || !home_.setup_ready)
        {
            cue(Cue::error);
            return;
        }
        if (game->remote)
        {
            play_remote(*game);
            return;
        }
        launch(game->file, game->name, game->cover);
        return;
    default:
        return;
    }
}

void Launcher::update_controllers(float dt)
{
    const unsigned now = services_.controllers() & 0xfu;
    bool joined = false;
    bool left = false;
    for (int player = 0; player < 4; ++player)
    {
        const std::size_t index = static_cast<std::size_t>(player);
        const bool on = (now >> player & 1u) != 0;
        controller_lit_[index].target = on ? 1.0f : 0.0f;
        if (!controllers_known_)
        {
            controller_lit_[index].snap(controller_lit_[index].target);
        }
        else if (on != ((controllers_ >> player & 1u) != 0))
        {
            (on ? joined : left) = true;
            if (on)
                controller_pop_[index] = 1.0f;
        }
        controller_lit_[index].update(dt, 10.0f);
        controller_pop_[index] = std::max(0.0f, controller_pop_[index] - dt / 0.5f);
    }
    // One sound however many changed at once; none while a game is starting.
    if ((joined || left) && selected_game_.empty())
        cue(joined ? Cue::saved : Cue::modal_close);
    controllers_ = now;
    controllers_known_ = true;
}

void Launcher::draw_controllers(Canvas &c)
{
    if (textures_.controller() == 0)
        return;
    // The four controllers, player 1 at the left, ending before the clock.
    constexpr float kWidth = 54.0f;
    constexpr float kGap = 10.0f;
    const float right = kRight - text_width(c, clock_, theme::kClock) - 36.0f;
    for (int player = 0; player < 4; ++player)
    {
        const std::size_t index = static_cast<std::size_t>(player);
        const Rect r{right - (4.0f - static_cast<float>(player)) * (kWidth + kGap) + kGap,
                     kBarY - kWidth * 25.0f / 72.0f, kWidth, kWidth * 50.0f / 72.0f};
        const float lit = tween::clamp01(controller_lit_[index].value);
        // A controller that joins lands with a small bounce.
        const float pop = controller_pop_[index];
        const float bounce = std::sin(kPi * (1.0f - pop)) * pop * motion();
        c.list.push_transform(1.0f + 0.34f * bounce, r.x + r.w * 0.5f, r.y + r.h * 0.5f, 0.0f,
                              -5.0f * bounce);
        controller_icon(c, r, lit);
        c.list.pop_transform();
    }
}

void Launcher::draw_carousel(Canvas &c)
{
    gfx::DrawList &list = c.list;
    const int count = static_cast<int>(games_.size());
    if (count == 0)
    {
        // Nothing to show yet: why, where the cards would be.
        const Rect panel{kCardsLeft, kCardTop, 1680.0f, theme::kCardSize};
        glass(c, panel, 22.0f, theme::kGlass.with_alpha(0.60f), kWhite.with_alpha(0.10f));
        const char *title = !home_.setup_ready ? tr("Setup required") :
                            !games_loaded_     ? tr("Reading...") :
                                                 tr("No ROM files found.");
        text_shrink(c, title, 960.0f, baseline(kCardTop + 120.0f, 56.0f, theme::kDisplay), theme::kDisplay,
                    theme::kTitle, 1560.0f, Align::center);
        if (!home_.status.empty())
            notice_block(c, home_.status, 200.0f, baseline(kCardTop + 200.0f, 36.0f, theme::kText24),
                         theme::kText24, 36.0f, theme::kCopy, 1520.0f, 3, home_.launch_failed);
        return;
    }

    const float focus = tween::clamp01(home_springs_[0].value);
    const float pitch = library_.pitch;
    const float size = theme::kCardSize;
    const auto card_x = [&](float at) { return kCardsLeft + at - library_.scroll(); };
    // The card under the highlight grows, and dips when pressed.
    const auto grow = [&](float near) { return 1.0f + (0.06f - 0.03f * press_) * near * motion(); };

    // ---- the cards ----
    for (int row = std::max(0, library_.first_row() - 1); row <= library_.last_row(); ++row)
    {
        const Game &game = games_[static_cast<std::size_t>(row)];
        const float at = static_cast<float>(row) * pitch;
        const float near = tween::clamp01(1.0f - std::fabs(library_.cursor() - at) / pitch) * focus;
        const Rect r{card_x(at), kCardTop, size, size};
        list.push_transform(grow(near), r.x + r.w * 0.5f, r.y + r.h * 0.5f, 0.0f, 0.0f);
        if (game.remote)
            remote_cover(c, game.cover, r, 18.0f, 0.9f);
        else
            cover(c, game.cover, r, 18.0f, 0.9f);
        // A game being downloaded: how far it is, along the bottom of its card.
        const Download *download = game.remote ? download_of(game.key) : nullptr;
        if (download != nullptr && download->total > 0 &&
            (download->state == DownloadState::downloading || download->state == DownloadState::verifying))
        {
            const float done = std::clamp(static_cast<float>(static_cast<double>(download->done) /
                                                             static_cast<double>(download->total)),
                                          0.0f, 1.0f);
            const Rect bar{r.x + 24.0f, r.y + r.h - 22.0f, r.w - 48.0f, 6.0f};
            list.rounded_rect(bar, 3.0f, theme::kScrim.with_alpha(0.7f));
            list.rounded_rect({bar.x, bar.y, std::max(6.0f, bar.w * done), bar.h}, 3.0f, theme::kLime);
        }
        list.pop_transform();
    }

    // ---- the highlight: a lit frame that glides from card to card ----
    const float x = card_x(library_.cursor());
    if (focus > 0.01f)
    {
        const Rect r{x, kCardTop, size, size};
        const float pulse = 0.5f + 0.5f * std::sin(c.time * 3.2f) * motion();
        list.push_opacity(focus);
        list.push_transform(grow(1.0f), r.x + r.w * 0.5f, r.y + r.h * 0.5f, 0.0f, 0.0f);
        list.shadow({r.x - 6.0f, r.y - 6.0f, r.w + 12.0f, r.h + 12.0f}, 26.0f, 40.0f,
                    theme::kLime.with_alpha(0.16f + 0.14f * pulse));
        list.bordered_rect({r.x - 9.0f, r.y - 9.0f, r.w + 18.0f, r.h + 18.0f}, 26.0f, kWhite.with_alpha(0.0f),
                           5.0f, gfx::mix(theme::kLime, theme::kLimePale, pulse));
        list.pop_transform();
        list.pop_opacity();
    }

    // ---- the selected game: its name over its card, its details under it ----
    const Game &game = games_[static_cast<std::size_t>(library_.selected)];
    const float shown = tween::clamp01(detail_.value);
    const float left = std::max(kCardsLeft, x);
    list.push_opacity((0.45f + 0.55f * focus) * shown);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - shown) * 8.0f * motion());
    text_fit(c, game.name, left, baseline(212.0f, 48.0f, theme::kHeading), theme::kHeading, theme::kLimePale,
             kRight - left);
    Color tone = theme::kMeta;
    const std::string details =
        game.remote ? remote_state(game, &tone) :
                      game.format + "  /  " + game.size + "  /  " +
                          addons_line(game.addons_short, game.mods, game.mods_on, true);
    text_shrink(c, details, left, baseline(700.0f, 32.0f, theme::kSmall), theme::kSmall, tone,
                kRight - left - 160.0f);
    list.pop_transform();
    list.pop_opacity();
    text(c, list_position(library_.selected + 1, count), kRight, baseline(700.0f, 32.0f, theme::kSmall),
         theme::kSmall, theme::kLimePale, Align::right);

    // What the last press said, else what needs attention: a game that failed to start, or one
    // without the language chosen.
    const float line = baseline(744.0f, 32.0f, theme::kSmall);
    if (!message_.empty())
        notice(c, message_, kCardsLeft, line, theme::kSmall, message_warning_ ? theme::kWarning : theme::kLimePale,
               1680.0f, message_warning_);
    else if (!home_.status.empty())
        notice_block(c, home_.status, kCardsLeft, line, theme::kSmall, 28.0f,
                     home_.launch_failed ? theme::kWarning : theme::kCopy, 1680.0f, 1, home_.launch_failed);
    else if (!game.language_note.empty())
        notice(c, game.language_note, left, line, theme::kSmall, theme::kWarning, kRight - left, true);
}

void Launcher::draw_system_buttons(Canvas &c)
{
    gfx::DrawList &list = c.list;
    static constexpr const char *kNames[] = {TR("Profiles"), TR("Settings"), TR("About")};
    static_assert(std::size(kNames) == kSystemButtons);
    for (int i = 0; i < kSystemButtons; ++i)
    {
        const float f = tween::clamp01(home_springs_[static_cast<std::size_t>(1 + i)].value);
        const float cx = button_x(i);
        list.push_transform(1.0f + (0.10f - 0.05f * press_) * f * motion(), cx, kButtonY, 0.0f, 0.0f);
        if (f > 0.01f)
            list.shadow({cx - kButtonRadius, kButtonY - kButtonRadius, 2.0f * kButtonRadius, 2.0f * kButtonRadius},
                        kButtonRadius, 30.0f, theme::kLime.with_alpha(0.22f * f));
        list.circle(cx, kButtonY, kButtonRadius, gfx::mix(theme::kGlass.with_alpha(0.82f), theme::kRowFocus, f));
        list.ring(cx, kButtonY, kButtonRadius, 2.0f + 2.5f * f, gfx::mix(kWhite.with_alpha(0.16f), theme::kLime, f));
        draw_mark(list, i, cx, kButtonY, gfx::mix(theme::kMuted, theme::kTitle, f));
        list.pop_transform();
        // Its name shows while it is focused.
        list.push_opacity(f);
        text(c, tr(kNames[i]), cx, baseline(kButtonY + 72.0f, 0.0f, theme::kSmall), theme::kSmall, theme::kText,
             Align::center);
        list.pop_opacity();
    }
}

void Launcher::draw_home(Canvas &c)
{
    gfx::DrawList &list = c.list;
    // Each band of the screen arrives a moment after the one above it.
    const auto begin_band = [&](int band, float rise)
    {
        const float e = tween::cubic_out((intro_ - 0.12f - 0.08f * static_cast<float>(band)) / 0.55f);
        list.push_opacity(e);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - e) * rise * motion());
    };
    const auto end_band = [&]
    {
        list.pop_transform();
        list.pop_opacity();
    };

    // ---- top bar: who is playing, the controllers, the clock ----
    begin_band(0, -18.0f);
    const std::string who = playing_.empty() ? std::string{"ProsperoEden"} : playing_;
    list.shadow({112.0f, kBarY - 40.0f, 80.0f, 80.0f}, 40.0f, 18.0f, Color{0.0f, 0.0f, 0.0f, 0.35f});
    list.circle(152.0f, kBarY, 40.0f, theme::kLimeDeep);
    list.ring(152.0f, kBarY, 40.0f, 3.0f, theme::kLime.with_alpha(0.85f));
    text(c, initial(who), 152.0f, baseline(kBarY, 0.0f, theme::kHeading), theme::kHeading, theme::kTitle,
         Align::center);
    text_shrink(c, who, 214.0f, baseline(kBarY - 34.0f, 40.0f, theme::kText24), theme::kText24, theme::kText,
                640.0f);
    text(c, fill(tr("PS5 EDITION  /  {0}"), {version_}), 214.0f, baseline(kBarY + 4.0f, 30.0f, theme::kSmall),
         theme::kSmall, theme::kMeta, Align::left, 1.0f);
    draw_controllers(c);
    text(c, clock_, kRight, baseline(kBarY, 0.0f, theme::kClock), theme::kClock, Color::rgb(0xc1cdc2),
         Align::right);
    end_band();

    begin_band(1, 28.0f);
    draw_carousel(c);
    end_band();

    begin_band(2, 28.0f);
    draw_system_buttons(c);
    end_band();

    // ---- footer: the console's state at the left, the buttons at the right ----
    begin_band(3, 0.0f);
    list.rounded_rect({120.0f, 956.0f, 1680.0f, 1.0f}, 0.0f, theme::kText.with_alpha(0.10f));
    std::string status = home_.system_status;
    if (services_.room_status().phase == Eden::Multiplayer::Phase::Connected)
        status += std::string{"  /  "} + tr("Multiplayer") + ": " + tr("Connected");
    const float status_width = text_shrink(c, status, kCardsLeft, baseline(988.0f, 0.0f, theme::kSmall),
                                          theme::kSmall, Color::rgb(0x9eac9f), 760.0f);
    const float room = 1680.0f - status_width - 48.0f;
    const Game *game = home_focus_ == 0 && !games_.empty() ?
                           &games_[static_cast<std::size_t>(library_.selected)] : nullptr;
    static constexpr Hint kGame[] = {{Pad::cross, TR("Launch game")},
                                     {Pad::triangle, TR("Game settings")},
                                     {Pad::square, TR("Mods")},
                                     {Pad::l1, TR("Page"), Pad::r1}};
    static constexpr Hint kRemote[] = {{Pad::cross, TR("Download and play")},
                                       {Pad::square, TR("Download")},
                                       {Pad::l1, TR("Page"), Pad::r1}};
    static constexpr Hint kQueued[] = {{Pad::cross, TR("Play when downloaded")},
                                       {Pad::square, TR("Cancel download")},
                                       {Pad::l1, TR("Page"), Pad::r1}};
    static constexpr Hint kButtons[] = {{Pad::cross, TR("Select")}, {Pad::circle, TR("Back")}};
    static constexpr Hint kNone[] = {{Pad::dpad, TR("Navigate")}};
    if (game == nullptr)
        draw_hints(c, home_focus_ == 0 ? kNone : kButtons, home_focus_ == 0 ? 1 : 2, kRight, 988.0f,
                   theme::kMuted, room, Align::right);
    else if (!game->remote)
        draw_hints(c, kGame, 4, kRight, 988.0f, theme::kMuted, room, Align::right);
    else if (download_of(game->key) != nullptr)
        draw_hints(c, kQueued, 3, kRight, 988.0f, theme::kMuted, room, Align::right);
    else
        draw_hints(c, kRemote, 3, kRight, 988.0f, theme::kMuted, room, Align::right);
    end_band();
}

} // namespace pe::ui
