#include "Core/AnimationControls.h"
#include <iostream>
#include <stdexcept>
void Require(bool value, const char *text)
{
    if (!value)
        throw std::runtime_error(text);
}
int main()
{
    try
    {
        AnimationControls c;
        c.clips = {{"A", 2}, {"B", 4}};
        c.Advance(.5);
        Require(std::abs(c.time - .5) < 1e-8, "playback clock");
        c.Press(40, 90);
        c.Advance(1);
        Require(!c.playing && c.time == .5, "pause changed time");
        c.Press(162, 146);
        Require(c.scrubbing && !c.playing && c.time == 1, "timeline seek");
        c.Drag(900);
        Require(c.time == 2, "seek past endpoint");
        c.scrubbing = false;
        c.Activate(AnimationControls::Play);
        Require(c.playing && c.time == 0, "play from endpoint");
        c.speed = 2;
        c.Advance(.25);
        Require(c.time == .5, "speed ignored");
        c.loop = false;
        c.Advance(5);
        Require(c.time == 2 && !c.playing, "nonloop endpoint");
        c.loop = true;
        c.playing = true;
        c.time = 1.75;
        c.speed = 1;
        c.Advance(.5);
        Require(c.time == .25, "loop wrap");
        c.Activate(AnimationControls::Next);
        Require(c.clip == 1 && c.time == 0 && c.discontinuity, "clip change");
        c.Activate(AnimationControls::Next);
        Require(c.clip == 0, "next wrap");
        c.Activate(AnimationControls::Previous);
        Require(c.clip == 1, "previous wrap");
        c.discontinuity = c.modeDirty = false;
        c.Press(40, 235);
        Require(!c.dual && c.discontinuity && c.modeDirty, "linear UI");
        c.Press(180, 285);
        Require(!c.surface, "normal UI");
        c.Seek(1.2);
        c.speed = 1.75;
        auto restored = c;
        Require(restored.clip == c.clip && restored.time == 1.2 && !restored.playing &&
                    restored.speed == 1.75 && !restored.dual && !restored.surface,
                "state copy lost playback");
        c.visible = false;
        Require(!c.Press(40, 90), "hidden panel captured mouse");
        c.visible = true;
        Require(!c.Press(400, 90), "outside click captured");
        c.Seek(-2);
        Require(c.time == 0, "negative seek");
        for (int i = 0; i < 20; ++i)
            c.Activate(AnimationControls::Slower);
        Require(c.speed == .25, "minimum speed");
        for (int i = 0; i < 20; ++i)
            c.Activate(AnimationControls::Faster);
        Require(c.speed == 2, "maximum speed");
        AnimationControls empty;
        empty.Advance(1);
        empty.Activate(AnimationControls::Next);
        std::cout
            << "Animation controls: clock, hit tests, endpoints, mode changes and state preservation OK\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
