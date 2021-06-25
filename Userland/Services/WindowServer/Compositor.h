/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/OwnPtr.h>
#include <AK/RefPtr.h>
#include <LibCore/Object.h>
#include <LibGfx/Color.h>
#include <LibGfx/DisjointRectSet.h>
#include <LibGfx/Font.h>
#include <WindowServer/Overlays.h>

namespace WindowServer {

class ClientConnection;
class Cursor;
class MultiScaleBitmaps;
class Window;
class WindowManager;

enum class WallpaperMode {
    Tile,
    Center,
    Stretch,
    Unchecked
};

class Compositor final : public Core::Object {
    C_OBJECT(Compositor)
    friend class Overlay;

public:
    static Compositor& the();

    void compose();
    void invalidate_window();
    void invalidate_screen();
    void invalidate_screen(const Gfx::IntRect&);

    void screen_resolution_changed();

    bool set_background_color(const String& background_color);

    bool set_wallpaper_mode(const String& mode);

    bool set_wallpaper(const String& path, Function<void(bool)>&& callback);
    String wallpaper_path() const { return m_wallpaper_path; }

    void invalidate_cursor(bool = false);
    Gfx::IntRect current_cursor_rect() const;
    const Cursor* current_cursor() const { return m_current_cursor; }
    void current_cursor_was_reloaded(const Cursor* new_cursor) { m_current_cursor = new_cursor; }

    void increment_display_link_count(Badge<ClientConnection>);
    void decrement_display_link_count(Badge<ClientConnection>);

    void increment_show_screen_number(Badge<ClientConnection>);
    void decrement_show_screen_number(Badge<ClientConnection>);
    bool showing_screen_numbers() const { return m_show_screen_number_count > 0; }

    void invalidate_after_theme_or_font_change()
    {
        update_fonts();
        invalidate_occlusions();
        overlays_theme_changed();
        invalidate_screen();
    }

    void invalidate_occlusions() { m_occlusions_dirty = true; }
    void overlay_rects_changed();

    template<typename T, typename... Args>
    OwnPtr<T> create_overlay(Args&&... args)
    {
        return adopt_own(*new T(forward<Args>(args)...));
    }

    template<typename F>
    IterationDecision for_each_overlay(F f)
    {
        for (auto& overlay : m_overlay_list) {
            IterationDecision decision = f(overlay);
            if (decision != IterationDecision::Continue)
                return decision;
        }
        return IterationDecision::Continue;
    }

    void did_construct_window_manager(Badge<WindowManager>);

    const Gfx::Bitmap* cursor_bitmap_for_screenshot(Badge<ClientConnection>, Screen&) const;
    const Gfx::Bitmap& front_bitmap_for_screenshot(Badge<ClientConnection>, Screen&) const;

private:
    Compositor();
    void init_bitmaps();
    bool render_animation_frame(Screen&, Gfx::DisjointRectSet&);
    void step_animations();
    void invalidate_current_screen_number_rects();
    void overlays_theme_changed();

    void render_overlays();
    void add_overlay(Overlay&);
    void remove_overlay(Overlay&);
    void update_fonts();
    void notify_display_links();
    void start_compose_async_timer();
    void recompute_overlay_rects();
    void recompute_occlusions();
    bool any_opaque_window_above_this_one_contains_rect(const Window&, const Gfx::IntRect&);
    void change_cursor(const Cursor*);
    void flush(Screen&);

    RefPtr<Core::Timer> m_compose_timer;
    RefPtr<Core::Timer> m_immediate_compose_timer;
    bool m_flash_flush { false };
    bool m_occlusions_dirty { true };
    bool m_invalidated_any { true };
    bool m_invalidated_window { false };
    bool m_invalidated_cursor { false };
    bool m_overlay_rects_changed { false };

    struct ScreenData {
        RefPtr<Gfx::Bitmap> m_front_bitmap;
        RefPtr<Gfx::Bitmap> m_back_bitmap;
        RefPtr<Gfx::Bitmap> m_temp_bitmap;
        OwnPtr<Gfx::Painter> m_back_painter;
        OwnPtr<Gfx::Painter> m_front_painter;
        OwnPtr<Gfx::Painter> m_temp_painter;
        RefPtr<Gfx::Bitmap> m_cursor_back_bitmap;
        OwnPtr<Gfx::Painter> m_cursor_back_painter;
        Gfx::IntRect m_last_cursor_rect;
        OwnPtr<ScreenNumberOverlay> m_screen_number_overlay;
        bool m_buffers_are_flipped { false };
        bool m_screen_can_set_buffer { false };
        bool m_cursor_back_is_valid { false };

        Gfx::DisjointRectSet m_flush_rects;
        Gfx::DisjointRectSet m_flush_transparent_rects;
        Gfx::DisjointRectSet m_flush_special_rects;

        Gfx::Painter& overlay_painter() { return *m_temp_painter; }

        void init_bitmaps(Compositor&, Screen&);
        void flip_buffers(Screen&);
        void draw_cursor(Screen&, const Gfx::IntRect&);
        bool restore_cursor_back(Screen&, Gfx::IntRect&);

        template<typename F>
        IterationDecision for_each_intersected_flushing_rect(Gfx::IntRect const& intersecting_rect, F f)
        {
            auto iterate_flush_rects = [&](Gfx::DisjointRectSet const& flush_rects) {
                for (auto& rect : flush_rects.rects()) {
                    auto intersection = intersecting_rect.intersected(rect);
                    if (intersection.is_empty())
                        continue;
                    IterationDecision decision = f(intersection);
                    if (decision != IterationDecision::Continue)
                        return decision;
                }
                return IterationDecision::Continue;
            };
            auto decision = iterate_flush_rects(m_flush_rects);
            if (decision != IterationDecision::Continue)
                return decision;
            // We do not have to iterate m_flush_special_rects here as these
            // technically should be removed anyway. m_flush_rects and
            // m_flush_transparent_rects should cover everything already
            return iterate_flush_rects(m_flush_transparent_rects);
        }
    };
    friend class ScreenData;
    Vector<ScreenData, default_screen_count> m_screen_data;

    IntrusiveList<Overlay, RawPtr<Overlay>, &Overlay::m_list_node> m_overlay_list;
    Gfx::DisjointRectSet m_overlay_rects;
    Gfx::DisjointRectSet m_dirty_screen_rects;
    Gfx::DisjointRectSet m_opaque_wallpaper_rects;

    Gfx::IntRect m_last_dnd_rect;

    String m_wallpaper_path { "" };
    WallpaperMode m_wallpaper_mode { WallpaperMode::Unchecked };
    RefPtr<Gfx::Bitmap> m_wallpaper;

    const Cursor* m_current_cursor { nullptr };
    Screen* m_current_cursor_screen { nullptr };
    unsigned m_current_cursor_frame { 0 };
    RefPtr<Core::Timer> m_cursor_timer;

    RefPtr<Core::Timer> m_display_link_notify_timer;
    size_t m_display_link_count { 0 };

    size_t m_show_screen_number_count { 0 };
    Optional<Gfx::Color> m_custom_background_color;
};

}
