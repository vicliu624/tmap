#include "tmap/profile_plot.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>

int main()
{
    tmap::ProfileColumn columns[3];
    tmap::ProfilePlot plot;
    assert(plot.begin(columns, 3, 100, 8) == tmap::Result::Ok);
    tmap::ProfileSample sample;
    sample.valid = true;
    sample.elevation_m = 100;
    assert(plot.append(sample));
    sample.distance_m = 10;
    sample.elevation_m = 120;
    assert(plot.append(sample));
    sample.distance_m = 20;
    sample.elevation_m = 90;
    assert(plot.append(sample));
    assert(columns[0].first_m == 100 && columns[0].last_m == 90);
    assert(columns[0].minimum_m == 90 && columns[0].maximum_m == 120);
    sample.distance_m = 50;
    sample.valid = false;
    assert(plot.append(sample));
    sample.distance_m = 60;
    sample.valid = true;
    sample.break_before = true;
    assert(plot.append(sample));
    assert(columns[1].valid && columns[1].gap && columns[1].break_before);
    sample.distance_m = 100;
    assert(tmap::ProfilePlot::visit(&plot, sample));
    assert(columns[2].valid && plot.samples() == 6);
    sample.distance_m = 99;
    assert(!plot.append(sample) && plot.result() == tmap::Result::Invalid);
    assert(plot.begin(columns, 1, 0, 1) == tmap::Result::Ok);
    sample.distance_m = 0;
    assert(plot.append(sample));
    assert(!plot.append(sample) && plot.result() == tmap::Result::BudgetExceeded);
    assert(plot.begin(nullptr, 1, 1, 1) == tmap::Result::Invalid);
    assert(plot.result() == tmap::Result::Invalid);
    assert(!plot.append(sample));
    assert(plot.begin(columns, 3, 100, 8) == tmap::Result::Ok);
    sample.elevation_m = std::numeric_limits<double>::quiet_NaN();
    assert(!plot.append(sample));
}
