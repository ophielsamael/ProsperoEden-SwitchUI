// ProsperoEden - Launcher library: the game list, its details and per-game settings.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pe/ui/launcher.hpp"

#include "pe/core/log.hpp"

#include <cctype>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

namespace pe::ui
{

using audio::Cue;

namespace
{

constexpr Rect kDialog{550.0f, 180.0f, 820.0f, 720.0f};

// The game settings dialog's rows, and the window that shows five of them (also the Mods list's).
// Video to Language open what the game does differently from Settings (game_options.cpp).
enum GameRow : int
{
    row_mode,
    row_video,
    row_performance,
    row_audio,
    row_controls,
    row_language,
    row_mods,
    row_save,   // in builds that move saves
    row_delete, // a game a download source has: deleted from the console
};
constexpr float kDialogRowsTop = 334.0f;
constexpr float kDialogRowPitch = 96.0f;
constexpr float kDialogRowHeight = 94.0f;
constexpr int kDialogRowsShown = 5;
constexpr Rect kDialogWindow{592.0f, kDialogRowsTop, 736.0f,
                             kDialogRowPitch * (kDialogRowsShown - 1) + kDialogRowHeight};
constexpr float kDialogHints = 848.0f;

} // namespace

void order_by_recent(std::vector<Game> &games, const std::vector<Recent> &recents)
{
    const auto rank = [&recents](const Game &game)
    {
        for (std::size_t i = 0; i < recents.size(); ++i)
            if (recents[i].file == game.file)
                return i;
        return recents.size();
    };
    std::stable_sort(games.begin(), games.end(),
                     [&rank](const Game &a, const Game &b) { return rank(a) < rank(b); });
}

void Launcher::start_scan()
{
    if (!scan_.valid() && home_.setup_ready)
        scan_ = std::async(std::launch::async, [this] { return services_.games(); });
}

// A game's name as it is shown: with the characters a font has, the launcher's own or the
// console's. One with nothing left that says anything (a name in a script no font here has, or
// on a console whose fonts could not be read) is replaced by the name its file gives, rather than
// drawn as question marks.
static std::string shown_name(const gfx::Font *font, const std::string &name, const std::string &file)
{
    if (font == nullptr || name.empty() || font->can_draw(name))
        return name;
    std::string kept;
    for (const char c : font->drawable(name))
        if (c != ' ' || (!kept.empty() && kept.back() != ' '))
            kept += c;
    while (!kept.empty() && kept.back() == ' ')
        kept.pop_back();
    const bool says = std::any_of(kept.begin(), kept.end(), [](char c)
                                  { return std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                                           static_cast<unsigned char>(c) >= 0x80; });
    if (says)
        return kept;
    const std::string stem = file.substr(0, file.find_last_of('.'));
    return stem.empty() ? name : stem;
}

void Launcher::finish_scan(bool wait)
{
    if (!scan_.valid())
        return;
    if (!wait && scan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try
    {
        apply_games(scan_.get());
    }
    catch (const std::exception &error)
    {
        sys::log("game list: %s", error.what());
        games_loaded_ = true; // the list stays as it was
    }
}

void Launcher::apply_games(std::vector<Game> games)
{
    order_by_recent(games, home_.recents);
    // The same games in the same order (the usual case) keep the list where it is; otherwise
    // the selection follows its game (the last one played when the list is first read).
    bool same = games_loaded_ && games.size() == games_.size();
    for (std::size_t i = 0; same && i < games.size(); ++i)
        same = games[i].file == games_[i].file;
    const std::string selected = library_.selected < static_cast<int>(games_.size()) ?
                                     games_[static_cast<std::size_t>(library_.selected)].file :
                                     std::string{};
    games_ = std::move(games);
    for (Game &game : games_)
        game.name = shown_name(fonts_.font, game.name, game.file);
    games_loaded_ = true;
    for (Game &game : games_)
        if (game.title_id != 0)
            count_mods(game, services_.mods(game.title_id));
    if (!same)
    {
        int index = 0;
        for (int i = 0; i < static_cast<int>(games_.size()); ++i)
            if (games_[static_cast<std::size_t>(i)].file == selected)
                index = i;
        library_.reset(static_cast<int>(games_.size()), index);
        refresh_selected_game();
    }
    name_home_games();
}

void Launcher::name_home_games()
{
    // Games carry their own names; until one has been read the home screen names it by its file.
    for (const Game &game : games_)
    {
        if (game.file == home_.last_file)
            home_.last_title = game.name;
        for (Recent &recent : home_.recents)
            if (recent.file == game.file)
                recent.title = game.name;
    }
}

void Launcher::read_home()
{
    home_ = services_.home();
    home_.last_title = shown_name(fonts_.font, home_.last_title, home_.last_file);
    for (Recent &recent : home_.recents)
        recent.title = shown_name(fonts_.font, recent.title, recent.file);
    // Another profile has played other games: the carousel takes its order, on the same game.
    if (games_loaded_ && !games_.empty())
    {
        const std::string selected = games_[static_cast<std::size_t>(library_.selected)].file;
        order_by_recent(games_, home_.recents);
        if (games_[static_cast<std::size_t>(library_.selected)].file != selected)
        {
            int index = 0;
            for (int i = 0; i < static_cast<int>(games_.size()); ++i)
                if (games_[static_cast<std::size_t>(i)].file == selected)
                    index = i;
            library_.reset(static_cast<int>(games_.size()), index);
            refresh_selected_game();
        }
    }
}

void Launcher::check_games_present()
{
    // The home screen: its game, or one of its recent ones, is gone.
    if (screen_ == Screen::home && modal_ == Modal::none)
    {
        bool gone = home_.last_exists && !services_.game_exists(home_.last_file);
        for (const Recent &recent : home_.recents)
            gone = gone || !services_.game_exists(recent.file);
        if (gone)
            read_home();
    }
    // The carousel, whatever screen shows: a game's own dialogs stay with their game.
    if (modal_ == Modal::none)
        drop_missing_games();
}

bool Launcher::drop_missing_games()
{
    if (!games_loaded_)
        return false;
    const std::string selected =
        library_.selected < static_cast<int>(games_.size()) ? games_[static_cast<std::size_t>(library_.selected)].file :
                                                              std::string{};
    // A game on download sources has no file yet: it leaves when their lists do not have it.
    const auto gone = std::remove_if(games_.begin(), games_.end(), [this](const Game &game)
                                     { return !game.remote && !services_.game_exists(game.file); });
    if (gone == games_.end())
        return false;
    games_.erase(gone, games_.end());
    // The selection stays on its game, or on the one that took its place.
    int index = std::min(library_.selected, std::max(0, static_cast<int>(games_.size()) - 1));
    for (int i = 0; i < static_cast<int>(games_.size()); ++i)
        if (games_[static_cast<std::size_t>(i)].file == selected)
            index = i;
    library_.reset(static_cast<int>(games_.size()), index);
    refresh_selected_game();
    return true;
}

void Launcher::count_mods(Game &game, const std::vector<Mod> &mods)
{
    game.mods = static_cast<int>(mods.size());
    // The game's Mods switch (Square on the carousel) comes first: off, none of its mods is used,
    // whatever their own switches say.
    game.mods_enabled = mods.empty() || services_.mods_enabled(game.title_id);
    game.mods_on = !game.mods_enabled ? 0 : static_cast<int>(
        std::count_if(mods.begin(), mods.end(), [](const Mod &mod) { return mod.enabled; }));
}

void Launcher::read_mods(Game &game)
{
    mods_ = services_.mods(game.title_id);
    count_mods(game, mods_);
    mod_list_.clear();
    for (int mod = 0; mod < static_cast<int>(mods_.size()); ++mod)
    {
        mod_list_.push_back({mod, -1});
        for (int cheat = 0; cheat < static_cast<int>(mods_[static_cast<std::size_t>(mod)].cheats.size());
             ++cheat)
            mod_list_.push_back({mod, cheat});
    }
}

std::string Launcher::addons_line(const std::string &addons, int mods, int mods_on, bool brief)
{
    std::string line = addons;
    if (mods > 0)
    {
        // Mods that are switched off are still there: "1 of 2 mods on", or "1/2 mods" where there
        // is little room.
        const std::string count = std::to_string(mods);
        const char *counted = mods == 1 ? tr("{0} mod") : tr("{0} mods");
        line += (line.empty() ? "" : ", ") +
                (mods_on == mods ? fill(counted, {count}) :
                 brief ? fill(counted, {std::to_string(mods_on) + "/" + count}) :
                         fill(tr("{0} of {1} mods on"), {std::to_string(mods_on), count}));
    }
    return line.empty() ? std::string{tr("None")} : line;
}

std::string Launcher::hertz(int refresh)
{
    return fill(tr("{0} Hz"), {refresh == 1 ? "120" : "60"});
}

void Launcher::refresh_selected_game()
{
    const std::uint64_t id =
        games_.empty() ? 0 : games_[static_cast<std::size_t>(library_.selected)].title_id;
    selected_docked_ = id == 0 || services_.docked(id);
    detail_.value = 0.0f;
    detail_.velocity = 0.0f;
}

void Launcher::open_game_settings()
{
    const Game &game = games_[static_cast<std::size_t>(library_.selected)];
    if (game.remote)
    {
        say(tr("Download the game to change its settings."), true);
        cue(Cue::error);
        return;
    }
    if (game.title_id == 0)
    {
        say(tr("This game's settings cannot be saved (no title ID)."), true);
        cue(Cue::error);
        return;
    }
    game_settings_ = services_.game_settings(game.title_id);
    game_docked_ = selected_docked_;
    import_source_ = services_.save_transfer_available() ?
                         services_.save_import_source(game.title_id) : SaveSource::none;
    import_armed_ = false;
    delete_armed_ = false;
    read_mods(games_[static_cast<std::size_t>(library_.selected)]);
    open_modal(Modal::game);
    game_rows_.visible = kDialogRowsShown;
    game_rows_.pitch = kDialogRowPitch;
    game_rows_.reset(dialog_rows(Modal::game), 0);
}

// ---------------------------------------------------------------- game settings dialog

int Launcher::game_row(int row) const
{
    if (row < row_save)
        return row;
    return services_.save_transfer_available() ? row : row + 1;
}

void Launcher::press_game(Key key)
{
    Game &game = games_[static_cast<std::size_t>(library_.selected)];
    const int kind = game_row(option_);
    switch (key)
    {
    case Key::circle:
        close_modal();
        selected_docked_ = game_docked_;
        return;
    case Key::up:
    case Key::down:
        if (game_rows_.move(key == Key::down ? 1 : -1))
        {
            option_ = game_rows_.selected;
            message_.clear();
            import_armed_ = false;
            delete_armed_ = false;
            cue(Cue::focus);
        }
        return;
    case Key::square:
        if (kind != row_save)
            return;
        break;
    case Key::left:
    case Key::right:
    case Key::cross:
        break;
    default:
        return;
    }
    if (kind >= row_video && kind <= row_language)
    {
        // A kind of setting has its own list.
        if (key == Key::cross)
            open_game_options(kind - row_video);
        return;
    }
    if (kind == row_mods)
    {
        // The game's mods have their own list. It is read again: mods may have been copied in
        // since this dialog opened.
        if (key != Key::cross)
            return;
        read_mods(game);
        mod_rows_.visible = kDialogRowsShown;
        mod_rows_.pitch = kDialogRowPitch;
        mod_rows_.reset(static_cast<int>(mod_list_.size()), 0);
        modal_ = modal_shown_ = Modal::mods;
        message_.clear();
        cue(Cue::open);
        return;
    }
    if (kind == row_delete)
    {
        // Deleting a game a download source has: its file and all of its updates and DLC go, its save
        // data and settings stay. Cross asks first, then deletes.
        if (key != Key::cross)
            return;
        if (!delete_armed_)
        {
            delete_armed_ = true;
            say(tr("Press again to delete the game and its updates and DLC from the console. Save data and "
                   "settings are kept."),
                true);
            cue(Cue::notify);
            return;
        }
        delete_armed_ = false;
        std::string result;
        if (!services_.delete_game(game, &result))
        {
            say(result.empty() ? std::string{tr("Could not delete the game.")} : result, true);
            cue(Cue::error);
            return;
        }
        // On its sources only now: so it shows until the list is read again (it keeps its place).
        game.remote = true;
        game.title_id = 0;
        game.addons.clear();
        game.addons_short.clear();
        game.mods = game.mods_on = 0;
        close_modal();
        read_home();
        refresh_selected_game();
        say(result);
        cue(Cue::saved);
        return;
    }
    if (kind == row_save)
    {
        // Save data: Square copies the game's save out, Cross (twice) copies one in.
        std::string result;
        if (key == Key::square)
        {
            import_armed_ = false;
            const bool exported = services_.save_export(game.title_id, &result);
            say(result, !exported);
            cue(exported ? Cue::saved : Cue::error);
        }
        else if (key != Key::cross)
        {
            cue(Cue::error);
        }
        else if (import_source_ != SaveSource::none && !import_armed_)
        {
            // Importing replaces the save in use, so it asks first.
            import_armed_ = true;
            say(tr("Press again to replace this game's save. The current one is backed up."), true);
            cue(Cue::notify);
        }
        else
        {
            // With nothing to import the answer says where a save has to be put.
            import_armed_ = false;
            const bool imported = services_.save_import(game.title_id, &result);
            say(result, !imported && import_source_ != SaveSource::none);
            cue(imported ? Cue::saved : import_source_ != SaveSource::none ? Cue::error : Cue::notify);
        }
        return;
    }
    // The console mode.
    const bool saved = services_.set_docked(game.title_id, !game_docked_);
    if (saved)
        game_docked_ = !game_docked_;
    say(saved ? tr("Saved for this game. Applies on next launch.") : tr("Could not save. Please try again."),
        !saved);
    cue(saved ? Cue::toggle : Cue::error);
}

void Launcher::draw_game(Canvas &c, float open)
{
    gfx::DrawList &list = c.list;
    list.push_opacity(open);
    list.push_transform(1.0f - 0.03f * (1.0f - open) * motion(), 960.0f, 540.0f, 0.0f,
                        (1.0f - open) * 26.0f * motion());
    glass(c, kDialog, 26.0f, theme::kPanel.with_alpha(0.97f), theme::kPanelEdge.with_alpha(0.66f),
          1.6f);
    text_shrink(c, tr("Game settings"), 592.0f, baseline(218.0f, 62.0f, theme::kDisplay),
                theme::kDisplay, theme::kTitle, 736.0f);
    const Game *game = games_.empty() ? nullptr : &games_[static_cast<std::size_t>(library_.selected)];
    text_fit(c, game != nullptr ? game->name : std::string{}, 592.0f,
             baseline(291.0f, 32.0f, theme::kSmall), theme::kSmall, Color::rgb(0xbecbb9), 736.0f);

    // A kind of setting: what the game does differently from Settings, if anything.
    const auto changed = [this](int category)
    {
        const int count = game_overrides(category);
        return count == 0 ? std::string{tr("Follows Settings")} :
                            fill(tr("{0} changed"), {std::to_string(count)});
    };
    const std::string values[] = {
        game_docked_ ? tr("Docked") : tr("Handheld"),
        changed(row_video - row_video),
        changed(row_performance - row_video),
        changed(row_audio - row_video),
        changed(row_controls - row_video),
        changed(row_language - row_video),
        // With the game's Mods switch off (the Library's), none of them is on.
        mods_.empty() ? std::string{tr("No mods")} :
        game != nullptr && !game->mods_enabled ? std::string{tr("Off")} :
            fill(tr("{0} of {1} on"),
                 {std::to_string(std::count_if(mods_.begin(), mods_.end(),
                                               [](const Mod &mod) { return mod.enabled; })),
                  std::to_string(mods_.size())}),
        import_source_ == SaveSource::ryujinx ? tr("Ryujinx save found") :
        import_source_ == SaveSource::folder ? tr("Save folder found") : tr("Nothing to import"),
        game != nullptr ? game->size : std::string{},
    };
    static constexpr const char *kLabels[] = {TR("Console mode"), TR("Video"),    TR("Performance"),
                                              TR("Audio"),        TR("Controls"), TR("Language"),
                                              TR("Mods"),         TR("Save data"), TR("Delete from console")};
    // Five rows show; the list scrolls to the others.
    list.push_clip({kDialogWindow.x - 24.0f, kDialogWindow.y - 6.0f, kDialogWindow.w + 48.0f,
                    kDialogWindow.h + 12.0f});
    const auto row_top = [&](int row)
    { return kDialogRowsTop + static_cast<float>(row) * kDialogRowPitch - game_rows_.scroll(); };
    for (int row = game_rows_.first_row(); row <= game_rows_.last_row(); ++row)
    {
        list.push_opacity(game_rows_.row_alpha(row, kDialogRowHeight));
        plate_rest(c, kRowPlate, {592.0f, row_top(row), 736.0f, kDialogRowHeight});
        list.pop_opacity();
    }
    plate_focus(c, kRowPlate,
                {592.0f, kDialogRowsTop + game_rows_.cursor() - game_rows_.scroll(), 736.0f,
                 kDialogRowHeight},
                1.0f);
    for (int row = game_rows_.first_row(); row <= game_rows_.last_row(); ++row)
    {
        const float top = row_top(row);
        const float focus = row == option_ ? 1.0f : 0.0f;
        const int kind = game_row(row);
        list.push_opacity(game_rows_.row_alpha(row, kDialogRowHeight));
        // The value first: the row's name takes what it leaves. The kinds of setting, Mods and
        // Save data say what is there; the console mode is a choice; deleting says the game's size.
        const bool there = kind == row_mods ? !mods_.empty() && (game == nullptr || game->mods_enabled) :
                           kind == row_save ? import_source_ != SaveSource::none :
                           kind == row_delete ? false :
                                              game_overrides(kind - row_video) > 0;
        const float taken =
            kind != row_mode ?
                text_shrink(c, values[kind], 1292.0f, baseline(top, kDialogRowHeight, theme::kSmall),
                            theme::kSmall, there ? theme::kLimePale : theme::kMeta, 320.0f,
                            Align::right) :
                chooser(c, values[kind], 1296.0f, baseline(top, kDialogRowHeight, theme::kText24),
                        focus, theme::kLimePale);
        text_shrink(c, tr(kLabels[kind]), 628.0f, baseline(top, kDialogRowHeight, theme::kText24),
                    theme::kText24, kind == row_delete ? theme::kWarning : theme::kValue, 664.0f - taken - 28.0f);
        list.pop_opacity();
    }
    list.pop_clip();
    scrollbar(c, game_rows_, 1340.0f, kDialogWindow.y, kDialogWindow.h);

    if (!message_.empty())
    {
        // Up to two lines: what Save data answers is longer than a "Saved".
        notice_block(c, message_, 592.0f, kDialogHints - 6.0f, theme::kSmall, 26.0f,
                     message_warning_ ? theme::kWarning : theme::kLimePale, 736.0f, 2,
                     message_warning_);
    }
    else if (game_row(option_) == row_delete)
    {
        static constexpr Hint kDelete[] = {{Pad::cross, TR("Delete")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kDelete, 2, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else if (game_row(option_) == row_save)
    {
        static constexpr Hint kTransfer[] = {{Pad::cross, TR("Import")},
                                             {Pad::square, TR("Export a copy")},
                                             {Pad::circle, TR("Back")}};
        draw_hints(c, kTransfer, 3, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else if (option_ != row_mode)
    {
        static constexpr Hint kOpen[] = {{Pad::cross, TR("Open")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kOpen, 2, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else
    {
        static constexpr Hint kHints[] = {
            {Pad::updown, TR("Select")}, {Pad::leftright, TR("Change")}, {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 3, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

// ---------------------------------------------------------------- a game's mods

void Launcher::press_mods(Key key)
{
    Game &game = games_[static_cast<std::size_t>(library_.selected)];
    switch (key)
    {
    case Key::circle:
        // Back to the game's settings, on its Mods row.
        modal_ = modal_shown_ = Modal::game;
        message_.clear();
        cue(Cue::back);
        return;
    case Key::up:
    case Key::down:
        if (mod_rows_.move(key == Key::down ? 1 : -1))
        {
            message_.clear();
            cue(Cue::focus);
        }
        return;
    case Key::square:
    {
        // With no mods yet: the folder they go in, named after the game's ID, is made on request.
        if (!mods_.empty())
            return;
        const bool made = services_.make_mods_folder(game.title_id);
        say(made ? fill(tr("Created {0}. Copy each mod's folder into it."),
                        {services_.mods_folder(game.title_id)}) :
                   tr("Could not create the folder. Check that the game files folder can be written."),
            !made);
        cue(made ? Cue::saved : Cue::error);
        return;
    }
    case Key::cross:
    case Key::left:
    case Key::right:
    {
        if (mods_.empty())
            return;
        const ModRow at = mod_list_[static_cast<std::size_t>(mod_rows_.selected)];
        Mod &mod = mods_[static_cast<std::size_t>(at.mod)];
        // With the game's Mods switch off (the Library's), choosing a mod turns the switch on and
        // that mod with it: nobody switches a mod in a list that is switched off.
        const bool revive = !game.mods_enabled;
        if (at.cheat >= 0)
        {
            // One of the mod's cheats. Choosing it turns on what it runs behind: its mod, and the
            // game's Mods switch. One of a group (two frame rates) takes the other's place, so
            // the list is read again.
            const Cheat &cheat = mod.cheats[static_cast<std::size_t>(at.cheat)];
            const bool enabled = revive || !mod.enabled || !cheat.enabled;
            const bool saved =
                (!revive || services_.set_mods_enabled(game.title_id, true)) &&
                (mod.enabled || services_.set_mod_enabled(game.title_id, mod.name, true)) &&
                services_.set_cheat_enabled(game.title_id, mod.name, cheat.name, enabled);
            read_mods(game);
            if (mod_rows_.count != static_cast<int>(mod_list_.size()))
                mod_rows_.reset(static_cast<int>(mod_list_.size()), 0);
            say(saved ? tr("Saved for this game. Applies on next launch.") :
                        tr("Could not save. Please try again."),
                !saved);
            cue(saved ? Cue::toggle : Cue::error);
            return;
        }
        const bool enabled = revive || !mod.enabled;
        const bool saved = (!revive || services_.set_mods_enabled(game.title_id, true)) &&
                           services_.set_mod_enabled(game.title_id, mod.name, enabled);
        if (saved)
            mod.enabled = enabled;
        count_mods(game, mods_);
        say(saved ? tr("Saved for this game. Applies on next launch.") :
                    tr("Could not save. Please try again."),
            !saved);
        cue(saved ? Cue::toggle : Cue::error);
        return;
    }
    default:
        return;
    }
}

void Launcher::draw_mods(Canvas &c, float open)
{
    gfx::DrawList &list = c.list;
    list.push_opacity(open);
    list.push_transform(1.0f - 0.03f * (1.0f - open) * motion(), 960.0f, 540.0f, 0.0f,
                        (1.0f - open) * 26.0f * motion());
    glass(c, kDialog, 26.0f, theme::kPanel.with_alpha(0.97f), theme::kPanelEdge.with_alpha(0.66f),
          1.6f);
    text_shrink(c, tr("Mods"), 592.0f, baseline(218.0f, 62.0f, theme::kDisplay), theme::kDisplay,
                theme::kTitle, 736.0f);
    const Game *game = games_.empty() ? nullptr : &games_[static_cast<std::size_t>(library_.selected)];
    text_fit(c, game != nullptr ? game->name : std::string{}, 592.0f,
             baseline(291.0f, 32.0f, theme::kSmall), theme::kSmall, Color::rgb(0xbecbb9), 736.0f);

    if (mods_.empty())
    {
        text_block(c,
                   fill(tr("No mods for this game yet. Copy each mod's folder to {0}, next to roms/."),
                        {game != nullptr ? services_.mods_folder(game->title_id) : std::string{}}),
                   592.0f, baseline(364.0f, 40.0f, theme::kText24), theme::kText24, 40.0f,
                   theme::kBody, 736.0f, 5, kShrink);
    }
    else
    {
        list.push_clip({kDialogWindow.x - 24.0f, kDialogWindow.y - 6.0f, kDialogWindow.w + 48.0f,
                        kDialogWindow.h + 12.0f});
        const auto row_top = [&](int row)
        { return kDialogRowsTop + static_cast<float>(row) * kDialogRowPitch - mod_rows_.scroll(); };
        for (int row = mod_rows_.first_row(); row <= mod_rows_.last_row(); ++row)
        {
            list.push_opacity(mod_rows_.row_alpha(row, kDialogRowHeight));
            plate_rest(c, kRowPlate, {592.0f, row_top(row), 736.0f, kDialogRowHeight});
            list.pop_opacity();
        }
        plate_focus(c, kRowPlate,
                    {592.0f, kDialogRowsTop + mod_rows_.cursor() - mod_rows_.scroll(), 736.0f,
                     kDialogRowHeight},
                    1.0f);
        // With the game's Mods switch off (the Library's) no mod is used: their own switches keep
        // their state, drawn faint.
        const bool live = game == nullptr || game->mods_enabled;
        for (int row = mod_rows_.first_row(); row <= mod_rows_.last_row(); ++row)
        {
            const ModRow at = mod_list_[static_cast<std::size_t>(row)];
            const Mod &mod = mods_[static_cast<std::size_t>(at.mod)];
            const float top = row_top(row);
            list.push_opacity(mod_rows_.row_alpha(row, kDialogRowHeight));
            if (at.cheat >= 0)
            {
                // One of the mod's cheats, set in under it, with its own switch: faint while its
                // mod is off.
                const Cheat &cheat = mod.cheats[static_cast<std::size_t>(at.cheat)];
                const bool used = live && mod.enabled;
                text_fit(c, cheat.name, 668.0f,
                         baseline(top + (kDialogRowHeight - 38.0f) * 0.5f, 38.0f, theme::kText24),
                         theme::kText24, cheat.enabled && used ? theme::kValue : theme::kMeta, 520.0f);
                list.push_opacity(used ? 1.0f : 0.4f);
                toggle(c, 1292.0f, top + kDialogRowHeight * 0.5f, cheat.enabled ? 1.0f : 0.0f);
                list.pop_opacity();
                list.pop_opacity();
                continue;
            }
            // Its name as the player's folder has it, what it changes under it (with how many of
            // its cheats are chosen when it lists several), its switch.
            std::string kind = mod.kind;
            if (!mod.cheats.empty())
                kind += ", " + fill(tr("{0} of {1} on"),
                                    {std::to_string(std::count_if(
                                         mod.cheats.begin(), mod.cheats.end(),
                                         [](const Cheat &cheat) { return cheat.enabled; })),
                                     std::to_string(mod.cheats.size())});
            text_fit(c, mod.name, 628.0f, baseline(top + 14.0f, 38.0f, theme::kText24),
                     theme::kText24, mod.enabled && live ? theme::kValue : theme::kMeta, 560.0f);
            text_shrink(c, kind, 628.0f, baseline(top + 52.0f, 28.0f, theme::kSmall),
                        theme::kSmall, theme::kMeta, 560.0f);
            list.push_opacity(live ? 1.0f : 0.4f);
            toggle(c, 1292.0f, top + kDialogRowHeight * 0.5f, mod.enabled ? 1.0f : 0.0f);
            list.pop_opacity();
            list.pop_opacity();
        }
        list.pop_clip();
        scrollbar(c, mod_rows_, 1340.0f, kDialogWindow.y, kDialogWindow.h);
    }

    if (!message_.empty())
    {
        notice_block(c, message_, 592.0f, kDialogHints - 6.0f, theme::kSmall, 26.0f,
                     message_warning_ ? theme::kWarning : theme::kLimePale, 736.0f, 2,
                     message_warning_);
    }
    else if (mods_.empty())
    {
        static constexpr Hint kEmpty[] = {{Pad::square, TR("Create the folder")},
                                          {Pad::circle, TR("Back")}};
        draw_hints(c, kEmpty, 2, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    else
    {
        static constexpr Hint kHints[] = {{Pad::updown, TR("Select")},
                                          {Pad::cross, TR("Turn on or off")},
                                          {Pad::circle, TR("Back")}};
        draw_hints(c, kHints, 3, 592.0f, kDialogHints, theme::kCopy, 736.0f);
    }
    list.pop_transform();
    list.pop_opacity();
}

} // namespace pe::ui
