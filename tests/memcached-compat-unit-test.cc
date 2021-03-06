/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2012 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "config.h"
#include <gtest/gtest.h>
#include <libcouchbase/couchbase.h>


class MemcachedCompatibility : public ::testing::Test
{
};

TEST_F(MemcachedCompatibility, createInstance)
{
    struct libcouchbase_memcached_st memcached;
    libcouchbase_t instance;

    memset(&memcached, 0, sizeof(memcached));
    memcached.serverlist = "localhost:11211;localhost:11212";

    EXPECT_EQ(LIBCOUCHBASE_SUCCESS,
              libcouchbase_create_compat(LIBCOUCHBASE_MEMCACHED_CLUSTER,
                                         &memcached,
                                         &instance, NULL));
    libcouchbase_destroy(instance);
}
