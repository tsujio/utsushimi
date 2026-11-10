#pragma once
namespace utsushimi {
// Called only by the body loop. Optional diagnostic; never controls scheduling.
void report_task_load(bool search_active);
}
