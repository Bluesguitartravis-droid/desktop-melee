/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <aurora/desktop_monitor_geometry.hpp>
#include <cassert>
#include <cstdio>
#include <array>
using namespace aurora::desktop::monitor_geometry;
int main() {
    AuroraDesktopGeometry g{};
    Rect primary{0,0,1920,1080}, bar{0,1040,1920,1080};
    assert(make(primary,{0,0,1920,1040},&bar,g));
    assert(g.screen_x==0 && g.platform_top==1040 && g.platform_right==1920);
    Rect left{-2560,-200,0,1240}, left_bar{-2560,1200,0,1240};
    assert(make(left,{-2560,-200,0,1200},&left_bar,g));
    assert(g.screen_x==-2560 && g.screen_y==-200 && g.platform_top==1400 && g.platform_right==2560);
    assert(make(left,{-2560,-200,0,1240},&bar,g)); // reject primary taskbar
    assert(g.platform_top==1392 && g.platform_left==0 && g.platform_right==2560);
    Rect right{1920,0,5760,2160};
    assert(make(right,right,nullptr,g));
    assert(g.screen_x==1920 && g.width==3840 && g.platform_top==2112);
    Rect portrait{1920,-1920,3000,0}, portrait_bar{1920,-48,3000,0};
    assert(make(portrait,{1920,-1920,3000,-48},&portrait_bar,g));
    assert(g.height==1920 && g.width==1080 && g.screen_y==-1920 && g.platform_top==1872);
    Rect top_bar{0,0,1920,48}, side_bar{0,0,48,1080};
    assert(make(primary,{0,48,1920,1080},&top_bar,g) && g.platform_top==1032);
    assert(make(primary,{48,0,1920,1080},&side_bar,g) && g.platform_top==1032);
    Rect hidden{0,1079,1920,1080};
    assert(make(primary,primary,&hidden,g) && g.platform_top==1032);
    Rect small{0,0,48,32};
    assert(make(small,small,nullptr,g) && g.platform_top==1);
    assert(!make({0,0,0,100},primary,nullptr,g));
    assert(!make({0,0,20000,100},primary,nullptr,g));
    assert(!make({0,0,100,3},primary,nullptr,g));
    {
        std::array<Rect,2> screens{{{0,0,1920,1080},{1920,0,3840,1080}}};
        std::array<int32_t,2> floors{{1040,1040}};
        assert(make_span(screens,floors,g));
        assert(g.screen_x==0 && g.screen_y==0 && g.width==3840 && g.height==1080);
        assert(g.platform_left==0 && g.platform_right==1920 && g.platform_top==1040);
    }
    {
        std::array<Rect,2> screens{{{-2560,-200,0,1240},{0,0,1920,1080}}};
        std::array<int32_t,2> floors{{1200,1040}};
        assert(make_span(screens,floors,g));
        assert(g.screen_x==-2560 && g.screen_y==-200 && g.width==4480 && g.height==1440);
        assert(g.platform_top==1400); // absolute y=1200, lowest monitor floor
    }
    {
        std::array<Rect,2> disconnected{{{0,0,1920,900},{1920,1000,3840,2080}}};
        std::array<int32_t,2> floors{{852,2032}};
        assert(make_span(disconnected,floors,g));
        assert(g.screen_x==0 && g.screen_y==0 && g.width==3840 && g.height==2080);
        assert(g.platform_left==1920 && g.platform_right==3840 && g.platform_top==2032);
    }
    {
        std::array<Rect,5> staggered{{{-1920,-1080,-960,0},{-960,-1080,0,0},
            {0,-1080,2560,0},{2560,-900,3200,-260},{640,0,1920,1080}}};
        std::array<int32_t,5> floors{{-48,-48,-48,-308,1040}};
        assert(make_span(staggered,floors,g));
        assert(g.screen_x==-1920 && g.screen_y==-1080);
        assert(g.width==5120 && g.height==2160 && g.platform_top==2120);
        assert(g.platform_left==2560 && g.platform_right==3840);
    }
    puts("monitor geometry: single, side-by-side and staggered all-monitor spans passed");
}
