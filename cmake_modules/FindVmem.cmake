# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# - Find VMEM (libvmem.h, libvmem.so)
# This module defines
#  VMEM_INCLUDE_DIR, directory containing headers
#  VMEM_SHARED_LIB, path to vmem's shared library
#  VMEM_STATIC_LIB, path to vmem's static library
#  VMEM_FOUND, whether libvmem has been found

find_path(VMEM_INCLUDE_DIR libvmem.h
  # make sure we don't accidentally pick up a different version
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(VMEM_SHARED_LIB vmem
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)
find_library(VMEM_STATIC_LIB libvmem.a
  NO_CMAKE_SYSTEM_PATH
  NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VMEM REQUIRED_VARS
  VMEM_SHARED_LIB VMEM_STATIC_LIB VMEM_INCLUDE_DIR)
