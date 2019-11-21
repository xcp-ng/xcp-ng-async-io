/*
 * Copyright (C) 2019  Vates SAS - ronan.abhamon@vates.fr
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _XCP_NG_ASYNC_IO_IO_GLOBAL_H_
#define _XCP_NG_ASYNC_IO_IO_GLOBAL_H_

#include <stdbool.h>

// =============================================================================

#ifndef XCP_DECL_UNUSED
  #define XCP_DECL_UNUSED __attribute__((__unused__))
#endif // ifndef XCP_DECL_UNUSED

#ifndef XCP_UNUSED
  #define XCP_UNUSED(ARG) ((void)ARG)
#endif // ifndef XCP_UNUSED

#ifndef XCP_LIKELY
  #define XCP_LIKELY(EXPRESSION) __builtin_expect(!!(EXPRESSION), true)
#endif // ifndef XCP_LIKELY

#ifndef XCP_UNLIKELY
  #define XCP_UNLIKELY(EXPRESSION) __builtin_expect(!!(EXPRESSION), false)
#endif // ifndef XCP_UNLIKELY

#endif // ifndef _XCP_NG_ASYNC_IO_IO_GLOBAL_H_
