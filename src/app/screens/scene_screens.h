// Scripted scenes: the Krusty Krab cut scene before level 1-1 (CutSceneScreen.as) and the map based scene runner of
// GameCompleteLevel.as / MiniGameLevel.as (the ending, and the walk to Bubble Buddy's bonus game).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/app.h"
#include "engine/level_view.h"
#include "game/board.h"
#include "game/cutscene.h"
#include "game/level.h"

namespace sbso::app {

// Overlay shown on top of a battle that is about to begin; done() runs when it has faded away.
class KrustyCutScene : public Screen {
public:
    KrustyCutScene(App& app, std::function<void()> done);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    float tick_rate() const override { return 38.0f; }

private:
    enum class Phase { Wait, Dialogue, Fade, Done };
    struct Actor { std::string type; float x, y; bool visible = true; };
    App& app_;
    std::function<void()> done_;
    Phase phase_ = Phase::Wait;
    int timer_ = 0;
    float alpha_ = 1;
    engine::LevelView view_;
    std::unique_ptr<engine::MovieClip> bg_, reveal_;
    std::vector<engine::LevelView::UnitVisual> actors_;
    std::vector<std::unique_ptr<engine::MovieClip>> bubbles_;
    bool loaded_ = false;
};

class SceneScreen : public Screen {
public:
    enum class Mode { GameComplete, Minigame };
    SceneScreen(App& app, Mode mode, int game = 0);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    float tick_rate() const override { return 31.0f; }
    bool ok() const { return ok_; }

private:
    enum class Wait { None, Dialogue, Actors, Timer, FadeIn, FadeOut, Credits, Minigame };
    void next_step();
    void do_step(const sbso::game::CutStep& s);
    engine::LevelView::UnitVisual* actor_at(int tx, int ty);
    void finish();

    App& app_;
    Mode mode_;
    int game_;
    bool ok_ = false;
    sbso::game::Level level_;
    sbso::game::Board board_;
    engine::LevelView view_;
    std::vector<sbso::game::CutStep> steps_;
    size_t step_ = 0;
    Wait wait_ = Wait::None;
    int timer_ = 0, next_id_ = 0;
    float alpha_ = 0;  // fade mask (1 = black)
    float map_x_ = 0, map_y_ = 0;
    std::vector<engine::LevelView::UnitVisual> actors_;
    struct Walk { int id; std::vector<sbso::game::Pt> path; size_t next = 0; };
    std::vector<Walk> walks_;
    std::unique_ptr<engine::MovieClip> bg_;  // krusty interior once the "cutscene" step ran
    bool interior_ = false;
};

}  // namespace sbso::app
