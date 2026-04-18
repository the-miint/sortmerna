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

/**
 * FILE: kvdb.hpp
 * Created: Nov 06, 2017 Mon
 */

#pragma once

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/version.h"

// RocksDB 10.0 changed DB::Open's output parameter from DB** to
// std::unique_ptr<DB>*. Select the matching field type so overload
// resolution on &kvdb picks the right Open() in kvdb.cpp.
#if ROCKSDB_MAJOR >= 10
#include <memory>
#endif

class KeyValueDatabase {
public:
	KeyValueDatabase(std::string const &kvdbPath);
#if ROCKSDB_MAJOR >= 10
	~KeyValueDatabase() = default;
#else
	~KeyValueDatabase() { delete kvdb; }
#endif

	void put(std::string key, std::string val);
	std::string get(std::string key);
	int clear(std::string dbPath);
private:
#if ROCKSDB_MAJOR >= 10
	std::unique_ptr<rocksdb::DB> kvdb;
#else
	rocksdb::DB* kvdb;
#endif
	rocksdb::Options options;
};
