/*
 * Copyright (c) 2023 Balazs Scheidler <bazsi77@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

int number_of_sockets;

SEC("socket")
int random_choice(struct __sk_buff *skb)
{
  if (number_of_sockets == 0)
    return -1;

  return bpf_get_prandom_u32() % number_of_sockets;
}
