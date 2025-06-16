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

public class DummyStructuredDestination extends StructuredLogDestination {

  private String name;

  public DummyStructuredDestination(long arg0) {
    super(arg0);
  }

  public void deinit() {
    System.out.println("Deinit");
  }

  public int flush() {
    System.out.println("Flush");
    return SUCCESS;
  }

  public boolean init() {
    name = getOption("name");
    if (name == null) {
      System.err.println("Name is a required option for this destination");
      return false;
    }
    System.out.println("Init " + name);
    return true;
  }

  public boolean open() {
    return true;
  }

  public boolean isOpened() {
    return true;
  }

  public void close() {
    System.out.println("close");
  }

  public int send(LogMessage arg0) {
    arg0.release();
    return SUCCESS;
  }

  @Override
  public String getNameByUniqOptions() {
    InternalMessageSender.debug("getNameByUniqOptions");
    return "DummyStructuredDestination";
  }

}
