# ==============================================================================
# FindLiburing.cmake
#
# Copyright (C) 2019  xcp-ng-async-io
# Copyright (C) 2019  Vates SAS
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
# ==============================================================================

# Find the liburing library.
#
# This will define the following variables:
#   Liburing_FOUND
#   Liburing_INCLUDE_DIRS
#   Liburing_LIBRARIES
#
# and the following imported targets:
#   Liburing::Liburing

find_path(Liburing_INCLUDE_DIR
  NAMES liburing.h
  PATHS /usr/include
)

find_library(Liburing_LIBRARY
  NAMES uring
  PATHS /usr/lib /usr/lib64
)

mark_as_advanced(Liburing_FOUND Liburing_INCLUDE_DIR Liburing_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Liburing
  REQUIRED_VARS Liburing_INCLUDE_DIR Liburing_LIBRARY
)

if (Liburing_FOUND)
  get_filename_component(Liburing_INCLUDE_DIRS ${Liburing_INCLUDE_DIR} DIRECTORY)
endif ()
set(Liburing_LIBRARIES ${Liburing_LIBRARY})

if (Liburing_FOUND AND NOT TARGET Liburing::Liburing)
  add_library(Liburing::Liburing INTERFACE IMPORTED)
  set_target_properties(Liburing::Liburing PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${Liburing_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${Liburing_LIBRARIES}"
  )
endif ()
