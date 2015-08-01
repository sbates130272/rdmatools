////////////////////////////////////////////////////////////////////////
//
// Copyright 2014 PMC-Sierra, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you
// may not use this file except in compliance with the License. You may
// obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0 Unless required by
// applicable law or agreed to in writing, software distributed under the
// License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for
// the specific language governing permissions and limitations under the
// License.
//
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
//
//   Author: Logan Gunthorpe
//
//   Date:   Oct 23 2014
//
//   Description:
//     Common reporting routines
//
////////////////////////////////////////////////////////////////////////

#include "report.h"
#include "suffix.h"

static double timeval_to_secs(struct timeval *t)
{
    return  t->tv_sec + t->tv_usec / 1e6;
}

void report_transfer_rate_elapsed(FILE *outf, double elapsed_time,
                                  size_t bytes)
{
    double bytes_d = bytes;
    double throughput = bytes_d / elapsed_time;

    const char *b_suffix = suffix_si_get(&bytes_d);
    const char *t_suffix = suffix_si_get(&throughput);

    const char *e_suffix = " ";
    if (elapsed_time < 1)
        e_suffix = suffix_si_get(&elapsed_time);

    fprintf(outf, "%6.2f%sB in %-6.1f%ss   %6.2f%sB/s",
            bytes_d, b_suffix, elapsed_time, e_suffix, throughput,
            t_suffix);
}

void report_transfer_rate(FILE *outf, struct timeval *start_time,
                          struct timeval *end_time, size_t bytes)
{
    double elapsed_time = timeval_to_secs(end_time) -
        timeval_to_secs(start_time);
    report_transfer_rate_elapsed(outf, elapsed_time, bytes);
}

void report_transfer_bin_rate_elapsed(FILE *outf, double elapsed_time,
                                      size_t bytes)
{
    double bytes_d = bytes;
    double throughput = bytes_d / elapsed_time;

    const char *b_suffix = suffix_dbinary_get(&bytes_d);
    const char *t_suffix = suffix_dbinary_get(&throughput);

    const char *e_suffix = " ";
    if (elapsed_time < 1)
        e_suffix = suffix_si_get(&elapsed_time);

    fprintf(outf, "%6.2f%sB in %-6.1f%ss   %6.2f%sB/s",
            bytes_d, b_suffix, elapsed_time, e_suffix,
            throughput, t_suffix);
}

void report_transfer_bin_rate(FILE *outf, struct timeval *start_time,
                              struct timeval *end_time, size_t bytes)
{
    double elapsed_time = timeval_to_secs(end_time) -
        timeval_to_secs(start_time);
    report_transfer_bin_rate_elapsed(outf, elapsed_time, bytes);
}
