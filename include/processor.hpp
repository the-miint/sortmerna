/*
@copyright 2016-2026 Clarity Genomics BVBA
@copyright 2012-2016 Bonsai Bioinformatics Research Group
@copyright 2014-2016 Knight Lab, Department of Pediatrics, UCSD, La Jolla

@parblock
SortMeRNA - next-generation reads filter for metatranscriptomic or total RNA

This is a free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

SortMeRNA is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with SortMeRNA. If not, see <http://www.gnu.org/licenses/>.
@endparblock

@contributors Jenya Kopylova   jenya.kopylov@gmail.com
              Laurent Noé      laurent.noe@lifl.fr
              Pierre Pericard  pierre.pericard@lifl.fr
              Daniel McDonald  wasade@gmail.com
              Mikaël Salson    mikael.salson@lifl.fr
              Hélène Touzet    helene.touzet@lifl.fr
              Rob Knight       robknight@ucsd.edu
              biocodz          biocodz@protonmail.com
*/

/*
 * file: processor.hpp
 * Created: Nov 06, 2017 Mon
 *
 * processing functions designed to run in threads
 */

#pragma once

// forward
class Readfeed;
class References;
class Refstats;
struct Runopts;
struct Index;
struct Readstats;
class KeyValueDatabase;

void align(Readfeed& readfeed, Readstats& readstats, Index& index, KeyValueDatabase& kvdb, Runopts& opts);

/*
 * Variant of align() that operates on a single, already-loaded index part.
 * Runs only the worker pool (align2) and Readstats housekeeping — does NOT
 * call index.load/unload or refs.load/unload. Used by the pre-loaded-index
 * library API (smr_index_load + smr_run_seqs_with_index); the CLI continues
 * to use the classic align() which handles multi-part iteration.
 */
void align_loaded(Readfeed& readfeed, Readstats& readstats,
                  Index& index, References& refs, Refstats& refstats,
                  KeyValueDatabase& kvdb, Runopts& opts,
                  uint16_t idx_num, uint16_t idx_part);

void denovo_stats(Readfeed& readfeed, Readstats& readstats, KeyValueDatabase& kvdb, Runopts& opts);
