// Counter-attack power gauge (CounterAttackGaugue.swf): press at the right moment for the strongest hit.
#pragma once
#include <functional>
#include <memory>

#include "app/app.h"

namespace sbso::app {

class GaugeScreen : public Screen {
public:
    // done(power, timed_out): power is the damage before rounding; timed_out when the gauge ran out without a press.
    GaugeScreen(App& app, int max_damage, std::function<void(double, bool)> done);
    void tick(App& app) override;
    void draw(App& app, std::vector<engine::DrawCmd>& out) override;
    void pointer_down(App& app, engine::Point p) override;
    void key_down(App& app, int key) override;
    float tick_rate() const override { return 31.0f; }

private:
    double power() const;  // CounterAttackGauge.getPower
    void finish(App& app, double p, bool timeout);

    int max_damage_;
    std::function<void(double, bool)> done_;
    std::unique_ptr<engine::MovieClip> clip_;
    bool finished_ = false;
    int delay_ = 0;
    double pending_p_ = 0;
    bool pending_timeout_ = false;
    std::function<void(double, bool)> pending_cb_;
};

}  // namespace sbso::app
