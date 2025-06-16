/*
 * Copyright (c) 2014 Balabit
 * Copyright (c) 2014 Viktor Juhasz <viktor.juhasz@balabit.com>
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

package org.syslog_ng;

public class LogTemplate {
  private long configHandle;
  private long templateHandle;

  public static final int LTZ_LOCAL = 0;
  public static final int LTZ_SEND = 1;

  public LogTemplate(long configHandle) {
    this.configHandle = configHandle;
    templateHandle = create_new_template_instance(configHandle);
  }

  public boolean compile(String template) {
    return compile(templateHandle, template);
  }

  public String format(LogMessage msg) {
    return format(msg, 0, LTZ_SEND);
  }

  public String format(LogMessage msg, long logTemplateOptionsHandle) {
    return format(msg, logTemplateOptionsHandle, LTZ_SEND);
  }

  public String format(LogMessage msg, long logTemplateOptionsHandle, int timezone) {
    return format(templateHandle, msg.getHandle(), logTemplateOptionsHandle, timezone, 0);
  }

  public String format(LogMessage msg, long logTemplateOptionsHandle, int timezone, int seqnum) {
    return format(templateHandle, msg.getHandle(), logTemplateOptionsHandle, timezone, seqnum);
  }

  public void release() {
    unref(templateHandle);
  }

  private native long create_new_template_instance(long configHandle);
  private native boolean compile(long templateHandle, String template);
  private native String format(long templateHandle, long msgHandle, long templateOptionsHandle, int timezone, int seqnum);
  private native void unref(long templateHandle);
}
