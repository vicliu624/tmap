#include "tmap/profile_job.h"

// Instantiate the header-only owner on actual embedded compilers so its stack
// frames are checked as well as the out-of-line core readers.
tmap::Result profileBegin(tmap::ProfileJob& job, tmap::RandomAccessFile& file,
                          const tmap::GeoPoint* points, size_t count, const tmap::ProfileJobOptions& options)
{
    return job.begin(file, points, count, options);
}
tmap::Result profileAdvance(tmap::ProfileJob& job, size_t budget) { return job.step(budget); }
void profileCancel(tmap::ProfileJob& job) { job.cancel(); }
size_t profileBytes(size_t columns) { return tmap::ProfileJob::requiredBytes(columns); }
