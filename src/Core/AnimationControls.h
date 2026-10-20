#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// CPU playback state survives renderer rebuilds; it contains no GPU handles.
struct AnimationControls
{
    struct Clip
    {
        std::string name;
        double duration;
    };
    enum Action
    {
        Previous,
        Next,
        Play,
        Restart,
        Loop,
        Slower,
        Faster,
        Linear,
        Dual,
        Surface,
        Authored
    };
    struct Button
    {
        Action action;
        float x, y, w, h;
        const char *label;
    };
    inline static const std::vector<Button> buttons = {{Previous, 12, 40, 30, 26, "<"},
                                                       {Next, 282, 40, 30, 26, ">"},
                                                       {Play, 12, 78, 96, 26, "PLAY"},
                                                       {Restart, 116, 78, 96, 26, "RESTART"},
                                                       {Loop, 220, 78, 92, 26, "LOOP"},
                                                       {Slower, 12, 174, 44, 26, "-"},
                                                       {Faster, 268, 174, 44, 26, "+"},
                                                       {Linear, 12, 224, 144, 26, "LINEAR"},
                                                       {Dual, 164, 224, 148, 26, "DUAL QUAT"},
                                                       {Surface, 12, 274, 144, 26, "SURFACE"},
                                                       {Authored, 164, 274, 148, 26, "TRANSFORMED"}};
    std::vector<Clip> clips;
    size_t clip = 0;
    double time = 0, speed = 1;
    bool playing = true, loop = true, visible = true, dual = true, surface = true;
    bool discontinuity = true, modeDirty = true, scrubbing = false;
    double Duration() const
    {
        return clips.empty() ? 0 : clips[clip].duration;
    }
    void Advance(double dt)
    {
        if (!playing || scrubbing || Duration() <= 0 || !std::isfinite(dt) || dt < 0)
            return;
        time += dt * speed;
        if (time >= Duration())
        {
            if (loop)
                time = std::fmod(time, Duration());
            else
            {
                time = Duration();
                playing = false;
            }
        }
    }
    void Select(size_t index)
    {
        if (index >= clips.size())
            return;
        clip = index;
        time = 0;
        discontinuity = true;
    }
    void Seek(double seconds)
    {
        if (!std::isfinite(seconds))
            return;
        time = std::clamp(seconds, 0., Duration());
        playing = false;
        discontinuity = true;
    }
    void Activate(Action action)
    {
        switch (action)
        {
        case Previous:
            if (!clips.empty())
                Select((clip + clips.size() - 1) % clips.size());
            break;
        case Next:
            if (!clips.empty())
                Select((clip + 1) % clips.size());
            break;
        case Play:
            if (!playing && time >= Duration())
            {
                time = 0;
                discontinuity = true;
            }
            playing = !playing;
            break;
        case Restart:
            time = 0;
            discontinuity = true;
            break;
        case Loop:
            loop = !loop;
            break;
        case Slower:
            speed = std::max(.25, speed - .25);
            break;
        case Faster:
            speed = std::min(2., speed + .25);
            break;
        case Linear:
            if (dual)
            {
                dual = false;
                modeDirty = discontinuity = true;
            }
            break;
        case Dual:
            if (!dual)
            {
                dual = true;
                modeDirty = discontinuity = true;
            }
            break;
        case Surface:
            if (!surface)
            {
                surface = true;
                modeDirty = discontinuity = true;
            }
            break;
        case Authored:
            if (surface)
            {
                surface = false;
                modeDirty = discontinuity = true;
            }
            break;
        }
    }
    bool Contains(float x, float y) const
    {
        return visible && x >= 0 && x <= 324 && y >= 0 && y <= 332;
    }
    bool Press(float x, float y)
    {
        if (!Contains(x, y))
            return false;
        if (y >= 132 && y <= 160 && x >= 12 && x <= 312)
        {
            scrubbing = true;
            Seek((x - 12) / 300. * Duration());
            return true;
        }
        for (const auto &b : buttons)
            if (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h)
            {
                Activate(b.action);
                break;
            }
        return true;
    }
    bool Drag(float x)
    {
        if (!scrubbing)
            return false;
        Seek((x - 12) / 300. * Duration());
        return true;
    }
};
